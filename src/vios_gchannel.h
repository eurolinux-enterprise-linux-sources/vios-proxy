#ifndef VIOS_GCHANNEL_H
#define VIOS_GCHANNEL_H

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

#include <string>

#include "vios_framing.h"

class ViosGHostManager;

class ViosGChannel
{
public:
    // constructor
    ViosGChannel ( const std::string& thePathName,
                   const int theTimeoutTickCount );

    // destructor
    ~ViosGChannel();

    // accessor method - set probation flag
    void setProbation ( bool onProbation )   { isOnProbation = onProbation; }

    // accessor method - get probation flag
    bool getProbation ( void )               { return isOnProbation; }

    // accessor method - get channel path name
    const std::string& getPathName ( void )  { return pathName; }

    // accessor method - get accepted client socket fd
    const SOCKET getClientFd ( void )        { return clientFd; }

    // accessor method - get host endpoint file handle fd
    const SOCKET getHostFd ( void )          { return hostFd; }

    // accessor method - get protocol connection state
    const ViosFraming::ConnectionState getConnState ( void )
                                             { return hostConnState; }
    
private:
    // Try to open host file
    void Reconnect ( void );

    // Initiate a RESET sequence with our host protocol peer
    void RequestReset ( const std::string& reason );

    // Run the protocol transmit side
    void RunProtocolTx ( void );

    // Run the protocol receive side
    void RunProtocolRx ( void );
    
public:
    // On demand from poller, run the protocol
    void RunProtocol ( void );

    // After accepting a connection from a client,
    // open our file fd connection to the host and start the protocol on it.
    bool StartConnection (SOCKET clientSocket);

    // Called from host manager to indicate a timer tick
    void ClockTick ( void );
    
    friend class ViosGHostManager;

    // Vios protocol Connection execution receiver substates
    enum ConnectionRxSubstate
    {
        GET_SYNC0,
        GET_SYNC1,
        GET_HEADER,
        GET_DATA,
        MESSAGE_READY,
        MESSAGE_TO_CLIENT
    };
    
    // Vios protocol Connection execution transmitter substates
    enum ConnectionTxSubstate
    {
        SEND_IDLE,
        SEND_HEADER,
        SEND_BUFFER
    };
    
    // Vios protocol Connection execution RESET substates
    enum ConnectionResetSubstate
    {
        RESET_IDLE,
        RESET_REQUESTED,
        RESET_SEND_IN_FLIGHT
    };

private:

    // path name of the host file 
    std::string pathName;

    // probation flag
    bool isOnProbation;

    // The Host File FD may exist but we can't open it for
    // some reason and that reason is stored here.
    int lastHostError;
    
    //
    // Client-side net socket
    //
    SOCKET clientFd;
    
    // poller/select - include clientFd in read fdset
    bool isClientFdRead;
    // poller/select - include clientFd in write fdset
    bool isClientFdWrite;
    
    // poller/select - select indicates clientFd is readable
    bool isIndClientReadable;
    // poller/select - select indicates clientFd is writeable
    bool isIndClientWriteable;
    // poller/select - select indicates clientFd is in error
    bool isIndClientError;

    //
    // Service-side socket controls
    //
    SOCKET hostFd;
    
    // poller/select - include hostFd in read fdset
    bool isHostFdRead;
    // poller/select - include hostFd in write fdset
    bool isHostFdWrite;

    // poller/select - select indicates hostFd is readable
    bool isIndHostReadable;
    // poller/select - select indicates hostFd is writeable
    bool isIndHostWriteable;
    // poller/select - select indicates hostFd is in error
    bool isIndHostError;
    
    // Protocol state - connection lifetime state
    ViosFraming::ConnectionState hostConnState;
    // Protocol state - receiver substate
    ConnectionRxSubstate hostConnRxSubstate;
    // Protocol state - transmitter substate
    ConnectionTxSubstate hostConnTxSubstate;
    // Protocol state - RESET coordinator substate
    ConnectionResetSubstate hostConnResetSubstate;

    // Protocol header received from host
    ViosFraming::ViosHeader hostRxHeader;
    // index/count for assembling incomplete received headers
    int hostRxHeaderBytecount;

    // Protocol header transmitted to host
    ViosFraming::ViosHeader hostTxHeader;
    // index/count for transmitting incompletely sent headers
    int hostTxHeaderBytecount;

    // Received payload from host to be sent to guest
    uint8_t hostRxBuffer[65536];
    // index/count for transmitting incompletely sent buffers
    int hostRxBufferBytecount;

    // Received payload from guest to be sent to host
    uint8_t hostTxBuffer[65536];
    // index/count for transmitting incompletely sent buffers
    int hostTxBufferBytecount;
    // indication that buffer needs to be sent to guest
    bool hostTxBufferPending;

    // Protocol token generated by guest for current connection
    uint32_t guestToken;
    // Protocol token generated by host for current connection
    uint32_t hostToken;

    // Protocol timeout tick counter
    uint32_t timeoutTicks;

    // Protocol timeout tick count admin setting
    uint32_t timeoutTickCount;
    
    // Common socket read/write routine return status
    enum SocketRtnStatus
    {
        SRTN_NORMAL,
        SRTN_OK_INCOMPLETE,
        SRTN_CLOSED,
        SRTN_EMPTY,
        SRTN_FULL = SRTN_EMPTY,
        SRTN_ERROR
    };

    // Process a protocol message, either a header or a header and a buffer.
    bool ProcessProtocolMessage ( void );
    
    // Receive some bytes from the host file.
    // Manages poller flags and error cleanup.
    SocketRtnStatus HostFdRecv(
        void * bufP,
        const size_t bytesToRead,
        ssize_t& bytesRead,
        int& accumulatedBytes);

    // Transmit some bytes to the host file.
    // Manages poller flags and error cleanup.
    SocketRtnStatus HostFdSend(
        void * bufP,
        const size_t bytesToSend,
        ssize_t& bytesSent,
        int& accumulatedBytes);

    // Receive some bytes from the client socket.
    // Manages poller flags and error cleanup.
    SocketRtnStatus ClientSocketRecv( void );
    
    // Transmit some bytes to the client socket.
    // Manages poller flags and error cleanup.
    SocketRtnStatus ClientSocketSend( void );

    // Clean up channel variables. Typically useful after a Reset has been
    // sent or received.
    void ResetCleanUp (void);
};
#endif
