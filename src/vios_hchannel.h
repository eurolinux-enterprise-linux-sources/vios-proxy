#ifndef VIOS_HCHANNEL_H
#define VIOS_HCHANNEL_H

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
#include "vios_hguest.h"

class ViosHChannel
{
public:
    // constructor
    ViosHChannel ( const std::string& thePathName,
                   const std::string& theGuestname,
                   const int theServicePort );

    // destructor
    ~ViosHChannel();

    // accessor method - set probation flag
    void setProbation ( bool onProbation )   { isOnProbation = onProbation; }

    // accessor method - get probation flag
    bool getProbation ( void )               { return isOnProbation; }

    // accessor method - get channel path name
    const std::string& getPathName ( void )  { return pathName; }

    // accessor method - get name of guest that own this channel
    const std::string& getGuestName ( void ) { return guestName; }

    // accessor method - get guest socket
    const SOCKET getSocket ( void )          { return guestUDS; }

    // accessor method - get service socket
    const SOCKET getServiceSocket ( void )    { return serviceSocket; }

private:
    // Try to open guest UDS
    void Reconnect ( void );

    // Open connection to our service host
    bool OpenServiceSocket ( void );

    // Initiate a RESET sequence with our guest protocol peer
    void RequestReset ( const std::string& reason );

    // Run the protocol transmit side
    void RunProtocolTx ( void );

    // Run the protocol receive side
    bool RunProtocolRx ( void );

public:
    // On demand from poller, run the protocol
    void RunProtocol ( void );

    friend class ViosHGuestManager;
    friend class ViosHGuest;

    // Vios protocol Connection execution receiver substates
    enum ConnectionRxSubstate
    {
        GET_SYNC0,
        GET_SYNC1,
        GET_HEADER,
        GET_DATA,
        MESSAGE_READY,
        MESSAGE_TO_SERVICE
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

    // path name of the guest UDS
    std::string pathName;

    // path name of the guest that owns this UDS
    std::string guestName;

    // The port number for the service we proxy to clients.
    int servicePort;

    // probation flag
    bool isOnProbation;

    // The UDS may exist but we can't open it for
    // some reason and that reason is stored here.
    int lastError;
    
    //
    // Guest-side UDS (Unix domain socket)
    //
    SOCKET guestUDS;

    // poller/select - include guestUDS in read fdset
    bool isFdRead;
    // poller/select - include guestUDS in write fdset
    bool isFdWrite;

    // poller/select - select indicates guestUDS is readable
    bool isIndReadable;
    // poller/select - select indicates guestUDS is writeable
    bool isIndWriteable;
    // poller/select - select indicates guestUDS is in error
    bool isIndError;

    //
    // Service-side socket
    //
    SOCKET serviceSocket;
    
    // poller/select - include serviceSocket in read fdset
    bool isServiceFdRead;
    // poller/select - include serviceSocket in write fdset
    bool isServiceFdWrite;

    // poller/select - select indicates serviceSocket is readable
    bool isServiceIndReadable;
    // poller/select - select indicates serviceSocket is wirteable
    bool isServiceIndWriteable;
    // poller/select - select indicates serviceSocket is in error
    bool isServiceIndError;

    // Protocol state - connection lifetime state
    ViosFraming::ConnectionState guestConnState;
    // Protocol state - receiver substate
    ConnectionRxSubstate guestConnRxSubstate;
    // Protocol state - transmitter substate
    ConnectionTxSubstate guestConnTxSubstate;
    // Protocol state - RESET coordinator substate
    ConnectionResetSubstate guestConnResetSubstate;

    // Protocol header received from guest
    ViosFraming::ViosHeader guestRxHeader;
    // index/count for assembling incomplete received headers
    int guestRxHeaderBytecount;

    // Protocol header transmitted to guest
    ViosFraming::ViosHeader guestTxHeader;
    // index/count for transmitting incompletely sent headers
    int guestTxHeaderBytecount;

    // Received payload from guest to be sent to service
    uint8_t guestRxBuffer[65536];
    // index/count for transmitting incompletely sent buffers
    int guestRxBufferBytecount;

    // Received payload from service to be sent to guest
    uint8_t guestTxBuffer[65536];
    // index/count for transmitting incompletely sent buffers
    int guestTxBufferBytecount;
    // indication that buffer needs to be sent to guest
    bool guestTxBufferPending;

    // Protocol token generated by guest for current connection
    uint32_t guestToken;
    // Protocol token generated by host for current connection
    uint32_t hostToken;

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

    // Receive some bytes from the guest UDS.
    // Manages poller flags and error cleanup.
    SocketRtnStatus GuestUdsRecv(
        void * bufP,
        const size_t bytesToRead,
        ssize_t& bytesRead,
        int& accumulatedBytes);

    // Transmit some bytes to the guest UDS.
    // Manages poller flags and error cleanup.
    SocketRtnStatus GuestUdsSend(
        void * bufP,
        const size_t bytesToSend,
        ssize_t& bytesSent,
        int& accumulatedBytes);

    // Receive some bytes from the service socket.
    // Manages poller flags and error cleanup.
    SocketRtnStatus ServiceSocketRecv( void );
    
    // Transmit some bytes to the service socket.
    // Manages poller flags and error cleanup.
    SocketRtnStatus ServiceSocketSend( void );

    // Clean up channel variables. Typically useful after a Reset has been
    // sent or received.
    void ResetCleanUp (void);
};

#endif
