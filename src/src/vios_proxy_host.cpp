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
// File: vios_host_proxy.cpp
// What: Proxy program run on a host.
//       It uses virtioserial ports as proxy network channels.
//
// Project-wide TODO items:
// TODO: try/catch
// TODO: better error handling
// TODO: boost::options for CLI
// TODO: config file?
// TODO: Reset messages need a 'reason' field
// TODO: Add some statistics - QMF or otherwise
// TODO: Add a CLI for runtime control and status
// TODO: Purge various magic strings and numbers
// TODO: improved logging - syslog?
//

#include <iostream>

#include <sys/param.h>
#include <signal.h>

#include <boost/algorithm/string.hpp>

#include "vios_hguest.h"
#include "vios_utility.h"
#include "vios_framing.h"

using namespace Utility;

//
// Exit flag
//
int g_keepRunning = 1;

struct sigaction new_action, old_action;

//
// Signal handler
//
void signal_handler (int signum)
{
    if (signum == SIGINT)
    {
        g_keepRunning = 0;
    }
}

//
// Usage
//
void Usage (std::string argv0)
{
  std::cout
    << "usage: " << argv0 << " [guest_dir [service_port [log_level]]]" << std::endl
    << "where" << std::endl
    << " guest_dir    - path containing directories of virtioserial endpoints to guests." << std::endl
    << "                Default = /tmp/qpid" << std::endl
    << " service_port - the service port on localhost that is proxied to the guests." << std::endl
    << "                Default = 5672" << std::endl
    << " log_level    - log verbosity setting." << std::endl
    << "                One of FATAL, ALERT, ERROR, WARN, NOTICE, INFO, DEBUG." << std::endl
    << "                Default = INFO" << std::endl;
}


//
// main
//
int main (int argc, char * argv[])
{
    // The main root directory of the host-side virtioserial endpoints.
    // Each subdirectory in this root represents a guest. Each unix domain
    // socket in any guest subdirectory is a host-side protocol endpoint.
    char guestDirectoryRoot[MAXPATHLEN];

    // The port on localhost that offers the service that this vios tunnel
    // relays to the guests.
    int  servicePort;

    //
    // Set up signal handlers
    //
    if (signal (SIGINT, signal_handler) == SIG_IGN)
        signal (SIGINT, SIG_IGN);
    
    // Permanently block SIGPIPE
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
    // Process args, set defaults
    //   Args may be skipped but if present then they must be in this order:
    //     1. Guest root directory
    //     2. Port number of the service on localhost that is proxied to guests.
    //     3. Log level
    //
    // Get the directory to monitor from argv[1]
    // else default to project-local directory
    //

    strncpy ( guestDirectoryRoot,
              (argc >= 2 ? argv[1] : "/tmp/qpid"), MAXPATHLEN - 1 );

    //
    // This program forwards to a given port only on localhost.
    // Parse the arg specifies the port.
    //
    servicePort = argc >= 3 ? atoi(argv[2]) : 5672;

    //
    // Log level
    //
    LogSetLevel ("INFO");
    
    if (argc >= 4 &&
        !LogSetLevel (argv[3]) )
    {
        exit(EXIT_FAILURE);
    }
    
    // Log startup
    LOG (ALERT, "Host proxy start. guest directory: "
        + std::string(guestDirectoryRoot) + ", service port: " + to_string(servicePort)
        + ", log level: " + logLevelNames[g_logLevel] );

    //
    // Set token generator seed.
    // Leave this out for repeatable token patterns.
    //
    time_t seed = time(0);
    ViosFraming::GenerateTokenSetSeed(seed);

    //
    // create the guest manager rooted at the specified directory
    //
    boost::shared_ptr <ViosHGuestManager>
    guestManager (new ViosHGuestManager(guestDirectoryRoot, servicePort));

    //
    // Print it out every N seconds
    //
    int const nPerReconnect  = 5; // sleep this many seconds per reconnect
    int nCReconnect;
    //int connectTimeoutSeconds = 5;
    

    while (g_keepRunning)
    {
        // Find and connect to guest directories.
        // Reconnect host-side endpoints that failed previously.
        guestManager->EnumerateGuestDirectories( true );

        // Loop for a while processing the protocol
        for (nCReconnect = 0;
             nCReconnect < nPerReconnect && g_keepRunning;
             nCReconnect++)
        {
            // spend one second doing protocol work
            guestManager->ViosHGuestPollOneSecond ();

            // One second has elapsed.
            // Find/destroy guest directories but don't reconnect those with
            // errors.
            if (g_keepRunning)
                guestManager->EnumerateGuestDirectories ( false );
        }
    }
    
    guestManager->ViosHGuestDestroyGuests();

    // Log shutdown
    LOG (ALERT, "Host proxy stop. guest directory: "
    + std::string(guestDirectoryRoot) + ", service port: " + to_string(servicePort));

    exit(EXIT_SUCCESS);
}
// kate: indent-mode cstyle; space-indent on; indent-width 0; 
