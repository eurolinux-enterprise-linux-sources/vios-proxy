/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */
#include <map>
#include <vector>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>

#include "vios_gchannel.h"
#include "vios_framing.h"
#include "vios_utility.h"

using namespace ViosFraming;
using namespace Utility;


//
// constructor
//
ViosGChannel::ViosGChannel ( const std::string& thePathName,
                             const int theTimeoutTickCount ) :
        pathName ( thePathName ),
        isOnProbation ( false ),
        lastHostError (0),
        clientFd ( INVALID_SOCKET ),
        isClientFdRead ( false ),
        isClientFdWrite ( false ),
        isIndClientReadable ( false ),
        isIndClientWriteable ( false ),
        isIndClientError ( false ),
        hostFd ( INVALID_SOCKET ),
        isHostFdRead ( false ),
        isHostFdWrite ( false ),
        isIndHostReadable ( false ),
        isIndHostWriteable ( false ),
        isIndHostError ( false ),
        hostConnState ( CLOSED ),
        hostConnRxSubstate ( GET_SYNC0 ),
        hostConnTxSubstate ( SEND_IDLE ),
        hostConnResetSubstate ( RESET_IDLE ),
        hostRxHeader (),
        hostRxHeaderBytecount (0),
        hostTxHeader (),
        hostTxHeaderBytecount (0),
        hostRxBufferBytecount (0),
        hostTxBufferBytecount (0),
        hostTxBufferPending (false),
        guestToken (0x21212121),    // '!!!!'
        hostToken (0x21212121),     // '!!!!'
        timeoutTicks (0),
        timeoutTickCount (theTimeoutTickCount)
{
    // In opening the channel we take over the host-side file endpoint.
    Reconnect();
}


//
// destructor
//
ViosGChannel::~ViosGChannel()
{
    // dispose of sockets
    if (INVALID_SOCKET != hostFd)
    {
        LOG (INFO, "Close host channel: " + pathName);
        close (hostFd);
        hostFd = INVALID_SOCKET;
    }
    if (INVALID_SOCKET != clientFd)
    {
        LOG (INFO, "Close client channel: " + pathName);
        close (clientFd);
        clientFd = INVALID_SOCKET;
    }

    LOG (INFO, "Destroy host channel: " + pathName);
}


//
// Reconnect
//
// Try to open the channel to host Fd endpoint
//
void ViosGChannel::Reconnect ( void )
{
    assert (hostFd == INVALID_SOCKET);

    hostFd = open (pathName.c_str(), O_RDWR);
    
    if (INVALID_SOCKET != hostFd)
    {
        // Set nonblocking
        int opts;
        
        opts = fcntl(hostFd, F_GETFL);
        if (opts < 0)
        {
            LogError (ERROR, "fcntl(F_GETFL) on host channel: " + pathName, errno );
            close (hostFd);
            hostFd = INVALID_SOCKET;
        }
        else
        {
            opts |= O_NONBLOCK;
            if (fcntl(hostFd, F_SETFL, opts) < 0)
            {
                LogError (ERROR, "fcntl(F_SETFL) on host channel: " + pathName, errno );
                close (hostFd);
                hostFd = INVALID_SOCKET;
            }
            else
            {
                LOG (INFO, "Open host channel: " + pathName);
            }
        }
    }
    else
    {
        lastHostError = errno;
        LogError ( WARN, "Failed to open host channel: " + pathName, errno );
    }
}



//
// RequestReset
//
// A channel is running and now it should be reset.
// Set things up so a RESET frame gets sent in due order.
// In this case the RESET will be sent to the hostFd protocol channel.
//
void ViosGChannel::RequestReset ( const std::string&  reason  )
{
    std::string ID;
    LOG (INFO, "Resetting host channel: " + pathName
        + ": " + hostTxHeader.GetConnectionId(ID, guestToken, hostToken)
        + ": " + reason);
    
    hostConnResetSubstate = RESET_REQUESTED;
    isHostFdWrite = true;

    if (INVALID_SOCKET != clientFd)
    {
        LOG (DEBUG, "RequestReset closes client channel: " + pathName);
        
        close (clientFd);
        clientFd = INVALID_SOCKET;
        isIndClientReadable = false;
        isIndClientWriteable = false;
    }
}

///////////////////////////////////////////
// RunProtocolTx
//
// Manage stalled transmits (to host file fd)
//
void ViosGChannel::RunProtocolTx ( void )
{
    assert (hostFd != INVALID_SOCKET && isIndHostWriteable);

    if (hostConnTxSubstate == SEND_HEADER)
    {
        // Send the rest of a stuck header
        uint8_t * pHdr = (uint8_t *)&hostTxHeader;
        size_t lenToSend = ViosHeaderSize;

        // Adjust for bytes sent so far
        pHdr      += hostTxHeaderBytecount;
        lenToSend -= hostTxHeaderBytecount;

        // Send more bytes
        ssize_t nSent;

        SocketRtnStatus rStatus = HostFdSend( pHdr, lenToSend, nSent, hostTxHeaderBytecount );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            hostConnTxSubstate = SEND_BUFFER;
            break;

        case SRTN_OK_INCOMPLETE:
            // A subset of the bytes needed was sent
            // FALLTHROUGH
        case SRTN_FULL:
            // socket not accepting any data now
            // Try again later.
            isHostFdWrite = true;
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            return;
        }
    }

    if (hostConnTxSubstate == SEND_BUFFER)
    {
        if (hostTxBufferPending)
        {
            // Send the rest of a stuck buffer
            uint8_t * pHdr = (uint8_t *)&hostTxBuffer;
            size_t lenToSend = hostTxHeader.GetPayloadLength();

            // Adjust for bytes sent so far
            pHdr      += hostTxBufferBytecount;
            lenToSend -= hostTxBufferBytecount;

            assert (lenToSend > 0);

            // Send more bytes
            SocketRtnStatus rStatus;
            ssize_t nSent;

            rStatus = HostFdSend( pHdr, lenToSend, nSent, hostTxBufferBytecount );

            switch (rStatus)
            {
            case SRTN_NORMAL:
                hostConnTxSubstate = SEND_IDLE;
                hostTxBufferPending = false;

            case SRTN_OK_INCOMPLETE:
                // A subset of the bytes needed was sent
                // FALLTHROUGH
            case SRTN_FULL:
                // socket not accepting any data now
                // fall out of loop. try again later.
                isHostFdWrite = true;
                break;

            case SRTN_CLOSED:
                // FALLTHROUGH
            case SRTN_ERROR:
                // Error has been logged and socket is closed.
                // This session is over.
                return;
            }
        }
        else
        {
            hostConnTxSubstate = SEND_IDLE;
        }
    }

    if (hostConnTxSubstate == SEND_IDLE)
    {
        isHostFdWrite = false;
    }
}


//
// RunRxProtocol
//
//
void ViosGChannel::RunProtocolRx ( void )
{
    //
    // V1 host protocol executor (GUEST SIDE)
    //
    // On entry:
    //    isIndXxx has be set true by a callback.
    //    isFdXxx  tells of the socket is in the poller's fd sets.
    assert (hostFd != INVALID_SOCKET && isIndHostReadable);

    //
    // Get the sync byte 0
    //
    while (isIndHostReadable &&
           hostConnRxSubstate == GET_SYNC0 &&
           hostConnResetSubstate == RESET_IDLE)
    {
        // Keep reading bytes until sync0 seen.
        SocketRtnStatus rStatus;
        ssize_t nRead;
        int dummy;

        rStatus = HostFdRecv( &hostRxHeader.sync0, 1, nRead, dummy );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            // We read the byte. Check it out.
            if (hostRxHeader.sync0 == ViosProtocolSync0)
            {
                // We have Sync0.
                hostConnRxSubstate = GET_SYNC1;
            }
            else
            {
                // Zero'th byte is not Sync0.
                // In SYN_SENT state, consume it and try again.
                // In any other state call for a reset.
                if (hostConnState == SYN_SENT)
                {
                    // try again
                }
                else
                {
                    RequestReset ( "Vios channel synchronization failed" );
                }
            }
            break;

        case SRTN_EMPTY:
            // fall out of loop. try again later.
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            return;

        case SRTN_OK_INCOMPLETE:
            // is degenerate on a one-byte read
            assert (false);
            return;
        }
    }

    //
    // Get the sync byte 1
    //
    while (isIndHostReadable && hostConnRxSubstate == GET_SYNC1)
    {
        // read sync1
        SocketRtnStatus rStatus;
        ssize_t nRead;
        int dummy;

        rStatus = HostFdRecv( &hostRxHeader.sync1, 1, nRead, dummy );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            // We read the byte. Check it out.
            if (hostRxHeader.sync1 == ViosProtocolSync1)
            {
                // We have Sync1.
                hostConnRxSubstate = GET_HEADER;
                hostRxHeaderBytecount = 2;      // Sync bytes already received.
            }
            else
            {
                // byte is not Sync1. Continue resynching.
                if (hostRxHeader.sync1 == ViosProtocolSync0)
                {
                    // second byte not sync1 but is another sync0
                    // try getting sync1 again
                }
                else
                {
                    // second byte not sync1 nor synch0
                    // restart resync from the top
                    hostConnRxSubstate = GET_SYNC0;
                }
            }
            break;

        case SRTN_EMPTY:
            // fall out of loop. try again later.
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            return;

        case SRTN_OK_INCOMPLETE:
            // is degenerate on a one-byte read
            assert (false);
            return;
        }
    }

    //
    // Next accumulate the header and data for a message
    //
    if (hostConnRxSubstate == GET_HEADER)
    {
        //
        // Receiving a header
        //
        if (isIndHostReadable)
        {
            // Cast the header structure as a series of bytes
            // to be read from socket
            uint8_t * pHdr = (uint8_t *)&hostRxHeader;
            size_t lenToRead = ViosHeaderSize;

            // Adjust for bytes read so far
            pHdr      += hostRxHeaderBytecount;
            lenToRead -= hostRxHeaderBytecount;

            // Read more bytes
            SocketRtnStatus rStatus;
            ssize_t nRead;

            rStatus = HostFdRecv( pHdr, lenToRead, nRead, hostRxHeaderBytecount );

            bool isOk = false;

            switch (rStatus)
            {
            case SRTN_NORMAL:
                // Normal case (he says) - full header was received.
                // See if the header passes muster
                if (!hostRxHeader.CheckSync())
                {
                    LOG (DEBUG, "Header CheckSync fails: " + pathName);
                }
                else if (hostRxHeader.GetVersion() != ViosProtocolVersion)
                {
                    LOG (DEBUG, "Version check fails: " + pathName);
                }
                else
                    isOk = true;

                if (isOk)
                {
                    // Look for some payload accumulation
                    // The only headers allowed to have payload bytes
                    // are DATA frames in Connected state. For now
                    // let everybody have payloads.
                    if (hostRxHeader.GetPayloadLength() > 0)
                    {
                        // Set up to receive a payload buffer
                        hostRxBufferBytecount = 0;
                        hostConnRxSubstate = GET_DATA;
                    }
                    else
                    {
                        // Payload length == 0. The header is ready.
                        hostConnRxSubstate = MESSAGE_READY;
                    }
                }
                else
                {
                    // protocol error - send a reset frame
                    RequestReset ( "Vios version not supported" );
                }
                break;

            case SRTN_OK_INCOMPLETE:
                // A subset of the bytes needed was returned
                break;

            case SRTN_EMPTY:
                // fall out of loop. try again later.
                break;

            case SRTN_CLOSED:
                // FALLTHROUGH
            case SRTN_ERROR:
                // Error has been logged and socket is closed.
                // This session is over.
                return;
            }
        }
        else
        {
            // We are trying to receive a header but the socket is
            // not readable. Try again later.
            isHostFdRead = true;
        }
    }

    if (hostConnRxSubstate == GET_DATA)
    {
        //
        // Receiving a data block
        //
        if (isIndHostReadable)
        {
            // Cast the header structure as a series of bytes
            // to be read from socket
            uint8_t * pHdr = (uint8_t *)&hostRxBuffer;
            size_t lenToRead = hostRxHeader.GetPayloadLength();

            // Adjust for bytes read so far
            pHdr      += hostRxBufferBytecount;
            lenToRead -= hostRxBufferBytecount;

            // Read more bytes
            SocketRtnStatus rStatus;
            ssize_t nRead;

            rStatus = HostFdRecv( pHdr, lenToRead, nRead, hostRxBufferBytecount );

            switch (rStatus)
            {
            case SRTN_NORMAL:
                hostConnRxSubstate = MESSAGE_READY;
                break;

            case SRTN_OK_INCOMPLETE:
                // A subset of the bytes needed was returned
                break;

            case SRTN_EMPTY:
                // fall out of loop. try again later.
                break;

            case SRTN_CLOSED:
                // FALLTHROUGH
            case SRTN_ERROR:
                // Error has been logged and socket is closed.
                // This session is over.
                return;
            }
        }
        else
        {
            // We are trying to receive a buffer but the socket is
            // not readable. Try again later.
            isHostFdRead = true;
        }
    }

    if (hostConnRxSubstate == MESSAGE_READY)
    {
        // Done receiving a header or a header and data.
        // Process them.
        
        if ( ProcessProtocolMessage () )
        {
            // After processing a message, clean up state for next
            hostConnRxSubstate = GET_SYNC0;
            hostRxHeaderBytecount = 0;
            hostRxBufferBytecount = 0;
            isHostFdRead = true;
        }
    }

    if (hostConnRxSubstate == MESSAGE_TO_CLIENT)
    {
        // This is a stalling state where we don't let the last
        // received message go until it's contents are sent to the client.
        SocketRtnStatus rStatus = ClientSocketSend ();

        switch (rStatus)
        {
        case SRTN_NORMAL:
            // After processing a message, clean up state for next
            hostConnRxSubstate = GET_SYNC0;
            
            hostRxHeaderBytecount = 0;
            hostRxBufferBytecount = 0;
            isHostFdRead = true;
            break;

        case SRTN_OK_INCOMPLETE:
            // A subset of the bytes needed were sent
            // Fallthrough
        case SRTN_FULL:
            // None of the bytes sent, but still ok otherwise
            // Go to state that stalls next guest Rx until service receives
            // last data frame.
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            RequestReset ( "Channel closed by client" );
        }
    }
}


//
// RunProtocol
//
// The guestManager has accepted a callback from select() and
// set our isIndXxx flags.
//
// Now take a pass at what the protocol should do next.
//
void ViosGChannel::RunProtocol ( void )
{
    LOG (DEBUG, "RunProtocol: " + pathName);
    
    assert ( CLOSED != hostConnState );
    assert ( LISTEN != hostConnState );

    //
    // Top level dispatch
    //
    int loopLimit;

    if (hostConnResetSubstate == RESET_IDLE)
    {
        //
        // No reset in progress
        // Run the transmit-to-host flush one time
        //
        if (hostFd != INVALID_SOCKET && isIndHostWriteable)
        {
            RunProtocolTx ();
        }
        
        //
        // Run the receive-from-host process.
        // This handles protocol ops and host-to-client data messages.
        //
        if (hostFd != INVALID_SOCKET && isIndHostReadable)
        {
            RunProtocolRx ();
        }

        //
        // While ESTABLISHED look for client data to send to host
        //
        if (hostConnState == ESTABLISHED)
        {
            if (isIndClientReadable)
                isClientFdRead = false;

            if (hostConnTxSubstate == SEND_IDLE)
            {
                // Client is readable and host Tx pipe is idle.
                // send some client data to host
                SocketRtnStatus rStatus = ClientSocketRecv ();

                switch (rStatus)
                {
                case SRTN_NORMAL:
                {
                    // Some bytes were read into the txBuffer
                    assert (hostTxBufferBytecount > 0 && hostTxBufferBytecount <= 65535);

                    uint16_t payloadLen = (uint16_t)hostTxBufferBytecount;

                    // construct a transmit header for the data
                    hostTxHeader.SetSync();
                    hostTxHeader.SetVersion( ViosProtocolVersion );
                    hostTxHeader.SetControl( ViosCtrl_DATA );
                    hostTxHeader.SetGuestToken ( guestToken );
                    hostTxHeader.SetHostToken ( hostToken );
                    hostTxHeader.SetPayloadLength ( payloadLen );
                    hostTxHeaderBytecount = 0;
                    hostTxBufferBytecount = 0;

                    hostConnTxSubstate = SEND_HEADER;
                    hostTxBufferPending = true;

                    // Stop reading the client channel and start sending on host
                    isClientFdRead = false;
                    isHostFdWrite = true;
                    break;
                }

                case SRTN_EMPTY:
                    // no bytes read
                    // Wake up when some arrive
                    isHostFdRead = true;
                    break;

                case SRTN_CLOSED:
                    // FALLTHROUGH
                case SRTN_ERROR:
                    // Error has been logged and socket is closed.
                    // This session is over.
                    RequestReset ( "Channel closed by service" );
                    break;

                case SRTN_OK_INCOMPLETE:
                    // incomplete does not make sense here.
                    assert (false);
                    break;
                }
            }
        }
    }

    if (hostConnResetSubstate == RESET_REQUESTED)
    {
        //
        // The protocol has decided to close the current connection.
        // Wait for the Tx engine to go idle
        //
        if (hostConnTxSubstate != SEND_IDLE)
        {
            if (hostFd != INVALID_SOCKET && isIndHostWriteable)
                RunProtocolTx ();
        }

        // If still sending then come back later
        if (hostConnTxSubstate != SEND_IDLE)
            return;

        // Transmit is idle
        // Wait for Rx engine to go between frames

        if (hostConnRxSubstate != GET_SYNC0)
        {
            for (loopLimit = 0;
                 hostFd != INVALID_SOCKET && isIndHostReadable && loopLimit < 10000;
                 loopLimit++)
            {
                RunProtocolRx ();
            }
        }

        if (hostConnRxSubstate != GET_SYNC0)
            return;

        // Now both Tx and Rx are between frames.
        // Generate our best Reset frame and send it.
        hostTxHeader.SetSync();
        hostTxHeader.SetVersion( ViosProtocolVersion );
        hostTxHeader.SetControl( ViosCtrl_RESET );
        hostTxHeader.SetGuestToken ( guestToken );
        hostTxHeader.SetHostToken ( hostToken );
        hostTxHeader.SetPayloadLength ( 0 );

        hostTxHeaderBytecount = 0;

        SocketRtnStatus rStatus;
        ssize_t nSent;

        rStatus = HostFdSend( (void *)&hostTxHeader, ViosHeaderSize, nSent, hostTxHeaderBytecount );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            // The guest connection has been reset
            ResetCleanUp();
            break;

        case SRTN_OK_INCOMPLETE:
            // Fallthrough
        case SRTN_FULL:
            // A subset of the bytes needed was sent
            // Try again next time
            hostConnResetSubstate = RESET_SEND_IN_FLIGHT;
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            return;
        }
    }


    if (hostConnResetSubstate ==  RESET_SEND_IN_FLIGHT)
    {
        // A reset frame is in flight
        if (hostConnTxSubstate != SEND_IDLE)
        {
            if (hostFd != INVALID_SOCKET && isIndHostWriteable)
                RunProtocolTx ();
        }

        // If still sending then come back later
        if (hostConnTxSubstate != SEND_IDLE)
            return;

        // The host connection has been reset
        ResetCleanUp();
    }
}


////////////////////////////////////////////////
// StartConnection
//
// The poller has accepted a new client connection
// and is passing the socket to this channel.
// Flush the connection to the host and start up
// a protocol session.
//
bool ViosGChannel::StartConnection (SOCKET clientSocket)
{
    assert (INVALID_SOCKET == clientFd);

    // Make sure the host channel is connected.
    if (INVALID_SOCKET == hostFd)
    {
        // Host is NOT connected. Try to reconnect.
        Reconnect();

        // Fail if the host didn't connect
        if (INVALID_SOCKET == hostFd)
        {
            return false;
        }
    }

    // Generate a buffer of protocol Sync0 characters
    memset (&hostTxBuffer, ViosProtocolSync0, ViosCtrl_PayloadBufferSize);

    // Write the buffer to the hostFd.
    // This write should go to a host that is in Listen state.
    // If the host is NOT in Listen state then it will be after it receives this.
    // If this write does not go into the hostFd then there are larger problems
    // and camping out to do repeated writes is of little benefit.
    write (hostFd, hostTxBuffer, ViosCtrl_MaxPayloadSize);

    // Repeatedly read the hostFd to drain any junk from previous sessions.
    ssize_t bytesRead;
    do {
        bytesRead = read (hostFd, hostRxBuffer, sizeof(hostRxBuffer));
    } while (bytesRead > 0);
    
    // Now there may be a RESET frame in flight that was not consumed
    // by the read loop. Handle them in them SYN_SENT state.
    
    // Tidy up all the class variables
    ResetCleanUp();

    // Store the new socket
    clientFd = clientSocket;

    // Generate a guest token
    guestToken = ViosFraming::GenerateToken();
    
    // Construct and send a SYN packet
    hostTxHeader.SetSync();
    hostTxHeader.SetVersion( ViosProtocolVersion );
    hostTxHeader.SetControl( ViosCtrl_SYN );
    hostTxHeader.SetGuestToken ( guestToken );
    hostTxHeader.SetHostToken ( 0x3F3F3F3F ); //'????'
    hostTxHeader.SetPayloadLength ( 0 );
    hostTxHeaderBytecount = 0;
    hostTxBufferBytecount = 0;

    SocketRtnStatus rStatus;
    ssize_t nSent;
    
    rStatus = HostFdSend( (void *)&hostTxHeader, ViosHeaderSize, nSent, hostTxHeaderBytecount );
    
    switch (rStatus)
    {
    case SRTN_NORMAL:
        // Syn has been sent
        break;

    case SRTN_OK_INCOMPLETE:
        // Fallthrough
    case SRTN_FULL:
        // A subset of the bytes needed was sent
        // Try again next time
        hostConnTxSubstate = SEND_HEADER;
        hostTxBufferPending = true;
        break;

    case SRTN_CLOSED:
        // FALLTHROUGH
    case SRTN_ERROR:
        // Error has been logged and socket is closed.
        // This session is over.
        return false;
        break;
    }

    std::string ID;
    LOG (INFO, "Transition to SYN_SENT: " + pathName + ": " +
         hostTxHeader.GetConnectionId(ID, hostTxHeader.GetGuestToken(), hostTxHeader.GetHostToken()));
    hostConnState = SYN_SENT;

    timeoutTicks = timeoutTickCount;

    // Now host-sides read/write needs to go to run the protocol
    isHostFdRead = true;
    isHostFdWrite = true;

    // Inhibit client-side reads. Upon opening the socket all clients send an
    // opening hello frame. That frame will sit in the poll/select loop
    // generating a storm of client-read-ready signals that are ignored until
    // the host sends the SYNACK back.
    isClientFdRead = false;
    
    return true;
}


////////////////////////////////////////////////
// ClockTick
//
// Execute time-based actions.
//
void ViosGChannel::ClockTick (void)
{
    // Timeout pending SYNs
    if (hostConnState == SYN_SENT)
    {
        // We sent a SYN some time ago.
        // Have we waited too long?
        assert (timeoutTicks > 0);
        timeoutTicks -= 1;

        if (0 == timeoutTicks)
        {
            // This session has timed out.
            // In the timeout duration the service-side peer has not
            // responded with a SYNACK or a RESET. He's not there.
            std::string ID;
            LOG (ERROR, "Session connection timeout: " + pathName + ": " +
            hostTxHeader.GetConnectionId(ID, hostTxHeader.GetGuestToken(), hostTxHeader.GetHostToken()));
            assert (INVALID_SOCKET != clientFd);
            close (clientFd);
            clientFd = INVALID_SOCKET;

            ResetCleanUp();
        }
    }
}


////////////////////////////////////////////////
// ProcessProtocolMessage
//
// A fully framed message has been received from guest UDS.
// Process it.
// Return true if frame processing is complete.
//
bool ViosGChannel::ProcessProtocolMessage (void)
{
    LOG (DEBUG, "ProcessProtocolMessage: " + pathName);
    
    //
    // Respond to received RESET protocol frames
    //
    if (hostRxHeader.GetCtrl() == ViosCtrl_RESET)
    {
        // assume that the reset is to take effect
        bool doTheReset = true;
        
        if (SYN_SENT == hostConnState)
        {
            // Possibly ignore this RESET if it is not related
            // to the SYN we just sent. It may be a stale, in-flight
            // RESET from a previous session.
            if (guestToken == hostRxHeader.GetGuestToken())
            {
                // This session is reset
                LOG (DEBUG, "RESET received in SYN_SENT state: " + pathName);
            }
            else
            {
                // Wrong session
                LOG (DEBUG, "RESET received in SYN_SENT state. Wrong guest token: " + pathName);
                doTheReset = false; // ignore this reset
            }
        }
        else if (ESTABLISHED == hostConnState)
        {
            // In SynRcvd or Established states we accept a reset but do not
            // send one back.
            // For diag purposes make sure this reset belongs to the current session.
            LOG (DEBUG, "RESET received: " + pathName);
            if (guestToken != hostRxHeader.GetGuestToken())
            {
                LOG (DEBUG, "RESET received for wrong session: " + pathName);
            }
        }

        if (doTheReset)
        {
            // Process the reset
            if (clientFd != INVALID_SOCKET)
            {
                close (clientFd);
                clientFd = INVALID_SOCKET;
            }
            ResetCleanUp();
        }
        
        return true;
    }

    bool result = true;

    // Dispatch on current state
    switch (hostConnState)
    {
    case SYN_SENT:
    {
        bool isOk = hostRxHeader.GetCtrl() == ViosCtrl_SYNACK &&
                    hostRxHeader.GetGuestToken() == guestToken;
        if (isOk)
        {
            // Received a SYNACK. This is normal protocol progress.

            // get host's token from SYNACK
            hostToken = hostRxHeader.GetHostToken();
            
            // Construct and send a ACK packet
            hostTxHeader.SetSync();
            hostTxHeader.SetVersion( ViosProtocolVersion );
            hostTxHeader.SetControl( ViosCtrl_ACK);
            hostTxHeader.SetGuestToken ( guestToken );
            hostTxHeader.SetHostToken ( hostToken );
            hostTxHeader.SetPayloadLength ( 0 );
            hostTxHeaderBytecount = 0;
            hostTxBufferBytecount = 0;
            
            SocketRtnStatus rStatus;
            ssize_t nSent;
            
            rStatus = HostFdSend( (void *)&hostTxHeader, ViosHeaderSize, nSent, hostTxHeaderBytecount );
            
            switch (rStatus)
            {
                case SRTN_NORMAL:
                    // Syn has been sent
                    break;
                    
                case SRTN_OK_INCOMPLETE:
                    // Fallthrough
                case SRTN_FULL:
                    // A subset of the bytes needed was sent
                    // Try again next time
                    hostConnTxSubstate = SEND_HEADER;
                    hostTxBufferPending = false;
                    break;
                    
                case SRTN_CLOSED:
                    // FALLTHROUGH
                case SRTN_ERROR:
                    // Error has been logged and socket is closed.
                    // This session is over.
                    return false;
                    break;
            }
            
            // Set next state
            hostConnState = ESTABLISHED;

            std::string ID;
            LOG (INFO, "Transistion to ESTABLISHED: " + pathName + ": " +
            hostTxHeader.GetConnectionId(ID, guestToken, hostToken));

            // Wake up service in poller loop
            isHostFdRead = true;
            isHostFdWrite = true;
        }
        else
        {
            // Anything but a properly addressed SYNACK gets a reset on the host protocol side
            RequestReset ( "SYN_SENT state received bad token or non-SYNACK frame" );

            // And a socket closure on client side.
            close ( clientFd );
            clientFd = INVALID_SOCKET;
        }
        break;
    }
    
    case ESTABLISHED:
    {
        bool isOk = hostRxHeader.GetCtrl() == ViosCtrl_DATA &&
                    hostRxHeader.GetGuestToken() == guestToken &&
                    hostRxHeader.GetHostToken() == hostToken;
        if (isOk)
        {
            // Hoy hoy, a data frame received.
            // In ESTABLISHED state data frames contain raw packets to send to client.
            assert (hostRxHeader.GetPayloadLength() > 0);
            
            // Push the data into the client socket
            hostRxBufferBytecount = 0;
            SocketRtnStatus rStatus = ClientSocketSend ();

            switch (rStatus)
            {
            case SRTN_NORMAL:
                break;

            case SRTN_OK_INCOMPLETE:
                // A subset of the bytes needed were sent
                // Fallthrough
            case SRTN_FULL:
                // None of the bytes sent, but still ok otherwise
                // Go to state that stalls next guest Rx until client accepts
                // last data frame.
                hostConnRxSubstate = MESSAGE_TO_CLIENT;

                // We are not ready for the next Rx frame
                result = false;

                break;

            case SRTN_CLOSED:
                // FALLTHROUGH
            case SRTN_ERROR:
                // Error has been logged and socket is closed.
                // This session is over.
                RequestReset ( "Connection closed by client" );
            }

        }
        else
        {
            // Anything but a properly addressed DATA frame gets a reset
            RequestReset ( "ESTABLISHED state received bad token or non-DATA frame" );
        }
        break;
    }
    default:
    {
        assert (false);
    }
    }

    return result;
}


////////////////////////////////////////////////
//
// HostFdRecv
//
// Issue a read() and then set flags in common manner
// for various return conditions.
//
ViosGChannel::SocketRtnStatus ViosGChannel::HostFdRecv(
    void * bufP,
    const size_t bytesToRead,
    ssize_t& bytesRead,
    int& accumulatedBytes )
{
    assert (INVALID_SOCKET != hostFd);

    SocketRtnStatus result;

    bytesRead = read(hostFd, bufP, bytesToRead);

    if ((size_t)bytesRead == bytesToRead)
    {
        result = SRTN_NORMAL;
    }
    else
    {
        // Now the read completed abnormally.
        // Do a global read cleanup.
        //
        // zero lastError to indicate 'no error'
        lastHostError = 0;

        if (bytesRead > 0)
        {
            // read success but buffer incomplete
            accumulatedBytes += bytesRead;

            isHostFdRead = true;
            isIndHostReadable = false;
            result = SRTN_OK_INCOMPLETE;
        }
        else if (bytesRead == 0)
        {
            // closed 
            LOG (INFO, "Host closed during read: " + pathName);
            
            close ( hostFd );
            hostFd = INVALID_SOCKET;
            if (clientFd != INVALID_SOCKET)
            {
                close (clientFd);
                clientFd = INVALID_SOCKET;
            }
            hostConnState = CLOSED;
            result = SRTN_CLOSED;
        }
        else
        {
            // nRead is < 0. This is nominally a socket error
            // but maybe just EAGAIN
            if (EAGAIN == errno)
            {
                // The socket is just empty.
                // Try again.
                isHostFdRead = true;
                isIndHostReadable = false;
                result = SRTN_EMPTY;
            }
            else
            {
                // Socket error
                LogError (WARN, "Host read error: " + pathName, errno);
                lastHostError = errno;
                close ( hostFd );
                hostFd = INVALID_SOCKET;
                if (clientFd != INVALID_SOCKET)
                {
                    close (clientFd);
                    clientFd = INVALID_SOCKET;
                }
                hostConnState = CLOSED;
                result = SRTN_ERROR;
            }
        }
    }

    return result;
}


////////////////////////////////////////////////
//
// HostFdSend
//
// Issue a write() and then set flags in common manner
// for various return conditions.
//
ViosGChannel::SocketRtnStatus ViosGChannel::HostFdSend(
    void * bufP,
    const size_t bytesToSend,
    ssize_t& bytesSent,
    int& accumulatedBytes )
{
    assert (INVALID_SOCKET != hostFd);

    SocketRtnStatus result;

    bytesSent = write(hostFd, bufP, bytesToSend);

    if ((size_t)bytesSent == bytesToSend)
    {
        result = SRTN_NORMAL;
    }
    else
    {
        // Now the send completed abnormally.
        // Do a global send cleanup.
        //
        // zero lastError to indicate 'no error'
        lastHostError = 0;

        if (bytesSent > 0)
        {
            // write success but buffer incomplete
            accumulatedBytes += bytesSent;

            isHostFdWrite = true;
            isIndHostWriteable = false;
            result = SRTN_OK_INCOMPLETE;
        }
        else if (bytesSent == 0)
        {
            // closed
            LOG (INFO, "Host closed during write: " + pathName);

            close ( hostFd );
            hostFd = INVALID_SOCKET;
            if (clientFd != INVALID_SOCKET)
            {
                close (clientFd);
                clientFd = INVALID_SOCKET;
            }
            hostConnState = CLOSED;
            result = SRTN_CLOSED;
        }
        else
        {
            // nSent is < 0. This is nominally a socket error
            // but maybe just EAGAIN
            if (EAGAIN == errno)
            {
                // The socket is just full.
                // Try again.
                isHostFdWrite = true;
                isIndHostWriteable = false;
                result = SRTN_FULL;
            }
            else
            {
                // Socket error
                LogError (WARN, "Host write error: " + pathName, errno);
                lastHostError = errno;
                close ( hostFd );
                hostFd = INVALID_SOCKET;
                if (clientFd != INVALID_SOCKET)
                {
                    close (clientFd);
                    clientFd = INVALID_SOCKET;
                }
                std::string ID;
                LOG (INFO, "Transistion to CLOSED: " + pathName + ": " +
                hostTxHeader.GetConnectionId(ID, guestToken, hostToken));
                hostConnState = CLOSED;
                result = SRTN_ERROR;
            }
        }
    }

    return result;
}



////////////////////////////////////////////////
//
// ClientSocketRecv
//
// Return data to hostTxBuffer / hostTxBufferBytecount if any.
// Manange socket closure.
//
ViosGChannel::SocketRtnStatus ViosGChannel::ClientSocketRecv( void )
{
    if (INVALID_SOCKET == clientFd)
    {
        // closed
        LOG (DEBUG, "Client socket recv and already closed: " + pathName);
        return SRTN_CLOSED;
    }
    
    ssize_t bytesRead;

    SocketRtnStatus result;

    bytesRead = recv(clientFd, &hostTxBuffer, ViosCtrl_MaxPayloadSize, MSG_DONTWAIT);

    // zero lastError to indicate 'no error'
    lastHostError = 0;

    if (bytesRead > 0)
    {
        // read some bytes
        hostTxBufferBytecount = bytesRead;

        // Turn off client read polling
        isClientFdRead = false;
        isIndClientReadable = false;

        // Turn on host write polling
        isHostFdWrite = true;
        result = SRTN_NORMAL;
    }
    else if (bytesRead == 0)
    {
        // closed
        LOG (INFO, "Client closed during recv: " + pathName);;
        close ( clientFd );
        clientFd = INVALID_SOCKET;
        result = SRTN_CLOSED;
    }
    else
    {
        // nRead is < 0. This is nominally a socket error
        // but maybe just EAGAIN
        if (EAGAIN == errno)
        {
            // The socket is just empty.
            // Try again.
            isClientFdRead = true;
            isIndClientReadable = false;
            result = SRTN_EMPTY;
        }
        else
        {
            // Socket error
            LogError (WARN, "Client error during recv: " + pathName, errno);
            lastHostError = errno;
            close ( clientFd );
            clientFd = INVALID_SOCKET;
            result = SRTN_ERROR;
        }
    }

    return result;
}


////////////////////////////////////////////////
//
// ClientSocketSend
//
// Send the hostRxBuffer to the client
//
ViosGChannel::SocketRtnStatus ViosGChannel::ClientSocketSend( void )
{
    if (INVALID_SOCKET == clientFd)
    {
        // closed
        LOG (DEBUG, "Client socket send and already closed: " + pathName);
        return SRTN_CLOSED;
    }
    
    SocketRtnStatus result;
    ssize_t bytesSent;

    uint8_t * pBuf = (uint8_t *)&hostRxBuffer;
    size_t lenToSend = hostRxHeader.GetPayloadLength();

    // Adjust for bytes sent so far
    pBuf      += hostRxBufferBytecount;
    lenToSend -= hostRxBufferBytecount;

    assert (lenToSend > 0);

    bytesSent = send(clientFd, pBuf, lenToSend, MSG_DONTWAIT);

    if ((size_t)bytesSent == lenToSend)
    {
        result = SRTN_NORMAL;
    }
    else
    {
        // Now the send completed abnormally.
        // Do a global send cleanup.
        //
        // zero lastError to indicate 'no error'
        lastHostError = 0;

        if (bytesSent > 0)
        {
            // write success but buffer incomplete
            hostRxBufferBytecount += bytesSent;

            isClientFdWrite = true;
            isIndClientWriteable = false;
            result = SRTN_OK_INCOMPLETE;
        }
        else if (bytesSent == 0)
        {
            // closed
            LOG (INFO, "Client closed during send: " + pathName);
            close ( clientFd );
            clientFd = INVALID_SOCKET;
            result = SRTN_CLOSED;
        }
        else
        {
            // nSent is < 0. This is nominally a socket error
            // but maybe just EAGAIN
            if (EAGAIN == errno)
            {
                // The socket is just full.
                // Try again.
                isClientFdWrite = true;
                isIndClientWriteable = false;
                result = SRTN_FULL;
            }
            else
            {
                // Socket error
                LogError (WARN, "Client error during send: " + pathName, errno);
                lastHostError = errno;
                close ( clientFd );
                clientFd = INVALID_SOCKET;
                result = SRTN_ERROR;
            }
        }
    }

    return result;
}


//
// ResetCleanup
//
// Reset channel state for a fresh new connection.
//
void ViosGChannel::ResetCleanUp (void)
{
    LOG (DEBUG, "Channel cleanup: " + pathName);
    
    isClientFdRead = true;
    isClientFdWrite = true;
    isIndClientReadable = false;
    isIndClientWriteable = false;
    isIndClientError = false;
    isHostFdRead = false;
    isHostFdWrite = false;
    isIndHostReadable = false;
    isIndHostWriteable = false;
    isIndHostError = false;
    if (CLOSED != hostConnState)
    {
        hostConnState = CLOSED;
        std::string ID;
        LOG (INFO, "Transistion to CLOSED: " + pathName + ": " +
        hostTxHeader.GetConnectionId(ID, guestToken, hostToken));
    }
    hostConnRxSubstate = GET_SYNC0;
    hostConnTxSubstate = SEND_IDLE;
    hostConnResetSubstate = RESET_IDLE;
    hostRxHeaderBytecount = 0;
    hostTxHeaderBytecount = 0;
    hostRxBufferBytecount = 0;
    hostTxBufferBytecount = 0;
    hostTxBufferPending = false;
    guestToken = 0x52525252;     // 'RRRR'
    hostToken  = 0x52525252;     // 'RRRR'
    memset(hostRxBuffer, 0, sizeof(hostRxBuffer));
    memset(hostTxBuffer, 0, sizeof(hostTxBuffer));
}
// kate: indent-mode cstyle; space-indent on; indent-width 0; 

