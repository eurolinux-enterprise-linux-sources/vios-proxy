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
// This is a program you run on a GUEST system.
// It tries to expose the differences between using a select() and a poll()
// on one of the virtioserial file handles the guest sees.
// It seems that poll() does not show POLLIN nor POLLOUT status!
// What's with that?
//
#include <vector>
#include <iostream>

#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <fcntl.h>

#include <boost/algorithm/string.hpp>

#include "vios_test_common.h"
#include "../vios_utility.h"

using namespace Utility;

int CHUNK_SIZE      = 100000;
int g_bytesToSend   = 1000000000;

unsigned char * pBuf = 0;

typedef struct pollfd pollfd_t;

std::vector<pollfd_t> pollFds;

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

// where to look
std::string  pathName = "/dev/virtio-ports/qpid.0";

//
// Usage
//
void Usage (std::string argv0)
{
    std::cout
    << "usage: " << argv0 << " [path_to_examine]" << std::endl
    << "        Default : " << pathName << std::endl;
}


//
// More Globals
//
SOCKET       hostFd = INVALID_SOCKET;
int          lastError;


void Reconnect ( void )
{
    if (hostFd != INVALID_SOCKET)
        return;

    // First try as net socket
    hostFd = open (pathName.c_str(), O_RDWR);
    
    if (INVALID_SOCKET != hostFd)
    {
        // Set nonblocking
        int opts;
        
        opts = fcntl(hostFd, F_GETFL);
        if (opts < 0)
        {
            LogError (ERROR, "fcntl(F_GETFL) on host file: " + pathName, errno );
            close (hostFd);
            hostFd = INVALID_SOCKET;
        }
        else
        {
            opts |= (O_NONBLOCK | O_ASYNC);
            if (fcntl(hostFd, F_SETFL, opts) < 0)
            {
                LogError (ERROR, "fcntl(F_SETFL) on host file: " + pathName, errno );
                close (hostFd);
                hostFd = INVALID_SOCKET;
            }
            else
            {
                LOG (INFO, "Opened channel to host: " + pathName);
            }
        }
    }
    else
    {
        // Else try as uds
        hostFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (INVALID_SOCKET == hostFd)
        {
            LogError ( WARN, "Failed to open guest channel: " + pathName, errno );
            close (hostFd);
            hostFd = INVALID_SOCKET;
        }
        else
        {
            // connect to uds
            struct sockaddr_un remote;
            remote.sun_family = AF_UNIX;
            strncpy(remote.sun_path, pathName.c_str(), sizeof(remote.sun_path) - 1);
            int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
            int result = connect(hostFd, (struct sockaddr *)&remote, len);
            if (SOCKET_ERROR == result)
            {
                LogError ( WARN, "Failed to connect to guest channel: " + pathName, errno );
                close ( hostFd );
                hostFd = INVALID_SOCKET;
            }
            else
            {
                // This is the success path
                // set non-blocking
                int opts;
                
                opts = fcntl(hostFd, F_GETFL, 0);
                if (opts < 0)
                {
                    LogError (ERROR, "fcntl(F_GETFL) on guest UDS: " + pathName, errno );
                    close (hostFd);
                    hostFd = INVALID_SOCKET;
                }
                else
                {
                    opts |= O_NONBLOCK;
                    if (fcntl(hostFd, F_SETFL, opts) < 0)
                    {
                        LogError (ERROR, "fcntl(F_SETFL) on guest UDS: " + pathName, errno );
                        close (hostFd);
                        hostFd = INVALID_SOCKET;
                    }
                    else
                    {
                        // declare success - the socket is open for polling
                        LOG (INFO, "Opened channel to guest: " + pathName);
                    }
                }
            }
        }
//        LogError ( WARN, "Failed to open host file: " + pathName, errno );
    }
}



//
//
//
void PollCycle ( void )
{
    if (INVALID_SOCKET == hostFd)
    {
        std::cout << "Poll: host FD is closed." << std::endl;
        return;
    }

    //
    // Show what poll() says
    //
    struct pollfd  PollFd;

    PollFd.fd      = hostFd;
    PollFd.events  = POLLOUT | POLLIN | POLLPRI;
    PollFd.revents = 0;

    int pollResult = poll ( &PollFd, 1, 0 );

    std::cout << "Poll: result= " << pollResult << ", revents= " <<
        std::hex << PollFd.revents << std::dec << " (";
    if ((PollFd.revents & POLLIN)  != 0) std::cout <<  "POLLIN";
    if ((PollFd.revents & POLLPRI) != 0) std::cout <<  "POLLPRI";
    if ((PollFd.revents & POLLOUT) != 0) std::cout <<  "POLLOUT";
    if ((PollFd.revents & POLLERR) != 0) std::cout <<  "POLLERR";
    if ((PollFd.revents & POLLHUP) != 0) std::cout <<  "POLLHUP";
    if ((PollFd.revents & POLLNVAL)!= 0) std::cout <<  "POLLNVAL";
    std::cout << ")" << std::endl;
    
    //
    // Show what select() says
    //
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
    waitTime.tv_usec = 0;
    
    // Put the listening socket into the FDsets
    pollerHighFd = hostFd;
    FD_SET(hostFd, &pollerReadSet);
    FD_SET(hostFd, &pollerWriteSet);
    FD_SET(hostFd, &pollerErrorSet);

    int selectResult = select(pollerHighFd + 1,
                              &pollerReadSet,
                              &pollerWriteSet,
                              &pollerErrorSet,
                              &waitTime);

    std::cout << "Select: result= " << selectResult << ", FD sets= (";
    if (FD_ISSET(hostFd, &pollerReadSet))  std::cout << "RD ";
    if (FD_ISSET(hostFd, &pollerWriteSet)) std::cout << "WR ";
    if (FD_ISSET(hostFd, &pollerErrorSet)) std::cout << "ER ";
    std::cout << ")" << std::endl;
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
        if (boost::iequals(argv[1], "-h")    ||
            boost::iequals(argv[1], "-help") ||
            boost::iequals(argv[1], "--h")   ||
            boost::iequals(argv[1], "--help") )
        {
            Usage ( argv[0] );
            exit (EXIT_SUCCESS);
        }
    }
    
    if (argc >= 2) pathName = argv[1];
    
    while (g_keepRunning)
    {
        LOG (INFO, "Polling " + pathName);
        
        Reconnect();

        PollCycle();

        MsSleep (4000);
    }
}