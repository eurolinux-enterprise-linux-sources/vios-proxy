/*
 *
 * Licensed the Apache Software Foundation (ASF) under one
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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/un.h>

#include "vios_hchannel.h"
#include "vios_hguest.h"
#include "vios_framing.h"
#include "vios_utility.h"

using namespace ViosFraming;
using namespace Utility;

//
//
//
ViosHChannel::ViosHChannel ( const std::string& thePathName,
                             const std::string& theGuestname,
                             const int theServicePort ) :
        pathName ( thePathName ),
        guestName ( theGuestname ),
        servicePort ( theServicePort ),
        isOnProbation ( false ),
        lastError (0),
        guestUDS ( INVALID_SOCKET ),
        isFdRead ( false ),
        isFdWrite ( false ),
        isIndReadable ( false ),
        isIndWriteable ( false ),
        isIndError ( false ),
        serviceSocket ( INVALID_SOCKET ),
        isServiceFdRead ( false ),
        isServiceFdWrite ( false ),
        isServiceIndReadable ( false ),
        isServiceIndWriteable ( false ),
        isServiceIndError ( false ),
        guestConnState ( CLOSED ),
        guestConnRxSubstate ( GET_SYNC0 ),
        guestConnTxSubstate ( SEND_IDLE ),
        guestConnResetSubstate ( RESET_IDLE ),
        guestRxHeader (),
        guestRxHeaderBytecount (0),
        guestTxHeader (),
        guestTxHeaderBytecount (0),
        guestRxBufferBytecount (0),
        guestTxBufferBytecount (0),
        guestTxBufferPending (false),
        guestToken (0x21212121),    // '!!!!'
        hostToken (0x21212121)      // '!!!!'
{
    // zero out the i/o buffers
    memset(guestRxBuffer, 0, sizeof(guestRxBuffer));
    memset(guestTxBuffer, 0, sizeof(guestTxBuffer));

    Reconnect();
}

//
//
//
ViosHChannel::~ViosHChannel()
{
    // dispose of sockets
    if (guestUDS != INVALID_SOCKET)
    {
        LOG (INFO, "Close guest channel: " + pathName);
        close (guestUDS);
        guestUDS = INVALID_SOCKET;
    }

    if (serviceSocket != INVALID_SOCKET)
    {
        LOG (INFO, "Close service channel: " + pathName);
        close (serviceSocket);
        serviceSocket = INVALID_SOCKET;
    }

    LOG (INFO, "Destroy guest channel: " + pathName);
}


//
// Reconnect
//
// Try to open the channel to guest UDS
//
void ViosHChannel::Reconnect ( void )
{
    assert (guestUDS == INVALID_SOCKET);

    // open the uds
    guestUDS = socket(AF_UNIX, SOCK_STREAM, 0);
    if (INVALID_SOCKET == guestUDS)
    {
        LogError ( WARN, "Failed to open guest channel: " + pathName, errno );
        lastError = errno;
        guestConnState = ViosFraming::CLOSED;
    }
    else
    {
        // connect to uds
        struct sockaddr_un remote;
        remote.sun_family = AF_UNIX;
        strncpy(remote.sun_path, pathName.c_str(), sizeof(remote.sun_path) - 1);
        int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        int result = connect(guestUDS, (struct sockaddr *)&remote, len);
        if (SOCKET_ERROR == result)
        {
            LogError ( WARN, "Failed to connect to guest channel: " + pathName, errno );
            lastError = errno;
            guestConnState = ViosFraming::CLOSED;
            close ( guestUDS );
            guestUDS = INVALID_SOCKET;
        }
        else
        {
            // This is the success path
            // set non-blocking
            int opts;

            opts = fcntl(guestUDS, F_GETFL, 0);
            if (opts < 0)
            {
                LogError (ERROR, "fcntl(F_GETFL) on guest channel: " + pathName, errno );
                close (guestUDS);
                guestUDS = INVALID_SOCKET;
            }
            else
            {
                opts |= O_NONBLOCK;
                if (fcntl(guestUDS, F_SETFL, opts) < 0)
                {
                    LogError (ERROR, "fcntl(F_SETFL) on guest channel: " + pathName, errno );
                    close (guestUDS);
                    guestUDS = INVALID_SOCKET;
                }
                else
                {
                    // Set flaggage to indicate new connection
                    ResetCleanUp();
                    
                    // Repeatedly read the guest to drain any junk from
                    // previous sessions.
                    ssize_t bytesRead;
                    do {
                        bytesRead = read (guestUDS, guestRxBuffer, sizeof(guestRxBuffer));
                    } while (bytesRead > 0);
                    
                    // declare success - the socket is open for polling
                    LOG (INFO, "Opened guest channel: " + pathName);
                }
            }
        }
    }
}


//
// OpenServiceSocket
//
// Try to open the channel to the service network socket
//
bool ViosHChannel::OpenServiceSocket ( void )
{
    assert (serviceSocket == INVALID_SOCKET);
    bool result = false;

    serviceSocket = socket (PF_INET, SOCK_STREAM, 0);

    if (INVALID_SOCKET == serviceSocket)
    {
        LogError ( WARN, "Failed to create service channel: " + pathName, errno );
        lastError = errno;
        RequestReset ( "Failed to create service channel" );
    }
    else
    {
        struct sockaddr_in name;

        name.sin_family = AF_INET;
        name.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        name.sin_port = htons( servicePort );

        int bResult = connect (serviceSocket, (struct sockaddr *) &name, sizeof(name));
        if (bResult < 0)
        {
            lastError = errno;
            LogError ( WARN, "Failed to connect to service channel: " + pathName, errno );
            close (serviceSocket);
            serviceSocket = INVALID_SOCKET;
            isServiceFdRead = false;
            isServiceFdWrite = false;
            isServiceIndReadable = false;
            isServiceIndWriteable = false;
            RequestReset ( "Failed to connect service channel" );
        }
        else
        {
            LOG (INFO, "Opened service channel: " + pathName);
            result = true;
        }
    }
    return result;
}



//
// RequestReset
//
// A channel is running and now it should be reset.
// Set things up so a RESET frame gets sent in due order.
// In this case the RESET will be sent to the guestUDS protocol channel.
//
void ViosHChannel::RequestReset ( const std::string&  reason  )
{
    std::string ID;
    LOG (INFO, "Resetting channel: " + pathName
    + ": " + guestRxHeader.GetConnectionId(ID, guestToken, hostToken)
    + ": " + reason);

    guestConnResetSubstate = RESET_REQUESTED;

    isFdWrite = true;

    if (INVALID_SOCKET != serviceSocket)
    {
        LOG (DEBUG, "RequestReset closes service: " + pathName);

        close (serviceSocket);
        serviceSocket = INVALID_SOCKET;
        isServiceFdRead = false;
        isServiceFdWrite = false;
        isServiceIndReadable = false;
        isServiceIndWriteable = false;
    }
}

///////////////////////////////////////////
// RunProtocolTx
//
// Manage stalled transmits
//
void ViosHChannel::RunProtocolTx ( void )
{
    assert (guestUDS != INVALID_SOCKET && isIndWriteable);

    if (guestConnTxSubstate == SEND_HEADER)
    {
        // Send the rest of a stuck header
        uint8_t * pHdr = (uint8_t *)&guestTxHeader;
        size_t lenToSend = ViosHeaderSize;

        // Adjust for bytes sent so far
        pHdr      += guestTxHeaderBytecount;
        lenToSend -= guestTxHeaderBytecount;

        // Send more bytes
        ssize_t nSent;

        SocketRtnStatus rStatus = GuestUdsSend( pHdr, lenToSend, nSent, guestTxHeaderBytecount );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            guestConnTxSubstate = SEND_BUFFER;
            break;

        case SRTN_OK_INCOMPLETE:
            // A subset of the bytes needed was sent
            // FALLTHROUGH
        case SRTN_FULL:
            // socket not accepting any data now
            // Try again later.
            isFdWrite = true;
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            return;
        }
    }

    if (guestConnTxSubstate == SEND_BUFFER)
    {
        if (guestTxBufferPending)
        {
            // Send the rest of a stuck buffer
            uint8_t * pHdr = (uint8_t *)&guestTxBuffer;
            size_t lenToSend = guestTxHeader.GetPayloadLength();

            // Adjust for bytes sent so far
            pHdr      += guestTxBufferBytecount;
            lenToSend -= guestTxBufferBytecount;

            // Send more bytes
            SocketRtnStatus rStatus;
            ssize_t nSent;

            rStatus = GuestUdsSend( pHdr, lenToSend, nSent, guestTxBufferBytecount );

            switch (rStatus)
            {
            case SRTN_NORMAL:
                guestConnTxSubstate = SEND_IDLE;
                guestTxBufferPending = false;
                isFdWrite = true;
                break;

            case SRTN_OK_INCOMPLETE:
                // A subset of the bytes needed was sent
                // FALLTHROUGH
            case SRTN_FULL:
                // socket not accepting any data now
                // fall out of loop. try again later.
                isFdWrite = true;
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
            guestConnTxSubstate = SEND_IDLE;
        }
    }

/*    if (guestConnTxSubstate == SEND_IDLE)
    {
        isFdWrite = false;
    }*/
}


//
// RunRxProtocol
//
// Return true to trigger a recursive-style recall
//
bool ViosHChannel::RunProtocolRx ( void )
{
    //
    // V1 host protocol executor (HOST SIDE)
    //
    // On entry:
    //    isIndXxx has be set true by a callback.
    //    isFdXxx  tells of the socket is in the poller's fd sets.
    assert (guestUDS != INVALID_SOCKET && isIndReadable);

    bool requestRecall = false;

    //
    // Get the sync byte 0
    //
    while (isIndReadable &&
           guestConnRxSubstate == GET_SYNC0 &&
           guestConnResetSubstate == RESET_IDLE)
    {
        // Keep reading bytes until sync0 seen.
        SocketRtnStatus rStatus;
        ssize_t nRead;
        int dummy;

        rStatus = GuestUdsRecv( &guestRxHeader.sync0, 1, nRead, dummy );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            // We read the byte. Check it out.
            if (guestRxHeader.sync0 == ViosProtocolSync0)
            {
                // We have Sync0.
                guestConnRxSubstate = GET_SYNC1;
            }
            else
            {
                // Zero'th byte is not Sync0. C
                // In LISTEN state, consume it and try again.
                // In any other state call for a reset.
                if (guestConnState == LISTEN)
                {
                    requestRecall = true;
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
            return false;

        case SRTN_OK_INCOMPLETE:
            // is degenerate on a one-byte read
            assert (false);
            return false;
        }
    }

    //
    // Get the sync byte 1
    //
    while (isIndReadable && guestConnRxSubstate == GET_SYNC1)
    {
        // read sync1
        SocketRtnStatus rStatus;
        ssize_t nRead;
        int dummy;

        rStatus = GuestUdsRecv( &guestRxHeader.sync1, 1, nRead, dummy );

        switch (rStatus)
        {
        case SRTN_NORMAL:
            // We read the byte. Check it out.
            if (guestRxHeader.sync1 == ViosProtocolSync1)
            {
                // We have Sync1.
                guestConnRxSubstate = GET_HEADER;
                guestRxHeaderBytecount = 2;      // Sync bytes already received.
            }
            else
            {
                // byte is not Sync1. Continue resynching.
                requestRecall = true;

                if (guestRxHeader.sync1 == ViosProtocolSync0)
                {
                    // second byte not sync1 but is another sync0
                    // try getting sync1 again
                }
                else
                {
                    // second byte not sync1 nor synch0
                    // restart resync from the top
                    guestConnRxSubstate = GET_SYNC0;
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
            return false;

        case SRTN_OK_INCOMPLETE:
            // is degenerate on a one-byte read
            assert (false);
            return false;
        }
    }

    //
    // Next accumulate the header and data for a message
    //
    if (guestConnRxSubstate == GET_HEADER)
    {
        //
        // Receiving a header
        //
        if (isIndReadable)
        {
            // Cast the header structure as a series of bytes
            // to be read from socket
            uint8_t * pHdr = (uint8_t *)&guestRxHeader;
            size_t lenToRead = ViosHeaderSize;

            // Adjust for bytes read so far
            pHdr      += guestRxHeaderBytecount;
            lenToRead -= guestRxHeaderBytecount;

            // Read more bytes
            SocketRtnStatus rStatus;
            ssize_t nRead;

            rStatus = GuestUdsRecv( pHdr, lenToRead, nRead, guestRxHeaderBytecount );

            //printf("GET_HEADER reads %d bytes from %s\n", nRead, pathName.c_str());

            bool isOk = false;

            switch (rStatus)
            {
            case SRTN_NORMAL:
                // Normal case (he says) - full header was received.
                // See if the header passes muster
                if (!guestRxHeader.CheckSync())
                {
                    LOG (DEBUG, "Header CheckSync fails: " + pathName);
                }
                else if (guestRxHeader.GetVersion() != ViosProtocolVersion)
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
                    if (guestRxHeader.GetPayloadLength() > 0)
                    {
                        // Set up to receive a payload buffer
                        guestRxBufferBytecount = 0;
                        guestConnRxSubstate = GET_DATA;
                    }
                    else
                    {
                        // Payload length == 0. The header is ready.
                        guestConnRxSubstate = MESSAGE_READY;
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
                return false;
            }
        }
        else
        {
            // We are trying to receive a header but the socket is
            // not readable. Try again later.
            isFdRead = true;
        }
    }

    if (guestConnRxSubstate == GET_DATA)
    {
        //
        // Receiving a data block
        //
        if (isIndReadable)
        {
            // Cast the header structure as a series of bytes
            // to be read from socket
            uint8_t * pHdr = (uint8_t *)&guestRxBuffer;
            size_t lenToRead = guestRxHeader.GetPayloadLength();

            // Adjust for bytes read so far
            pHdr      += guestRxBufferBytecount;
            lenToRead -= guestRxBufferBytecount;

            // Read more bytes
            SocketRtnStatus rStatus;
            ssize_t nRead;

            rStatus = GuestUdsRecv( pHdr, lenToRead, nRead, guestRxBufferBytecount );

            switch (rStatus)
            {
            case SRTN_NORMAL:
                guestConnRxSubstate = MESSAGE_READY;
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
                return false;
            }
        }
        else
        {
            // We are trying to receive a buffer but the socket is
            // not readable. Try again later.
            isFdRead = true;
        }
    }

    if (guestConnRxSubstate == MESSAGE_READY)
    {
        // Done receiving a header or a header and data.
        if (guestConnResetSubstate == RESET_IDLE)
        {
            // The connection is not being reset. 
            // Process the message.
            bool msgHandled = ProcessProtocolMessage ();
            if ( msgHandled )
            {
                // After processing a message, clean up state for next
                guestConnRxSubstate = GET_SYNC0;
                guestRxHeaderBytecount = 0;
                guestRxBufferBytecount = 0;
                isFdRead = true;
                isServiceFdRead = true;
            }
            else
            {
                // A message is unhandled when a received guest data frame
                // is on its way to the service but stuck.
            }
        }
        else
        {
            // The connection is being reset.
            // Ignore this received frame.
        }
    }

    if (guestConnRxSubstate == MESSAGE_TO_SERVICE)
    {
        // This is a stalling state where we don't let the last
        // received message go until it's contents are sent to the service.
        // Unfortunately, this state may arise after the service socket is
        // closed.
        SocketRtnStatus rStatus;
        if (INVALID_SOCKET != serviceSocket)
        {
            // Accept status from an actual send attempt
            rStatus = ServiceSocketSend ();
        }
        else
        {
            // There's no socket to send to.
            // Force success status and fall out of this state.
            rStatus = SRTN_NORMAL;
        }
        
        switch (rStatus)
        {
        case SRTN_NORMAL:
            // After processing a message, clean up state for next
            guestConnRxSubstate = GET_SYNC0;
            guestRxHeaderBytecount = 0;
            guestRxBufferBytecount = 0;
            isFdRead = true;
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
            RequestReset ( "Service closed" );
        }
    }

    return requestRecall;
}


//
// RunProtocol
//
// The guestManager has accepted a callback from select() and
// set our isIndXxx flags.
//
// Now take a pass at what the protocol should do next.
//
void ViosHChannel::RunProtocol ( void )
{
    //LOG (DEBUG, "RunProtocol: " + pathName);

    assert ( SYN_SENT != guestConnState );

    // Guard against closed sessions
    if ( CLOSED  == guestConnState )
    {
        isFdRead = false;
        isFdWrite = false;

        return;
    }
    
    //
    // Top level dispatch
    //
    bool runAgain;
    size_t loopLimit;

    if (guestConnResetSubstate == RESET_IDLE)
    {
        //
        // No reset in progress
        // Run the transmit-to-guest flush one time
        //
        if (guestUDS != INVALID_SOCKET && isIndWriteable)
            RunProtocolTx ();

        //
        // Run the receive-from-guest process in a loop. This is to support
        // initial LISTEN buffer synch processing gobs of junk before a
        // properly framed header.
        //
        for (runAgain = true, loopLimit = 0;
            guestUDS != INVALID_SOCKET &&
            isIndReadable &&
            runAgain &&
            loopLimit < ViosCtrl_MaxPayloadSize &&
            guestConnResetSubstate == RESET_IDLE;
            loopLimit++)
        {
            runAgain = RunProtocolRx ();
        }

        //
        // While ESTABLISHED look for service data to send to guest
        //
        if (guestConnState == ESTABLISHED)
        {
            if (isServiceIndReadable)
                isServiceFdRead = false;

            if (isServiceIndReadable && guestConnTxSubstate == SEND_IDLE)
            {
                // Host is readable and guest Tx pipe is idle.
                // send some host data to guest
                SocketRtnStatus rStatus = ServiceSocketRecv ();

                switch (rStatus)
                {
                case SRTN_NORMAL:
                {
                    // Some bytes were read into the txBuffer
                    assert (guestTxBufferBytecount > 0);

                    uint16_t payloadLen = (uint16_t)guestTxBufferBytecount;

                    // construct a transmit header for the data
                    guestTxHeader.SetSync();
                    guestTxHeader.SetVersion( ViosProtocolVersion );
                    guestTxHeader.SetControl( ViosCtrl_DATA );
                    guestTxHeader.SetGuestToken ( guestToken );
                    guestTxHeader.SetHostToken ( hostToken );
                    guestTxHeader.SetPayloadLength ( payloadLen );
                    guestTxHeaderBytecount = 0;
                    guestTxBufferBytecount = 0;

                    guestConnTxSubstate = SEND_HEADER;
                    guestTxBufferPending = true;

                    // Stop reading the service channel and start sending on guest
                    isServiceFdRead = false;
                    isFdWrite = true;
                    break;
                }

                case SRTN_EMPTY:
                    // no bytes read
                    // Wake up when some arrive
                    isServiceFdRead = true;
                    break;

                case SRTN_CLOSED:
                    // FALLTHROUGH
                case SRTN_ERROR:
                    // Error has been logged and socket is closed.
                    // This session is over.
                    RequestReset ( "Socket closed by service" );
                    break;
                case SRTN_OK_INCOMPLETE:
                    // This can't happen
                    assert (false);
                    break;
                }
            }
        }
    }

    if (guestConnResetSubstate == RESET_REQUESTED)
    {
        //
        // The protocol has decided to close the current connection.
        // Wait for the Tx engine to go idle
        //
        // It's idle, start sending the reset.
        if (guestUDS != INVALID_SOCKET && isIndWriteable)
            RunProtocolTx ();

        // If still sending then come back later
        if (guestConnTxSubstate != SEND_IDLE)
            return;

        // Transmit is idle
        // Wait for Rx engine to go between frames

        if (guestConnRxSubstate != GET_SYNC0)
        {
            for (runAgain = true, loopLimit = 0;
                 guestUDS != INVALID_SOCKET && isIndReadable && runAgain && loopLimit < 10000;
                 loopLimit++)
            {
                runAgain = RunProtocolRx ();
            }
        }

        if (guestConnRxSubstate != GET_SYNC0)
            return;

        // Now both Tx and Rx are between frames.
        // Generate our best Reset frame and send it.
        guestTxHeader.SetSync();
        guestTxHeader.SetVersion( ViosProtocolVersion );
        guestTxHeader.SetControl( ViosCtrl_RESET );
        guestTxHeader.SetGuestToken ( guestToken );
        guestTxHeader.SetHostToken ( hostToken );
        guestTxHeader.SetPayloadLength ( 0 );

        guestTxHeaderBytecount = 0;
        guestTxBufferPending = false;
        
        SocketRtnStatus rStatus;
        ssize_t nSent;

        rStatus = GuestUdsSend( (void *)&guestTxHeader, ViosHeaderSize, nSent, guestTxHeaderBytecount );

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
            guestConnResetSubstate = RESET_SEND_IN_FLIGHT;
            break;

        case SRTN_CLOSED:
            // FALLTHROUGH
        case SRTN_ERROR:
            // Error has been logged and socket is closed.
            // This session is over.
            return;
        }
    }


    if (guestConnResetSubstate ==  RESET_SEND_IN_FLIGHT)
    {
        // A reset frame is in flight
        if (guestConnTxSubstate != SEND_IDLE)
        {
            if (guestUDS != INVALID_SOCKET && isIndWriteable)
                RunProtocolTx ();
        }

        // If still sending then come back later
        if (guestConnTxSubstate != SEND_IDLE)
            return;

        // The guest connection has been reset
        ResetCleanUp();
    }
}


////////////////////////////////////////////////
// ProcessProtocolMessage
//
// A fully framed message has been received from guest UDS.
// Process it.
// Return true if frame processing is complete.
//
bool ViosHChannel::ProcessProtocolMessage (void)
{
    LOG (DEBUG, "ProcessProtocolMessage: " + pathName + ", " +
    to_string(guestRxHeader.GetCtrl()) +
        ", len:" + to_string(guestRxHeader.GetPayloadLength()));
    
    //
    // Respond to received RESET protocol frames
    //
    if (guestRxHeader.GetCtrl() == ViosCtrl_RESET)
    {
        if (LISTEN == guestConnState)
        {
            // In listen state there's no connection to reset.
            // Just swallow the reset.
            LOG (DEBUG, "Ignore a RESET received in LISTEN state: " + pathName);
        }
        else if (SYN_RCVD == guestConnState || ESTABLISHED == guestConnState)
        {
            // In SynRcvd or Established states we accept a reset but do not
            // send one back.
            // For diag purposes make sure this reset belongs to the current session.
            LOG (DEBUG, "RESET received: " + pathName);
            if (guestToken != guestRxHeader.GetGuestToken())
            {
                LOG (DEBUG, "RESET received for wrong session: " + pathName);
            }

            // Process the reset
            ResetCleanUp();

            // Close service connection
            assert (INVALID_SOCKET != serviceSocket);

            close (serviceSocket);
            serviceSocket = INVALID_SOCKET;
        }
        return true;
    }

    bool result = true;

    // Dispatch on current state
    switch (guestConnState)
    {
    case LISTEN:
        // Listen state expects a SYN packet
        if (guestRxHeader.GetCtrl() == ViosCtrl_SYN)
        {
            // A SYN packet. Open a socket to the service
            if ( OpenServiceSocket() )
            {
                // Accept the syn
                guestToken = guestRxHeader.GetGuestToken();

                // Generate a host token
                hostToken = ViosFraming::GenerateToken();

                // Construct and send SynAck
                guestTxHeader.SetSync();
                guestTxHeader.SetVersion( ViosProtocolVersion );
                guestTxHeader.SetControl( ViosCtrl_SYNACK );
                guestTxHeader.SetGuestToken ( guestToken );
                guestTxHeader.SetHostToken ( hostToken );
                guestTxHeader.SetPayloadLength ( 0 );

                guestTxHeaderBytecount = 0;
                guestTxBufferPending = false;

                SocketRtnStatus rStatus;
                ssize_t nSent;

                rStatus = GuestUdsSend( (void *)&guestTxHeader, ViosHeaderSize, nSent, guestTxHeaderBytecount );

                assert (rStatus == SRTN_NORMAL);
                
                // Set next state
                guestConnState = SYN_RCVD;

                std::string ID;
                LOG (INFO, "Transistion to SYN_RCVD: " + pathName + ": " +
                guestTxHeader.GetConnectionId(ID, guestToken, hostToken));
                
                // Don't turn on any service socket isFd select poller switches until data state
            }
            else
            {
                // Failed to open service socket
                RequestReset ( "Failed to open service" );
            }
        }
        else
        {
            // Anything other than SYN gets a reset
            //RequestReset ( "Listen state did not receive SYN frame" );
        }
        break;

    case SYN_RCVD:
    {
        bool isOk = guestRxHeader.GetCtrl() == ViosCtrl_ACK &&
                    guestRxHeader.GetGuestToken() == guestToken &&
                    guestRxHeader.GetHostToken() == hostToken;
        if (isOk)
        {
            // Set next state
            guestConnState = ESTABLISHED;

            std::string ID;
            LOG(INFO, "Transistion to ESTABLISHED: " + pathName + ": " +
            guestTxHeader.GetConnectionId(ID, guestToken, hostToken));

            // Wake up service in poller loop
            isServiceFdRead = true;
            isServiceFdWrite = true;
        }
        else
        {
            // Anything but a properly addressed ACK gets a reset
            RequestReset ( "SYN_RCVD state received bad token or non-ACK frame" );
        }
        break;
    }
    case ESTABLISHED:
    {
        bool isOk = guestRxHeader.GetCtrl() == ViosCtrl_DATA &&
                    guestRxHeader.GetGuestToken() == guestToken &&
                    guestRxHeader.GetHostToken() == hostToken;
        if (isOk)
        {
            // Hoy hoy, a data frame received.
            // In ESTABLISHED state data frames contain raw packets to send to service.
            //printf("Received data in ESTABLISHED state: %d bytes.\n", guestRxHeader.GetPayloadLength());

            // Push the data into the service socket
            guestRxBufferBytecount = 0;
            SocketRtnStatus rStatus = ServiceSocketSend ();

            switch (rStatus)
            {
            case SRTN_NORMAL:
                break;

            case SRTN_OK_INCOMPLETE:
                // A subset of the bytes needed were sent
                // Fallthrough
            case SRTN_FULL:
                // None of the bytes sent, but still ok otherwise
                // Go to state that stalls next guest Rx until service receives
                // last data frame.
                guestConnRxSubstate = MESSAGE_TO_SERVICE;

                // We are not ready for the next Rx frame
                result = false;

                break;

            case SRTN_CLOSED:
                // FALLTHROUGH
            case SRTN_ERROR:
                // Error has been logged and socket is closed.
                // This session is over.
                RequestReset ( "Socket closed by service" );
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
        // This shouldn't happen
        assert (false);
    }
    }

    return result;
}


////////////////////////////////////////////////
//
// GuestUdsRecv
//
// Issue a recv() and then set flags in common manner
// for various return conditions.
//
ViosHChannel::SocketRtnStatus ViosHChannel::GuestUdsRecv(
    void * bufP,
    const size_t bytesToRead,
    ssize_t& bytesRead,
    int& accumulatedBytes )
{
    assert (INVALID_SOCKET != guestUDS);

    SocketRtnStatus result;

    bytesRead = recv(guestUDS, bufP, bytesToRead, MSG_DONTWAIT);

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
        lastError = 0;

        if (bytesRead > 0)
        {
            // read success but buffer incomplete
            accumulatedBytes += bytesRead;

            isFdRead = true;
            isIndReadable = false;
            result = SRTN_OK_INCOMPLETE;
        }
        else if (bytesRead == 0)
        {
            LOG (INFO, "Guest closed during recv: " + pathName);
            
            close ( guestUDS );
            guestUDS = INVALID_SOCKET;
            guestConnState = CLOSED;
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
                isFdRead = true;
                isIndReadable = false;
                result = SRTN_EMPTY;
            }
            else
            {
                // Socket error
                LogError (WARN, "Guest channel error: " + pathName, errno);
                LOG (INFO, "Close guest channel: " + pathName);
                close ( guestUDS );
                guestUDS = INVALID_SOCKET;
                guestConnState = CLOSED;
                result = SRTN_ERROR;
            }
        }
    }

    return result;
}


////////////////////////////////////////////////
//
// GuestUdsSend
//
// Issue a send() and then set flags in common manner
// for various return conditions.
//
ViosHChannel::SocketRtnStatus ViosHChannel::GuestUdsSend(
    void * bufP,
    const size_t bytesToSend,
    ssize_t& bytesSent,
    int& accumulatedBytes )
{
    assert (INVALID_SOCKET != guestUDS);

    SocketRtnStatus result;

    //printf("GuestUdsSend sending %d bytes\n", bytesToSend);
    
    bytesSent = send(guestUDS, bufP, bytesToSend, MSG_DONTWAIT);

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
        lastError = 0;

        if (bytesSent > 0)
        {
            // write success but buffer incomplete
            accumulatedBytes += bytesSent;

            isFdWrite = true;
            isIndWriteable = false;
            result = SRTN_OK_INCOMPLETE;
        }
        else if (bytesSent == 0)
        {
            LOG (INFO, "Close guest channel: " + pathName);

            close ( guestUDS );
            guestUDS = INVALID_SOCKET;
            guestConnState = CLOSED;
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
                isFdWrite = true;
                isIndWriteable = false;
                result = SRTN_FULL;
            }
            else
            {
                // Socket error
                LogError (WARN, "Guest channel send error: " + pathName, errno);
                LOG (INFO, "Close guest channel: " + pathName);
                lastError = errno;
                close ( guestUDS );
                guestUDS = INVALID_SOCKET;
                guestConnState = CLOSED;
                result = SRTN_ERROR;
            }
        }
    }

    return result;
}



////////////////////////////////////////////////
//
// ServiceSocketRecv
//
// Return data to guestTxBuffer / guestTxBufferBytecount if any.
// Manange socket closure.
//
ViosHChannel::SocketRtnStatus ViosHChannel::ServiceSocketRecv( void )
{
    if (INVALID_SOCKET == serviceSocket)
    {
        // closed
        LOG (INFO, "Service recv closed:" + pathName);
        return SRTN_CLOSED;
    }
    
    ssize_t bytesRead;

    SocketRtnStatus result;

    bytesRead = recv(serviceSocket, &guestTxBuffer, ViosCtrl_MaxPayloadSize, MSG_DONTWAIT);

    // zero lastError to indicate 'no error'
    lastError = 0;

    if (bytesRead > 0)
    {
        // read some bytes
        guestTxBufferBytecount = bytesRead;

        // Turn off service read polling
        isServiceFdRead = false;
        isServiceIndReadable = false;

        // Turn on guest write polling
        isFdWrite = true;
        result = SRTN_NORMAL;
    }
    else if (bytesRead == 0)
    {
        // closed
        LOG (INFO, "Service closed during recv: " + pathName);
        close (serviceSocket);
        serviceSocket = INVALID_SOCKET;
        isServiceFdRead = false;
        isServiceFdWrite = false;
        isServiceIndReadable = false;
        isServiceIndWriteable = false;
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
            isServiceFdRead = true;
            isServiceIndReadable = false;
            result = SRTN_EMPTY;
        }
        else
        {
            // Socket error
            LogError (WARN, "Service error during recv: " + pathName, errno);
            lastError = errno;
            close (serviceSocket);
            serviceSocket = INVALID_SOCKET;
            isServiceFdRead = false;
            isServiceFdWrite = false;
            isServiceIndReadable = false;
            isServiceIndWriteable = false;
            result = SRTN_ERROR;
        }
    }

    return result;
}


////////////////////////////////////////////////
//
// ServiceSocketSend
//
// Send the guestRxBuffer to the service
//
ViosHChannel::SocketRtnStatus ViosHChannel::ServiceSocketSend( void )
{
    if (INVALID_SOCKET == serviceSocket)
    {
        // closed
        LOG (INFO, "Service send closed:" + pathName);
        return SRTN_CLOSED;
    }
    
    SocketRtnStatus result;
    ssize_t bytesSent;

    uint8_t * pBuf = (uint8_t *)&guestRxBuffer;
    size_t lenToSend = guestRxHeader.GetPayloadLength();

    // Adjust for bytes sent so far
    pBuf      += guestRxBufferBytecount;
    lenToSend -= guestRxBufferBytecount;

    if (lenToSend <= 0)
    {
        LOG (WARN, "LenToSend <= 0" + pathName);
    }
    assert (lenToSend > 0);

    bytesSent = send(serviceSocket, pBuf, lenToSend, MSG_DONTWAIT);

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
        lastError = 0;

        if (bytesSent > 0)
        {
            // write success but buffer incomplete
            guestRxBufferBytecount += bytesSent;

            isServiceFdWrite = true;
            isServiceIndWriteable = false;
            result = SRTN_OK_INCOMPLETE;
        }
        else if (bytesSent == 0)
        {
            // closed
            LOG (INFO, "Service closed during send: " + pathName);
            close (serviceSocket);
            serviceSocket = INVALID_SOCKET;
            isServiceFdRead = false;
            isServiceFdWrite = false;
            isServiceIndReadable = false;
            isServiceIndWriteable = false;
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

                isServiceFdWrite = true;
                isServiceIndWriteable = false;
                result = SRTN_FULL;
            }
            else
            {
                // Socket error
                LogError (WARN, "Service error during send: " + pathName, errno);
                lastError = errno;
                close (serviceSocket);
                serviceSocket = INVALID_SOCKET;
                isServiceFdRead = false;
                isServiceFdWrite = false;
                isServiceIndReadable = false;
                isServiceIndWriteable = false;
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
void ViosHChannel::ResetCleanUp (void)
{
    LOG (DEBUG, "Channel cleanup: " + pathName);
    
    isFdRead = true;
    isFdWrite = true;
    isIndReadable = false;
    isIndWriteable = false;
    isIndError = false;
    isServiceFdRead = false;
    isServiceFdWrite = false;
    isServiceIndReadable = false;
    isServiceIndWriteable = false;
    isServiceIndError = false;
    if (LISTEN != guestConnState)
    {
        guestConnState = LISTEN;
        LOG (INFO, "Transition to LISTEN: " + pathName);
    }
    guestConnRxSubstate = GET_SYNC0;
    guestConnTxSubstate = SEND_IDLE;
    guestConnResetSubstate = RESET_IDLE;
    guestRxHeaderBytecount = 0;
    guestTxHeaderBytecount = 0;
    guestRxBufferBytecount = 0;
    guestTxBufferBytecount = 0;
    guestTxBufferPending = false;
    guestToken = 0x52525252;     // 'RRRR'
    hostToken  = 0x52525252;     // 'RRRR'
    memset(guestRxBuffer, 0, sizeof(guestRxBuffer));
    memset(guestTxBuffer, 0, sizeof(guestTxBuffer));
}
// kate: indent-mode cstyle; space-indent on; indent-width 0; 
