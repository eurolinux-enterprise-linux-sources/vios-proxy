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

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <signal.h>

#include "vios_hguest.h"
#include "vios_hchannel.h"
#include "vios_utility.h"

using namespace Utility;

//////////////////////////////////////////////////////
// ViosHGuest
//
// constructor
//
ViosHGuest::ViosHGuest ( const std::string & thePathName,
                         const int theServicePort ) :
        pathName ( thePathName ),
        servicePort ( theServicePort ),
        isOnProbation ( false )
{
}


//
// destructor
//
ViosHGuest::~ViosHGuest()
{
    LOG (INFO, "Destroy guest: " + pathName);

    // Delete the guest's channels as guest is being destroyed
    std::map< std::string, ViosHGuestManager::hChannelPtr >::iterator itr;
    std::list<std::string> deleteList;
    
    for (itr = ViosHGuestManager::channelStore.begin();
         itr != ViosHGuestManager::channelStore.end();
         itr++)
    {
        if (itr->second->getGuestName() == pathName)
        {
            deleteList.push_back(itr->first);
        }
    }

    for (std::list<std::string>::const_iterator dIter = deleteList.begin(); dIter != deleteList.end(); dIter++)
        ViosHGuestManager::channelStore.erase(*dIter);
}


//
//
//
void ViosHGuest::EnumerateGuestChannels ( bool reconnect )
{
    //
    // EnumerateGuestChannels
    //
    // Scans a guest directory for sockets.
    // Each socket represents a guest connection.
    //
    struct stat stDirInfo;
    struct dirent * stFiles;
    DIR * stDirIn;
    char szFullName[MAXPATHLEN];
    struct stat stFileInfo;
    std::map< std::string, ViosHGuestManager::hChannelPtr >::iterator itr;

    //
    // Put current channels on probation
    //
    for (itr = ViosHGuestManager::channelStore.begin();
         itr != ViosHGuestManager::channelStore.end();
         itr++)
    {
        if (itr->second->getGuestName() == pathName)
        {
            itr->second->setProbation ( true );
        }
    }

    //
    // Stat the guest directory.
    //
    if ( lstat ( pathName.c_str(), &stDirInfo ) < 0 )
    {
        LogError (PANIC, "Error accessing guest path: " + pathName, errno);
        exit ( EXIT_FAILURE );
    }

    //
    // It can't be a file
    //
    if ( !S_ISDIR ( stDirInfo.st_mode ) )
    {
        LOG (PANIC, "Guest path is not a directory: " + pathName );
        exit ( EXIT_FAILURE );
    }

    //
    // Open the DIR*
    //
    if ( ( stDirIn = opendir ( pathName.c_str() ) ) == NULL )
    {
        LOG (PANIC, "Guest path open error: " + pathName );
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
        sprintf ( szFullName, "%s/%s", pathName.c_str(), stFiles -> d_name );
        std::string tmpName(szFullName);
        
        if ( lstat ( szFullName, &stFileInfo ) >= 0 )
        {
            // Determine if this is a domain socket
            if (! S_ISDIR  ( stFileInfo.st_mode ) &&
                ! S_ISCHR  ( stFileInfo.st_mode ) &&
                ! S_ISBLK  ( stFileInfo.st_mode ) &&
                ! S_ISREG  ( stFileInfo.st_mode ) &&
                ! S_ISFIFO ( stFileInfo.st_mode ) &&
                ! S_ISLNK  ( stFileInfo.st_mode ) &&
                  S_ISSOCK ( stFileInfo.st_mode ) )
            {
                //
                // Maintain list of channels
                //
                std::map< std::string, ViosHGuestManager::hChannelPtr >::iterator pChannel =
                    ViosHGuestManager::channelStore.find(tmpName);
                if ( pChannel == ViosHGuestManager::channelStore.end())
                {
                    // Name not found. Create new channel and add to list.
                    LOG (INFO, "Create guest channel: " + tmpName);
                    ViosHGuestManager::hChannelPtr newChannel (
                        new ViosHChannel ( szFullName, pathName, servicePort ) );
                    ViosHGuestManager::channelStore[tmpName] = newChannel;
                }
                else
                {
                    // Found an existing guest. Take off probation.
                    pChannel->second->setProbation ( false );

                    // Check if this channel needs a reconnect
                    if (reconnect)
                    {
                        if (INVALID_SOCKET == pChannel->second->getSocket())
                        {
                            pChannel->second->Reconnect();
                        }
                    }
                }
            }
            else
            {
                // cruft in guest dir that is not a socket. ignore it.
                LOG (DEBUG, "Path in guest directory is not a usable guest endpoint: "
                + tmpName);
            }
        }
        else
        {
            // Unable to stat this pathname
            LogError (WARN, "Unable to stat possible guest endpoint: " + tmpName, errno);
        }
    }  // end while

    //
    // Terminate directory walk
    //
    closedir ( stDirIn );

    //
    // Delete channels still on probation after directory walk
    // 
    std::list<std::string> deleteList;
    
    for (itr = ViosHGuestManager::channelStore.begin();
         itr != ViosHGuestManager::channelStore.end();
         itr++)
    {
        if (itr->second->getGuestName() == pathName)
        {
            if (itr->second->getProbation())
            {
                LOG (INFO, "Delete guest channel: " + itr->second->getPathName());
                deleteList.push_back(itr->first);
            }
        }
    }

    for (std::list<std::string>::const_iterator dIter = deleteList.begin(); dIter != deleteList.end(); dIter++)
        ViosHGuestManager::channelStore.erase(*dIter);
}


//////////////////////////////////////////////////////
// ViosHGuestManager
//
ViosHGuestManager::ViosHGuestManager ( const std::string& thePathName,
                                       const int& theServicePort
) :
        pathName ( thePathName ),
        servicePort ( theServicePort )
{
    gGuestManager = boost::shared_ptr<ViosHGuestManager>(this);
}



ViosHGuestManager::~ViosHGuestManager()
{
}

void ViosHGuestManager::ViosHGuestDestroyGuests ( void )
{
    guestStore.clear();
    channelStore.clear();
}

//
// EnumerateGuestDirectories
//
// Scans lowest root path.
//  Directories in root path represent guests.
//   Sockets in guest directories represent vios-proxy channels to that guest.
// On reconnect indication try to reconnect sockets that previously failed.
//
void ViosHGuestManager::EnumerateGuestDirectories ( bool reconnect )
{
    struct stat stDirInfo;
    struct dirent * stFiles;
    DIR * stDirIn;
    char szFullName[MAXPATHLEN];
    struct stat stFileInfo;
    std::map< std::string, boost::shared_ptr<ViosHGuest> >::iterator itr;
    
    //
    // Put current guests on probation
    //
    for ( itr = guestStore.begin();
          itr != guestStore.end();
          ++itr )
    {
        itr->second->setProbation ( true );
    }

    //
    // Stat the directory
    //
    if ( lstat ( pathName.c_str(), &stDirInfo ) < 0 )
    {
        LogError (WARN, "Error accessing guest root path: " + pathName, errno);
    }
    else
    {

        //
        // It can't be a file
        //
        if ( !S_ISDIR ( stDirInfo.st_mode ) )
        {
            LOG (WARN, "Guest root path is not a directory: " + pathName );
        }
        else
        {
            //
            // Open the DIR*
            //
            if ( ( stDirIn = opendir ( pathName.c_str() ) ) == NULL )
            {
                LOG (WARN, "Guest root path open error: " + pathName );
            }
            else
            {
                //
                // Scan the DIR*
                //
                while ( ( stFiles = readdir ( stDirIn ) ) != NULL )
                {
                    // Don't proces '.' and '..'. Seen this before?
                    if ( strncmp ( ".", stFiles->d_name, 3 ) == 0 )
                        continue;
                    if ( strncmp ( "..", stFiles->d_name, 4 ) == 0 )
                        continue;

                    // Construct pathname and 'stat' it
                    sprintf ( szFullName, "%s/%s", pathName.c_str(), stFiles -> d_name );
                    std::string tmpName(szFullName);

                    if ( lstat ( szFullName, &stFileInfo ) < 0 )
                    {
                        LOG (WARN, "Unable to 'stat' guest root entry " + tmpName);
                    }
                    else
                    {
                        // Determine if this is a guest directory
                        if ( S_ISDIR ( stFileInfo.st_mode ) &&
                            ! S_ISCHR ( stFileInfo.st_mode )  &&
                            ! S_ISBLK ( stFileInfo.st_mode ) &&
                            ! S_ISREG ( stFileInfo.st_mode ) &&
                            ! S_ISFIFO ( stFileInfo.st_mode ) &&
                            ! S_ISLNK ( stFileInfo.st_mode ) &&
                            ! S_ISSOCK ( stFileInfo.st_mode ) )
                        {
                            //
                            // Maintain list of guests
                            //
                            std::map< std::string, hGuestPtr >::iterator pGuest = guestStore.find(tmpName);
                            if (pGuest == guestStore.end())
                            {
                                // Name not found. Create new guest and add to guest store.
                                LOG (INFO, "Create guest: " + tmpName);
                                hGuestPtr newGuest ( new ViosHGuest( tmpName, servicePort ) );
                                guestStore[tmpName] = newGuest;
                            }
                            else
                            {
                                // Found an existing guest. Take off probation.
                                pGuest->second->setProbation ( false );
                            }
                        }
                        else
                        {
                            // cruft in root path that is not a 'guest directory'. ignore it.
                        }
                    }
                }  // end while

                //
                // Terminate directory walk
                //
                closedir ( stDirIn );
            }
        }
    }
    
    //
    // Delete guests still on probation
    //
    std::list<std::string> deleteList;
    
    for (itr = guestStore.begin(); itr != guestStore.end(); itr++)
    {
        if ( itr->second->getProbation() )
        {
            LOG (INFO, "Delete guest: " + itr->first );
            deleteList.push_back(itr->first);
        }
    }

    for (std::list<std::string>::const_iterator dIter = deleteList.begin(); dIter != deleteList.end(); dIter++)
        ViosHGuestManager::guestStore.erase ( *dIter );
    
    //
    // Scan guests for channels
    //
    for ( itr = guestStore.begin();
          itr != guestStore.end();
        ++itr )
     {
         itr->second->EnumerateGuestChannels( reconnect );
     }
}


//
// ViosHGuestAddChannelFds
//
// Add a channel's FDs to the poll FD vector at given index.
// Vector is extended if need be.
//
void ViosHGuestManager::ViosHGuestAddChannelFds (nfds_t & theIndex,
                                                hChannelPtr pChan)
{
    struct pollfd  newPollFd = {0};
    
    // selectors for client-side
    SOCKET thisSock = pChan->getSocket();
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
        
        if (pChan->isFdRead)
        {
            pollFds[theIndex].events |= (POLLIN | POLLPRI);
        }
        if (pChan->isFdWrite)
        {
            pollFds[theIndex].events |= POLLOUT;
            pChan->isFdWrite = false;
        }
        
        theIndex += 1;
    }
    
    // selectors for host-side
    // Note that the host-side may still have reset-in-flight activity
    // after the client-side socket is closed.
    thisSock = pChan->getServiceSocket();
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
        
        if (pChan->isServiceFdRead)
        {
            pollFds[theIndex].events |= (POLLIN | POLLPRI);
        }
        if (pChan->isServiceFdWrite)
        {
            pollFds[theIndex].events |= POLLOUT;
            pChan->isServiceFdWrite = false;
        }
        
        theIndex += 1;
    }
}


///////////////////////////////////////////////
// HGuestPoller
//
// Create FDsets for all sockets (guest UDS and host TCP),
// Issue a select with zero timeout.
// Process all channels that need attention.
// Return true to keep running, false if SIGINT seen.
//
bool ViosHGuestManager::ViosHGuestPoller ( long int waitTimeUSec )
{
    nfds_t         nFds = 0;
    
    int            waitTime_mS = waitTimeUSec / 1000;
    

    std::map< std::string, boost::shared_ptr< ViosHChannel > >::iterator itr;
    for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
    {
        // selectors for guest-side UDS
        hChannelPtr pChan = itr->second;
        ViosHGuestAddChannelFds ( nFds, pChan );
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
        nFds = 0;
        
        // Hunt for indicated FD values and make callbacks
        for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
        {
            hChannelPtr pChan = itr->second;
            bool        doRun = false;
            SOCKET      thisSock;
            
            thisSock = pChan->getSocket();
            if (thisSock != INVALID_SOCKET)
            {
                assert (pollFds[nFds].fd == thisSock);
                
                if ((pollFds[nFds].revents & (POLLIN | POLLPRI)) != 0)
                {
                    doRun = pChan->isIndReadable = true;
                }
                if ((pollFds[nFds].revents & POLLOUT) != 0)
                {
                    doRun = pChan->isIndWriteable = true;
                }
                if ((pollFds[nFds].revents & (POLLERR | POLLHUP)) != 0)
                {
                    doRun = pChan->isIndError = true;
                }
                nFds += 1;
            }

            thisSock = pChan->getServiceSocket();
            if (thisSock != INVALID_SOCKET)
            {
                assert (pollFds[nFds].fd == thisSock);
                
                if ((pollFds[nFds].revents & (POLLIN | POLLPRI)) != 0)
                {
                    doRun = pChan->isServiceIndReadable = true;
                }
                if ((pollFds[nFds].revents & POLLOUT) != 0)
                {
                    doRun = pChan->isServiceIndWriteable = true;
                }
                if ((pollFds[nFds].revents & (POLLERR | POLLHUP)) != 0)
                {
                    doRun = pChan->isServiceIndError = true;
                }
                nFds += 1;
            }
            
            if (doRun)
            {
                LOG (DEBUG, "Poll entry: " + pChan->getPathName() +
                ", isFdR:" + to_string(pChan->isFdRead) +
                ", isFdW:" + to_string(pChan->isFdWrite) +
                ", isIndR:" + to_string(pChan->isIndReadable) +
                ", isIndW:" + to_string(pChan->isIndWriteable));
                LOG (DEBUG, "Poll entry: " + pChan->getPathName() +
                ", isSerFdR:" + to_string(pChan->isServiceFdRead) +
                ", isserFdW:" + to_string(pChan->isServiceFdWrite) +
                ", isSerIndR:" + to_string(pChan->isServiceIndReadable) +
                ", isSerIndW:" + to_string(pChan->isServiceIndWriteable));
                
                pChan->RunProtocol();

                LOG (DEBUG, "Poll exit: " + pChan->getPathName() +
                ", isFdR:" + to_string(pChan->isFdRead) +
                ", isFdW:" + to_string(pChan->isFdWrite) +
                ", isIndR:" + to_string(pChan->isIndReadable) +
                ", isIndW:" + to_string(pChan->isIndWriteable));
                LOG (DEBUG, "Poll exit: " + pChan->getPathName() +
                ", isSerFdR:" + to_string(pChan->isServiceFdRead) +
                ", isserFdW:" + to_string(pChan->isServiceFdWrite) +
                ", isSerIndR:" + to_string(pChan->isServiceIndReadable) +
                ", isSerIndW:" + to_string(pChan->isServiceIndWriteable));
                LOG (DEBUG, "-------------------------------------------");
            }
        }
    }
    return true;
}


//
//
//
bool HasTimeElapsed(struct timeval& cTime, struct timeval& eTime)
{
    return cTime.tv_sec >  eTime.tv_sec ||
          (cTime.tv_sec == eTime.tv_sec && cTime.tv_usec >= eTime.tv_usec);
}


//
// PollOneSecond
//
void ViosHGuestManager::ViosHGuestManager::ViosHGuestPollOneSecond ( void )
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
        if (!ViosHGuestPoller ( waitUSec ))
            return;

        // re-read current time
        gettimeofday (&curTime, NULL);
    }
}


//
// Instantiate static members
//
std::map< std::string, ViosHGuestManager::hGuestPtr > ViosHGuestManager::guestStore;
std::map< std::string, ViosHGuestManager::hChannelPtr > ViosHGuestManager::channelStore;
