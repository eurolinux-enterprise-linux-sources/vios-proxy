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
#include <list>
#include <map>
#include <vector>
// 
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <signal.h>

#include "vios_ghost.h"
#include "vios_gchannel.h"
#include "vios_framing.h"
#include "vios_utility.h"

using namespace Utility;

//////////////////////////////////////////////////////
// ViosGHostManager
//   On the Guest, this class tracks a Host
//
// constructor
//
ViosGHostManager::ViosGHostManager ( const std::string& theRootDir,
                                     const int theListenSocket,
                                     const int theTimeoutTickCount
                                   ) :
        rootPath ( theRootDir ),
        listenPort ( theListenSocket ),
        listeningSocket ( INVALID_SOCKET ),
        sockAddrLen ( sizeof(sockAddr) ),
        sockValueOne ( 1 )
        
{
    // Allocate a socket to be the proxy's served LISTENING socket
    listeningSocket = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if (listeningSocket == INVALID_SOCKET)
    {
        LogError (PANIC, "Failed to create proxy service: ", errno);
        exit(EXIT_FAILURE);
    }

    // Set options
    setsockopt ( listeningSocket,
                SOL_SOCKET,
                SO_REUSEADDR,
                &sockValueOne,
                sizeof(sockValueOne));

    // Set nonblocking
    int opts;

    opts = fcntl(listeningSocket, F_GETFL);
    if (opts < 0)
    {
        LogError (PANIC, "Proxy service fcntl(F_GETFL): ", errno);
        exit(EXIT_FAILURE);
    }
    opts |= O_NONBLOCK;
    if (fcntl(listeningSocket, F_SETFL, opts) < 0)
    {
        LogError (PANIC, "Proxy service fcntl(F_SETFL): ", errno);
        exit(EXIT_FAILURE);
    }
    
    // Bind to the port
    sockAddr.sin_family      = AF_INET;
    sockAddr.sin_port        = htons( (short)(listenPort) );
    sockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int bindRes = bind ( listeningSocket,
                         (struct sockaddr *)&sockAddr,
                         sockAddrLen );
    if (bindRes != 0)
    {
        LogError (PANIC, "Proxy service bind: ", errno);
        exit(EXIT_FAILURE);
    }

    // Start listening
    int listenResult = listen ( listeningSocket, 0 );
    if (listenResult != 0)
    {
        LogError (PANIC, "Proxy service listen: ", errno);
        exit(EXIT_FAILURE);
    }
}


//
// destructor
//
ViosGHostManager::~ViosGHostManager()
{
}


//
//
//
void ViosGHostManager::ViosGHostDestroyChannels()
{
    channelStore.clear();
}


//
// EnumerateHostDirectories
//
// Scans lowest root path. Files therein represent host channel connection endpoints.
//
void ViosGHostManager::EnumerateHostDirectories ( bool reconnect )
{
    struct stat stDirInfo;
    struct dirent * stFiles;
    DIR * stDirIn;
    char szFullName[MAXPATHLEN];
    struct stat stFileInfo;
    std::map< std::string, boost::shared_ptr< ViosGChannel > >::iterator itr;
    
    //
    // Put current hosts on probation
    //
    for ( itr = channelStore.begin();
          itr != channelStore.end();
          ++itr )
    {
        itr->second->setProbation ( true );
    }

    //
    // Stat the directory
    //
    if ( lstat ( rootPath.c_str(), &stDirInfo ) < 0 )
    {
        LogError (PANIC, "Root path stat fail: " + rootPath, errno );
        exit ( EXIT_FAILURE );
    }

    //
    // It can't be a file
    //
    if ( !S_ISDIR ( stDirInfo.st_mode ) )
    {
        LOG (PANIC, "Root path is not a directory: " + rootPath);
        exit ( EXIT_FAILURE );
    }

    //
    // Open the DIR*
    //
    if ( ( stDirIn = opendir ( rootPath.c_str() ) ) == NULL )
    {
        LogError (PANIC, "Root path open: " + rootPath, errno );
        exit ( EXIT_FAILURE );
    }

    //
    // Scan the DIR*
    //
    while ( ( stFiles = readdir ( stDirIn ) ) != NULL )
    {
        // Don't proces '.' and '..'.
        if ( strncmp ( ".", stFiles->d_name, 3 ) == 0 )
            continue;
        if ( strncmp ( "..", stFiles->d_name, 4 ) == 0 )
            continue;

        // Construct pathname and 'stat' it
        sprintf ( szFullName, "%s/%s", rootPath.c_str(), stFiles -> d_name );
        std::string tmpName(szFullName);
        
        if ( lstat ( szFullName, &stFileInfo ) >= 0 )
        {
            // Determine if this is a host channel endpoint
            // On a real client the channels' file handles are all
            // symbolic links.
            // TODO: Detect actual chardev file handles
    //        if (! S_ISDIR  ( stFileInfo.st_mode ) &&
    //            ! S_ISCHR  ( stFileInfo.st_mode ) &&
    //            ! S_ISBLK  ( stFileInfo.st_mode ) &&
    //              S_ISREG  ( stFileInfo.st_mode ) &&
    //            ! S_ISFIFO ( stFileInfo.st_mode ) &&
    //            ! S_ISLNK  ( stFileInfo.st_mode ) &&
    //            ! S_ISSOCK ( stFileInfo.st_mode ) )
            if (true)
            {
                //
                // Maintain list of host channels
                //
                std::map< std::string, hChannelPtr >::iterator pHost = channelStore.find(tmpName);
                if (pHost == channelStore.end())
                {
                    // Name not found. Create new host and add to list.
                    LOG (INFO, "Create host channel: " + tmpName );
                    hChannelPtr newHost (new ViosGChannel( tmpName , connTimeout ) );
                    channelStore[tmpName] = newHost;
                }
                else
                {
                    // Found an existing guest. Take off probation.
                    pHost->second->setProbation ( false );

                    //
                    // Check if this channel needs a reconnect.
                    // Connections to the host FD will fail if the host proxy
                    // is not running. All we can do is keep trying.
                    //
                    if (reconnect)
                    {
                        if (INVALID_SOCKET == pHost->second->getHostFd())
                        {
                            pHost->second->Reconnect();
                        }
                    }
                }
            }
            else
            {
                // cruft in root path that is not a 'host channel'. ignore it
                LOG (DEBUG, "Path in host directory is not a usable host endpoint: "
                + tmpName);
            }
        }
        else
        {
            // Unable to stat this pathname
            LogError (WARN, "Unable to stat possible host endpoint: " + tmpName, errno);
        }
    }

    //
    // Terminate directory walk
    //
    closedir ( stDirIn );

    //
    // Delete hosts still on probation
    //

    std::list<std::string> deleteList;
    
    for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
    {
        if ( itr->second->getProbation() )
        {
            LOG (INFO, "Delete host: " + itr->second->getPathName() );
            deleteList.push_back(itr->first);
        }
    }

    for (std::list<std::string>::const_iterator dIter = deleteList.begin(); dIter != deleteList.end(); dIter++)
        ViosGHostManager::channelStore.erase(*dIter);
}


//
// ViosGHostAddChannelFds
//
// Add a channel's FDs to the poll FD vector at given index.
// Vector is extended if need be.
void ViosGHostManager::ViosGHostAddChannelFds (nfds_t & theIndex,
                                               hChannelPtr pChan)
{
    struct pollfd  newPollFd = {0};
    
    // selectors for client-side
    SOCKET thisSock = pChan->getClientFd();
    if (thisSock != INVALID_SOCKET)
    {
        // make room for pollfd in vector
        if (pollFds.size() == theIndex)
        {
            pollFds.push_back( newPollFd );
        }
        
        pollFds[theIndex].fd      = thisSock;
        pollFds[theIndex].events  = 0;
        pollFds[theIndex].revents = 0;
        
        if (pChan->isClientFdRead)
        {
            pollFds[theIndex].events |= (POLLIN | POLLPRI);
        }
        if (pChan->isClientFdWrite)
        {
            pollFds[theIndex].events |= POLLOUT;
            pChan->isClientFdWrite = false;
        }
        
        theIndex += 1;
    }
    
    // selectors for host-side
    // Note that the host-side may still have reset-in-flight activity
    // after the client-side socket is closed.
    thisSock = pChan->getHostFd();
    if (thisSock != INVALID_SOCKET)
    {
        // make room for pollfd in vector
        if (pollFds.size() == theIndex)
        {
            pollFds.push_back( newPollFd );
        }
        
        pollFds[theIndex].fd      = thisSock;
        pollFds[theIndex].events  = 0;
        pollFds[theIndex].revents = 0;
        
        if (pChan->isHostFdRead)
        {
            pollFds[theIndex].events |= (POLLIN | POLLPRI);
        }
        if (pChan->isHostFdWrite)
        {
            pollFds[theIndex].events |= POLLOUT;
            pChan->isHostFdWrite = false;
        }

        theIndex += 1;
    }
}


///////////////////////////////////////////////
// GHostPoller
//
// Create FDsets for all sockets (host UDS and host TCP),
// Issue a select with zero timeout.
// Process all channels that need attention.
//
bool ViosGHostManager::ViosGHostPoller ( long int waitTimeUSec )
{
    struct pollfd  newPollFd;
    nfds_t         nFds;

    int            waitTime_mS = waitTimeUSec / 1000;

    // Put the listening socket into the FDsets at [0]
    assert (INVALID_SOCKET != listeningSocket);

    if (pollFds.size() == 0)
    {
        pollFds.push_back ( newPollFd );
    }
    pollFds[0].fd      = listeningSocket;
    pollFds[0].events  = POLLIN;
    pollFds[0].revents = 0;

    nFds = 1;

    // Insert all important connection FDs
    std::map< std::string, boost::shared_ptr< ViosGChannel > >::iterator itr;
    for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
    {
        // Track active connections
        hChannelPtr pChan = itr->second;
        if (pChan->getConnState() != ViosFraming::CLOSED)
        {
            ViosGHostAddChannelFds ( nFds, pChan );
        }
    }

    int pollResult = poll( &pollFds[0], nFds, waitTime_mS);

    if (pollResult < 0)
    {
        // log a panic if not SIGINT, which is a normal exit signal.
        if (SIGILL != errno)
        {
            LogError (PANIC, "poll(): " + to_string(errno), errno);
        }
        return false;
    }

    if (pollResult > 0)
    {
        // Set scan index
        nFds = 1;

        // Hunt for indicated FD values and make callbacks
        for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
        {
            hChannelPtr pChan = itr->second;
            bool doRun = false;

            if (pChan->getConnState() != ViosFraming::CLOSED)
            {
                SOCKET thisSock;

                thisSock = pChan->getClientFd();
                if (thisSock != INVALID_SOCKET)
                {
                    assert (pollFds[nFds].fd == thisSock);

                    if ((pollFds[nFds].revents & (POLLIN | POLLPRI)) != 0)
                    {
                        doRun = pChan->isIndClientReadable = true;
                    }
                    if ((pollFds[nFds].revents & POLLOUT) != 0)
                    {
                        doRun = pChan->isIndClientWriteable = true;
                    }
                    if ((pollFds[nFds].revents & (POLLERR | POLLHUP)) != 0)
                    {
                        doRun = pChan->isIndClientError = true;
                    }
                    nFds += 1;
                }

                thisSock = pChan->getHostFd();
                if (thisSock != INVALID_SOCKET)
                {
                    assert (pollFds[nFds].fd == thisSock);

                    if ((pollFds[nFds].revents & (POLLIN | POLLPRI)) != 0)
                    {
                        doRun = pChan->isIndHostReadable = true;
                    }
                    if ((pollFds[nFds].revents & POLLOUT) != 0)
                    {
                        doRun = pChan->isIndHostWriteable = true;
                    }
                    if ((pollFds[nFds].revents & (POLLERR | POLLHUP)) != 0)
                    {
                        doRun = pChan->isIndHostError = true;
                    }
                    nFds += 1;
                }

                if (doRun)
                {
                    LOG (DEBUG, "Poll entry: " + pChan->getPathName() +
                    ", isCFdR:" + to_string(pChan->isClientFdRead) +
                    ", isCFdW:" + to_string(pChan->isClientFdWrite) +
                    ", isCIndR:" + to_string(pChan->isIndClientReadable) +
                    ", isCIndW:" + to_string(pChan->isIndClientWriteable));
                    LOG (DEBUG, "Poll entry: " + pChan->getPathName() +
                    ", isHFdR:" + to_string(pChan->isHostFdRead) +
                    ", isHFdW:" + to_string(pChan->isHostFdWrite) +
                    ", isHIndR:" + to_string(pChan->isIndHostReadable) +
                    ", isHIndW:" + to_string(pChan->isIndHostWriteable));

                    pChan->RunProtocol();

                    LOG (DEBUG, "Poll exit: " + pChan->getPathName() +
                    ", isCFdR:" + to_string(pChan->isClientFdRead) +
                    ", isCFdW:" + to_string(pChan->isClientFdWrite) +
                    ", isCIndR:" + to_string(pChan->isIndClientReadable) +
                    ", isCIndW:" + to_string(pChan->isIndClientWriteable));
                    LOG (DEBUG, "Poll exit: " + pChan->getPathName() +
                    ", isHFdR:" + to_string(pChan->isHostFdRead) +
                    ", isHFdW:" + to_string(pChan->isHostFdWrite) +
                    ", isHIndR:" + to_string(pChan->isIndHostReadable) +
                    ", isHIndW:" + to_string(pChan->isIndHostWriteable));
                    LOG (DEBUG, "-------------------------------------------");
                }
            }
        }

        // Process listen socket
        if ((pollFds[0].revents & POLLIN) != 0)
        {
            DoAccept();
        }
    }
    return true;
}


//
// HasTimeElapsed
// Compares two struct timeval time values.
// Return true if currentTime >= endTime.
//
bool HasTimeElapsed(struct timeval& cTime, struct timeval& eTime)
{
    return cTime.tv_sec >  eTime.tv_sec ||
    (cTime.tv_sec == eTime.tv_sec && cTime.tv_usec >= eTime.tv_usec);
}


//
// PollOneSecond
//
void ViosGHostManager::ViosGHostPollOneSecond ( void )
{
    struct timeval curTime = {0};
    struct timeval endTime = {0};
    
    // compute end time.
    gettimeofday (&endTime, NULL);
    endTime.tv_sec += 1;
    
    // read current time
    gettimeofday (&curTime, NULL);
    
    // loop until one second has elapsed
    while (!HasTimeElapsed(curTime, endTime))
    {
        // compute how long the poller select should wait
        long int waitUSec = endTime.tv_usec - curTime.tv_usec;
        if (waitUSec < 0)
            waitUSec += 1000000;
        
        assert (waitUSec >= 0 && waitUSec <= 1000000);
        
        // Run the poll loop
        if (!ViosGHostPoller ( waitUSec ))
            return;
        
        // re-read current time
        gettimeofday (&curTime, NULL);
    }
}


//
// ClockTick
//
// Approximately once per second this notification is called.
// Prevent channels from getting stuck waiting forever.
//
void ViosGHostManager::ViosGHostClockTick (void)
{
    std::map< std::string, boost::shared_ptr< ViosGChannel > >::iterator itr;
    for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
    {
        itr->second->ClockTick();
    }
}


//
// DoAccept
//
// The listening socket is now readable (or error).
// Try to accept() on it and assign a channel.
//
bool ViosGHostManager::DoAccept (void)
{
    LOG (INFO, "Accepting guest connection");

    // accept the socket
    SOCKET proposedSocket = accept (listeningSocket,
                                    (struct sockaddr *)&sockAddr,
                                    &sockAddrLen);
    {
        if (proposedSocket < 0)
        {
            LogError (ERROR, "Accept failed", errno);
            return false;
        }
    }

    // Find a channel and then launch this connection
    std::map< std::string, boost::shared_ptr< ViosGChannel > >::iterator itr;
    bool connectionStarted = false;
    
    for ( itr = channelStore.begin();
          itr != channelStore.end();
          ++itr )
    {
        if (itr->second->getClientFd() == INVALID_SOCKET)
        {
            connectionStarted = itr->second->StartConnection( proposedSocket );
            if (connectionStarted)
            {
                break;
            }
        }
    }

    // Close the proposed socket if no connections were available
    if (!connectionStarted)
    {
        LOG (INFO, "Connection rejected - all connections busy");
        close ( proposedSocket );
    }

    return connectionStarted;
}

//
// Instantiate static members
//
std::map< std::string, ViosGHostManager::hChannelPtr > ViosGHostManager::channelStore;
