#ifndef VIOS_GHOST_H
#define VIOS_GHOST_H

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
#include <netinet/in.h>
#include <sys/poll.h>

#include "boost/smart_ptr.hpp"

#include "vios_gchannel.h"

//
// ViosGHostanager
//
class ViosGHostManager
{
public:
    // constructor
    ViosGHostManager ( const std::string& theRootDir,
                       const int theListenSocket,
                       const int theTimeoutTickCount
                     );
    // destructor
    ~ViosGHostManager();

    // Shutdown function to destroy host channels
    void ViosGHostDestroyChannels ( void );
    
    // Scan host directory pool for addition/removal of host connections.
    void EnumerateHostDirectories ( bool reconnect );

    // Poller interface: execute one select cycle using given wait time
    bool ViosGHostPoller ( long int waitTimeUSec );

    // Poller interface: execute select cycles until one second has elapsed
    void ViosGHostPollOneSecond ( void );
    
    // Timeout control:
    // Host manager notifies each connection of one timeout period.
    void ViosGHostClockTick ( void );
    
    // Shared pointer to a channel
    typedef boost::shared_ptr< ViosGChannel > hChannelPtr;
    
    //
    // channelStore
    //   index: channel full path name
    //   value: shared pointer to channel
    static std::map< std::string, hChannelPtr > channelStore;

    // Set connection timeout period in seconds
    void SetConnectTimeout ( int seconds ) { connTimeout = seconds; };
    
private:

    // The path name of the to-host file descriptor that this client owns.
    // This is the channel to the host service from this client.
    std::string rootPath;

    // The port number on which we listen for connections from our clients.
    int listenPort;

    // Socket on which we listen for connections from our clients
    SOCKET listeningSocket;

    // Listening socket address on which we do accepts
    struct sockaddr_in sockAddr;
    // length of sockAddr
    socklen_t          sockAddrLen;
    // static constant value of 1
    int                sockValueOne;

    // Connection timeout tick count
    int connTimeout;

    // type def for pollfd
    typedef struct pollfd pollfd_t;
    
    // vector of pollfd's for the poll() interface
    std::vector< pollfd_t > pollFds;
    
    // Add a channel's FDs to the poll FD vector at given index.
    // Vector is extended if need be.
    // Index is advanced per FD added.
    void ViosGHostAddChannelFds (nfds_t & theIndex, hChannelPtr pChan);

    // Function called by Poller when listening socket is readable.
    // This function creates new connections as listening socket accept()
    // calls are processed.
    bool DoAccept (void);
};

#endif
