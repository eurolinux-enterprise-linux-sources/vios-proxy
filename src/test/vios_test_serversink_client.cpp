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
#include <vector>

#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/poll.h>
#include <sys/time.h>

#include <boost/algorithm/string.hpp>

#include "vios_test_common.h"
#include "../vios_utility.h"

using namespace Utility;

// define the size of the chunk buffer
#define CHUNK_SIZE_SIZE   100000

// set chunk size default
int CHUNK_SIZE      = CHUNK_SIZE_SIZE;

// set bytes-to-send default
int g_bytesToSend   = 1000000000;

// allocate the chunk buffe
unsigned char buffer[CHUNK_SIZE_SIZE];

// type def of pollFds objects
typedef struct pollfd pollfd_t;

// the pollFds vector
std::vector<pollfd_t> pollFds;

// set true for checking the data
bool verifyBytes = false;

// when checking this is the next byte value
uint8_t nextByte = 0;

// Exit flag
int g_keepRunning = 1;

// Port to open
uint16_t servicePort = TEST_PORT;

// Signal handler
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
    << "usage: " << argv0 << " [bytes_to_send [service_port [verify_switch]]]" << std::endl
    << std::endl
    << " bytes_to_send- number of bytes to send to host sink" << std::endl
    << "                Default = " << g_bytesToSend << std::endl
    << " service_port - the service port on localhost that is proxied to the guests." << std::endl
    << "                Default = " << servicePort << std::endl
    << " verify_switch- generate a data pattern or not." << std::endl
    << "                Default = " << verifyBytes << std::endl;
}


//
// More Globals
//
SOCKET       serviceSocket = INVALID_SOCKET;
int          lastError;
std::string  pathName = "test port";


//
// OpenServiceSocket
//
// Try to open the channel to the service network socket
//
bool OpenServiceSocket ( void )
{
    assert (serviceSocket == INVALID_SOCKET);
    bool result = false;
    
    serviceSocket = socket (PF_INET, SOCK_STREAM, 0);
    
    if (INVALID_SOCKET == serviceSocket)
    {
        LogError ( WARN, "Failed to create service channel: " + pathName, errno );
        lastError = errno;
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
        }
        else
        {
            LOG (INFO, "Opened channel to service: " + pathName);
            result = true;
        }
    }
    return result;
}


//
// Send a chunk
//
bool SendAChunk ( int lenToSend )
{
    assert (lenToSend <= CHUNK_SIZE);

    if (verifyBytes)
    {
        assert ((size_t)lenToSend <= sizeof(buffer));

        for (int i=0; i<lenToSend; i++)
            buffer[i] = nextByte++;
    }
    
    int bytesSent = 0;
    
    while (lenToSend > 0 && g_keepRunning)
    {
        bytesSent = send(serviceSocket, &buffer[bytesSent], lenToSend, MSG_DONTWAIT);

        if ((size_t)bytesSent == lenToSend)
        {
            return true;
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
                // continue
            }
            else if (bytesSent == 0)
            {
                // closed
                LOG (INFO, "Service socket closed during send: " + pathName);

                return false;
            }
            else
            {
                // nSent is < 0. This is nominally a socket error
                // but maybe just EAGAIN
                if (EAGAIN == errno)
                {
                    bytesSent = 0;
                    // The socket is just full.
                    // Try again.
                    // continue
                }
                else
                {
                    // Socket error
                    LogError (WARN, "Service socket error during send: " + pathName, errno);
                    lastError = errno;
                    close ( serviceSocket );
                    serviceSocket = INVALID_SOCKET;
                    return false;
                }
            }
        }

        //
        // It didn't exit. Accumulate bytes sent so far.
        //
        lenToSend -= bytesSent;

        //
        // Now poll() to wait
        //
        struct pollfd  newPollFd;
        struct pollfd *pThisFd;
        nfds_t         nFds = 0;
        int            waitTime_mS = 1000;
        
        // Add single fd for read
        if (pollFds.size() == 0)
        {
            pollFds.push_back ( newPollFd );
        }

        assert (pollFds.size() == 1);

        pThisFd = &pollFds[ nFds ];

        pThisFd->fd = serviceSocket;
        pThisFd->events = POLLOUT;

        nFds += 1;

        int pollResult = 0;
        
        while (pollResult == 0 && g_keepRunning)
        {
            pollResult = poll ( &pollFds[0], nFds, waitTime_mS );
            if (pollResult < 0)
            {
                LogError(PANIC, "poll", errno);
                exit(EXIT_FAILURE);
            }
        }

        // loop to try again
    }
    return false;
}

//
// main
//
int main (int argc, char * argv[])
{
    //
    // Start/stop storage
    //
    struct timeval startTime = {0};
    struct timeval endTime   = {0};

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
        if (boost::iequals(argv[1], "-h")    ||
            boost::iequals(argv[1], "-help") ||
            boost::iequals(argv[1], "--h")   ||
            boost::iequals(argv[1], "--help") )
        {
            Usage ( argv[0] );
            exit (EXIT_SUCCESS);
        }
    }

    if (argc >= 2)
        g_bytesToSend = atoi( argv[1] );

    servicePort = (argc >= 3) ? atoi( argv[2] ) : TEST_PORT;

    if (argc >= 4)
        verifyBytes = atoi( argv[3] );

    LOG (ALERT, "Starting serversink_client: bytes: " + to_string(g_bytesToSend) +
        ", port: " + to_string(servicePort) +
        ", verify: " + (verifyBytes ? "true" : "false"));
    
    //
    // Connect to test port
    //
    // Running back to back serversink_clients exposes an issue with the Vios
    // tunnel: the tunnel takes some time to shut down after a client exits.
    // So a good client will try connecting a couple of times to work around
    // the proxy's link closure.
    //
    if (!OpenServiceSocket())
    {
        // That's once.
        MsSleep (50);
        
        if (!OpenServiceSocket())
        {
            // That's twice.
            MsSleep (100);
            
            if (!OpenServiceSocket())
            {
                // Strike three, you're out!
                LOG (PANIC, "TEST FAIL: Unable to open test service port.");
                exit (EXIT_FAILURE);
            }
        }
    }

    // read current time
    gettimeofday (&startTime, NULL);

    //
    // Send 'em
    //
    int bytesSent = 0;
    while (bytesSent < g_bytesToSend && g_keepRunning)
    {
        int bytesNow = g_bytesToSend - bytesSent;
        if (bytesNow > CHUNK_SIZE)
            bytesNow = CHUNK_SIZE;

        if (!SendAChunk( bytesNow ))
            break;

        bytesSent += bytesNow;
    }

    // compute end time

    gettimeofday (&endTime, NULL);

    // close socket
    if (serviceSocket != INVALID_SOCKET)
    {
        close (serviceSocket);
    }

    // report

    int elapsed_S = endTime.tv_sec - startTime.tv_sec;
    int elapsed_uS = endTime.tv_usec - startTime.tv_usec;

    int elapsed_uTime = elapsed_S * 1000000 + elapsed_uS;

    double bytesPerSec = (double) bytesSent / (double) elapsed_uTime ;
    bytesPerSec *= 1000000.0;
    
    std::cout << "Bytes sent   = " << to_string(bytesSent)     << std::endl
              << "Elapsed uSec = " << to_string(elapsed_uTime) << std::endl
              << "Bytes/sec    = " << to_string(bytesPerSec)   << std::endl;
}