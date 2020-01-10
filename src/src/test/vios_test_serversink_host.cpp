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

//
// vios_test_serversink_client and vios_test_serversink_host are a pair of
// executables that simply blast data across the link between them.
// The client controls how much is sent. The host accepts connections on a
// network port and run multiple sessions.
// They both print some stats for each connection/session.
//

#include <iostream>
#include <map>

#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#include <boost/algorithm/string.hpp>
#include "boost/smart_ptr.hpp"

#include "vios_test_common.h"
#include "../vios_utility.h"

using namespace Utility;

// For checking the data
bool verifyBytes = false;

// The port on the guest localhost that this tunnel offers to clients.
int          listenPort = TEST_PORT;

//
// Exit flag
//
int g_keepRunning = 1;

//
// Signal handler that resets the Exit flag to cause termination
//
void signal_handler (int /* signum */)
{
    g_keepRunning = 0;
}

//
// Usage
//
void Usage (std::string argv0)
{
    std::cout
    << "usage: " << argv0 << " [service_port [verify_switch]]" << std::endl
    << std::endl
    << " service_port - the service port on localhost that is proxied to the guests." << std::endl
    << "                Default = " << listenPort << std::endl
    << " verify_switch- check a data pattern or not." << std::endl
    << "                Default = " << verifyBytes << std::endl;
}


//
// More Globals
//

// Listening socket address on which we do accepts
struct sockaddr_in sockAddr;
// length of sockAddr
socklen_t          sockAddrLen = sizeof(sockAddr);
// static constant value of 1
int                sockValueOne = 1;

SOCKET       listeningSocket = INVALID_SOCKET;
int          lastError = 0;
std::string  pathName = "test port";

//
// The SinkChannel is a class that represents a connection.
// Multiple channel connections are handled simultaneously.
//
class SinkChannel
{
public:
    SinkChannel (SOCKET theSock);
    ~SinkChannel ();

    struct timeval startTime;
    struct timeval endTime;

    uint64_t readsWithData;
    uint64_t totalBytes;
    uint64_t eAgains;
    uint8_t  nextByte;
    
    int nErrorsToReport; // Count down to 0 then stop
    
    SOCKET socket;
};

SinkChannel::SinkChannel (SOCKET theSock) :
    readsWithData (0),
    totalBytes (0),
    eAgains (0),
    nextByte (0),
    nErrorsToReport (10),
    socket (theSock)
{
    gettimeofday (&startTime, 0);
    gettimeofday (&endTime, 0);
    endTime.tv_usec += 1;
}

SinkChannel::~SinkChannel (void)
{
}

//
// Pointers to and storage for channels
//
typedef boost::shared_ptr< SinkChannel > SinkChannelPtr;
std::map< SOCKET, SinkChannelPtr > channelStore;


//
// ConnectListeningSocket
//
void ConnectListeningSocket ( void )
{
    listeningSocket = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if (listeningSocket == INVALID_SOCKET)
    {
        LogError (PANIC, "Failed to create proxy service socket: ", errno);
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
        LogError (PANIC, "Proxy service socket fcntl(F_GETFL): ", errno);
        exit(EXIT_FAILURE);
    }
    opts |= O_NONBLOCK;
    if (fcntl(listeningSocket, F_SETFL, opts) < 0)
    {
        LogError (PANIC, "Proxy service socket fcntl(F_SETFL): ", errno);
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
        LogError (PANIC, "Proxy service socket bind: ", errno);
        exit(EXIT_FAILURE);
    }
    
    // Start listening
    int listenResult = listen ( listeningSocket, 0 );
    if (listenResult != 0)
    {
        LogError (PANIC, "Proxy service port listen: ", errno);
        exit(EXIT_FAILURE);
    }
}


//
// DoAccept
//
// The listening socket is now readable (or error).
// Try to accept() on it and assign a channel.
//
bool DoAccept (void)
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

    // Add this channel to list of channels in the store
    LOG (INFO, "Create new channel: " + to_string(proposedSocket));
    
    SinkChannelPtr newChannel (new SinkChannel(proposedSocket) );
    channelStore [proposedSocket] = newChannel;
    
    return true;
}


//
// PrintStats
//
void PrintStats (SinkChannelPtr theChannel)
{
    gettimeofday (&theChannel->endTime, 0);

    int elapsed_S = theChannel->endTime.tv_sec - theChannel->startTime.tv_sec;
    int elapsed_uS = theChannel->endTime.tv_usec - theChannel->startTime.tv_usec;
    
    int elapsed_uTime = elapsed_S * 1000000 + elapsed_uS;

    std::cout << "==================" << std::endl;
    std::cout << "Channel close " <<  to_string(theChannel->socket) << std::endl;
    std::cout << "Bytes sent   = " << to_string(theChannel->totalBytes) << std::endl;
    std::cout << "Elapsed uSec = " << to_string(elapsed_uTime) << std::endl;
    
    double bytesPerSec = (double) theChannel->totalBytes / (double) elapsed_uTime ;
    bytesPerSec *= 1000000.0;
    
    std::cout << "Bytes/sec    = " << to_string(bytesPerSec) << std::endl;
    std::cout << "ReadsWithData= " << to_string(theChannel->readsWithData) << std::endl;
    std::cout << "eAgains      = " << to_string(theChannel->eAgains) << std::endl;
    std::cout.flush();
}


//
// Sink Data
//
// A channel is readable, read like crazy.
// Handle errors and closure.
// Exit when the channel data has been drained.
//
void SinkData (SOCKET theSocket, SinkChannelPtr theChannel)
{
    ssize_t bytesRead;
#define SINK_SIZE 100000
    unsigned char buffer[SINK_SIZE];

    while (true)
    {
        bytesRead = recv(theSocket, &buffer, SINK_SIZE, MSG_DONTWAIT);

        if (bytesRead > 0)
        {
            if (verifyBytes)
            {
                for (int i=0; i<bytesRead; i++)
                {
                    uint8_t expected = theChannel->nextByte++;
                    if (buffer[i] != expected)
                    {
                        LOG (ERROR, "Verify error: actual: " + to_string((int)buffer[i]) +
                        ", expected: " + to_string((int)expected) + ", at offset: " +
                        to_string(theChannel->totalBytes + i));
                        if (theChannel->nErrorsToReport-- == 0)
                        {
                            LOG (PANIC, "Too many verify errors");
                            exit (EXIT_FAILURE);
                        }
                    }
                }
            }
            
            theChannel->readsWithData += 1;
            theChannel->totalBytes    += bytesRead;
            //LOG (INFO, "Channel Reads: " + to_string(bytesRead) +
            //    ", new total: " + to_string(theChannel->totalBytes));
            
        }
        else if (bytesRead == 0)
        {
            // closed
            LOG (INFO, "Client socket closed during recv: " + to_string(theSocket));;
            PrintStats(theChannel);
            close ( theChannel->socket );
            theChannel->socket = INVALID_SOCKET;
            break;
        }
        else
        {
            // nRead is < 0. This is nominally a socket error
            // but maybe just EAGAIN
            if (EAGAIN == errno)
            {
                theChannel->eAgains += 1;
                break;
            }
            else
            {
                // Socket error
                LogError (WARN, "Client socket error during recv: " + pathName, errno);
                PrintStats(theChannel);
                close ( theChannel->socket );
                theChannel->socket = INVALID_SOCKET;
                break;
            }
        }
    }
}


///////////////////////////////////////////////
// GuestPoller
//
// Create FDsets for all sockets (guest UDS and host TCP),
// Issue a select with given timeout.
// Process all channels that need attention.
//
void GuestPoller ( long int waitTimeUSec )
{
    int pollerHighFd;
    fd_set pollerReadSet;
    fd_set pollerWriteSet;
    fd_set pollerErrorSet;
    struct timeval waitTime;
    
    // construct fd_sets
    pollerHighFd = 0;
    FD_ZERO(&pollerReadSet);
    FD_ZERO(&pollerWriteSet);
    FD_ZERO(&pollerErrorSet);
    
    waitTime.tv_sec = 0;
    waitTime.tv_usec = waitTimeUSec;
    
    // Put the listening socket into the FDsets
    assert (INVALID_SOCKET != listeningSocket);
    pollerHighFd = listeningSocket;
    FD_SET(listeningSocket, &pollerReadSet); // Only wake on read
    FD_SET(listeningSocket, &pollerErrorSet);

    // Put our connections' sockets in
    std::map< SOCKET, SinkChannelPtr > ::iterator itr;
    for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
    {
        // selectors for guest-side UDS
        SOCKET         thisSock = itr->first;
        SinkChannelPtr pChan    = itr->second;
        
        assert (thisSock != INVALID_SOCKET);

        FD_SET(thisSock, &pollerReadSet);

        // Always set the error bit
        FD_SET(thisSock, &pollerErrorSet);

        if (thisSock > pollerHighFd)
            pollerHighFd = thisSock;
    }
    
    int selectResult = select(pollerHighFd + 1,
                              &pollerReadSet,
                              &pollerWriteSet,
                              &pollerErrorSet,
                              &waitTime);
    if (selectResult < 0)
    {
        LogError (PANIC, "Select(): ", errno);
        exit(EXIT_FAILURE);
    }
    
    if (selectResult > 0)
    {
        // Process listen socket first
        if (FD_ISSET(listeningSocket, &pollerReadSet))
        {
            DoAccept();
        }

        // Hunt for indicated FD values and make like a sink
        for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
        {
            SOCKET         thisSock = itr->first;
            SinkChannelPtr pChan    = itr->second;

            if (FD_ISSET(thisSock, &pollerReadSet))
            {
                SinkData (thisSock, pChan);
            }
        }
        //
        // Delete channels still on probation after directory walk
        //
        std::list<SOCKET> deleteList;
        
        for (itr = channelStore.begin(); itr != channelStore.end(); itr++)
        {
            if (itr->second->socket == INVALID_SOCKET)
            {
                deleteList.push_back(itr->first);
            }
        }

        for (std::list<SOCKET>::const_iterator dIter = deleteList.begin(); dIter != deleteList.end(); dIter++)
            channelStore.erase(*dIter);
    }
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
void PollOneSecond ( void )
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
        
        // Run the select loop
        GuestPoller ( waitUSec );
        
        // re-read current time
        gettimeofday (&curTime, NULL);
    }
}


//
// main
//
int main (int argc, char * argv[])
{
    //
    // Set up ^C exit handler
    //
    signal (SIGINT, signal_handler);
    
    // Ignore SIGPIPE
    sigset_t newMask, oldMask;
    sigemptyset(&newMask);
    sigemptyset(&oldMask);
    
    sigaddset(&newMask, SIGPIPE);
    sigprocmask(SIG_BLOCK, &newMask, &oldMask);
    
    //
    // help/usage
    //
    if (argc >= 2)
    {
        if (boost::iequals(argv[1], "-h") ||
            boost::iequals(argv[1], "-help") ||
            boost::iequals(argv[1], "--h") ||
            boost::iequals(argv[1], "--help") )
        {
            Usage ( argv[0] );
            exit (EXIT_SUCCESS);
        }
    }
    
    //
    // Get the listen socket address from argv[1] else default to 5672.
    // This is the socket we serve to our clients.
    //
    listenPort = argc >= 2 ? atoi ( argv[1] ) : 5672;
    
    if (!( listenPort >= 0 && listenPort <= 65535 ))
    {
        std::cout << "Specify listen port in range 1..65535" << std::endl;
        exit (EXIT_FAILURE);
    }

    if (argc >= 3)
        verifyBytes = atoi( argv[2] );

    LOG (ALERT, "Starting serversink_host: port: "
        + to_string(listenPort) +
        ", verify: " + (verifyBytes ? "true" : "false"));

    //
    // Connect
    //
    ConnectListeningSocket();

    while (g_keepRunning)
    {
        // spend one second doing protocol work
        PollOneSecond ();
    }
    
    exit(EXIT_SUCCESS);
    
}