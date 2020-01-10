#ifndef VIOS_HGUEST_H
#define VIOS_HGUEST_H

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
#include <string>
#include <vector>
#include <netinet/in.h>
#include <sys/poll.h>
#include "boost/smart_ptr.hpp"

class ViosHGuestManager;
class ViosHChannel;

typedef boost::shared_ptr< ViosHGuestManager > hGuestManagerPtr;

//
// ViosHGuest
//
class ViosHGuest
{
public:
    friend class ViosHGuestManager;

    // constructor
    ViosHGuest ( const std::string & thePathName,
                 const int theServicePort
    );

    // destructor
    ~ViosHGuest();

    // Access function : set probation status
    void setProbation ( bool onProbation ) { isOnProbation = onProbation; };

    // Access function : get probation status
    bool getProbation ( void )             { return isOnProbation; };

    // Access function : get the configured path name
    const std::string& getPathName ( void ){ return pathName; };

    // Scan guest directory pool for addition/removal of guest connections.
    // On reconnect indication try to reconnect sockets that previously failed.
    void EnumerateGuestChannels ( bool reconnect );

    // Set connection timeout period in seconds
    void SetConnectTimeout ( int seconds ) { connTimeout = seconds; };

private:
    // The directory path for this guest
    std::string pathName;

    // The port number for the service we proxy to clients.
    int servicePort;
    
    // For detecting when this guest has been deleted.
    bool isOnProbation;

    // Connection timeout tick count
    int connTimeout;
};


//
// ViosHGuestManager
//
class ViosHGuestManager
{
public:
    friend class ViosHGuest;
    
    ViosHGuestManager ( const std::string& thePathName,
                        const int& theServicePort
    );
    ~ViosHGuestManager();

    // Scan guest directory pool for addition/removal of guest connections.
    // On reconnect indication try to reconnect sockets that previously failed.
    void EnumerateGuestDirectories ( bool reconnect );

    // Debug printing for all the guests
    void PrintGuestFacts ( void );

    // Poller interface: execute one select cycle using given wait time
    bool ViosHGuestPoller ( long int waitTimeUSec );

    // Poller interface: execute select cycles until one second has elapsed
    void ViosHGuestPollOneSecond ( void );

    // Shared pointer to a guest.
    typedef boost::shared_ptr< ViosHGuest > hGuestPtr;
    
    //
    // guestStore
    //   index: guest full path name
    //   value: shared pointer to hGuest
    static std::map< std::string, hGuestPtr > guestStore;

    // Shared pointer to a channel
    typedef boost::shared_ptr< ViosHChannel > hChannelPtr;
    
    //
    // channelStore
    //   index: channel full path name
    //   value: shared pointer to channel
    static std::map< std::string, hChannelPtr > channelStore;
    
    // Access function to return the service port number 0..65535
    const int getServicePort ( void ) { return servicePort; }

    // Shutdown function to destroy guests and connections to them
    void ViosHGuestDestroyGuests ( void );

private:

    // The path that holds directories, each representing guests.
    std::string pathName;

    // The port number for the service we proxy to clients.
    int servicePort;

    // type def for pollfd
    typedef struct pollfd pollfd_t;
    
    // vector of pollfd's for the poll() interface
    std::vector< pollfd_t > pollFds;
    
    // Add a channel's FDs to the poll FD vector at given index.
    // Vector is extended if need be.
    // Index is advanced per FD added.
    void ViosHGuestAddChannelFds (nfds_t & theIndex, hChannelPtr pChan);

    boost::shared_ptr <ViosHGuestManager> gGuestManager;
};

#endif
