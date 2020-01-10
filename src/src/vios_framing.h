#ifndef VIOS_FRAMING_H
#define VIOS_FRAMING_H

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
#include <stdint.h>
#include <netinet/in.h>

#if defined __linux__
typedef int SOCKET;
#   define INVALID_SOCKET -1
#   define SOCKET_ERROR   -1
#endif

namespace ViosFraming
{
//
// Sync pattern
//
static const uint8_t ViosProtocolSync0 = 'V';
static const uint8_t ViosProtocolSync1 = 'S';

//
// Current protocol version
//
static const uint8_t ViosProtocolVersion = '1';

//
// Vios protocol Control byte values
//
static const uint8_t ViosCtrl_SYN    = 'A';  // 0x41
static const uint8_t ViosCtrl_ACK    = 'B';  // 0x42
static const uint8_t ViosCtrl_SYNACK = 'C';  // 0x43
static const uint8_t ViosCtrl_DATA   = 'D';  // 0x44
static const uint8_t ViosCtrl_RESET  = 'H';  // 0x48

//
// Version 1 payload size
//
static const size_t ViosCtrl_PayloadBufferSize = 65536;
static const size_t ViosCtrl_MaxPayloadSize    = 65535;

// Set packing as these are 'on the wire' structures
#   pragma pack(push, 1)

// Header structure for each transmitted frame
struct ViosHeader
{
    //
    // The Vios Header for framing messages across virtioserial.
    //  Note: The tokens and length are sent, received, and stored
    //        in network byte order.
    //        The accessors translate them to host byte order.
    //
    uint8_t sync0;          // 'V'
    uint8_t sync1;          // 'S'
    uint8_t version;        // '1' (that's an ascii 'one', 0x31)
    uint8_t ctrl;           // header type protocol control byte

    uint32_t guestToken;    // negotiated from guest
    uint32_t hostToken;     // negotiated from host
    uint16_t payloadLength; // length in bytes of optional payload

    //
    // Receiver decoding
    //
    bool CheckSync()
    {
        return ( sync0 == ViosProtocolSync0 ) && ( sync1 == ViosProtocolSync1 );
    }

    int GetVersion()
    {
        return version;
    }

    int GetCtrl()
    {
        return ctrl;
    }

    uint32_t GetGuestToken()
    {
        return ntohl ( guestToken );
    }

    uint32_t GetHostToken()
    {
        return ntohl ( hostToken );
    }

    uint16_t GetPayloadLength()
    {
        return ntohs ( payloadLength );
    }


    // accessor method - given a string to manipulate and the tokens,
    //                   return a connection ID in the given string
    std::string& GetConnectionId ( std::string& retVal,
                                   uint32_t guestToken,
                                   uint32_t hostToken);


    //
    // Sender encoding
    //
    void SetSync()
    {
        sync0 = ViosProtocolSync0;
        sync1 = ViosProtocolSync1;
    }

    void SetVersion ( uint8_t ver )
    {
        version = ver;
    }

    void SetControl ( uint8_t control )
    {
        ctrl = control;
    }

    void SetGuestToken ( uint32_t gToken )
    {
        guestToken = htonl ( gToken );
    }

    void SetHostToken ( uint32_t hToken )
    {
        hostToken = htonl ( hToken );
    }

    void SetPayloadLength ( uint16_t pLen )
    {
        payloadLength = htons ( pLen );
    }
};
#   pragma pack(pop)

//
// Header size
//
static const size_t ViosHeaderSize = sizeof ( ViosHeader );

//
// Vios protocol Connection State values
//
enum ConnectionState
{
    CLOSED,
    LISTEN,
    SYN_RCVD,
    SYN_SENT,
    ESTABLISHED
};

//
// Token Generator
//
uint32_t GenerateToken ( void );

void GenerateTokenSetSeed ( uint32_t seed );

}    // namespace ViosFraming

#endif
// kate: indent-mode cstyle; space-indent on; indent-width 4; 
