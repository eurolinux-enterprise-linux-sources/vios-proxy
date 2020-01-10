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
// File: vios_client_proxy.cpp
// What: Proxy program run on a client.
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
// TODO: Take advantage of poll() HUP status, client knows host proxy is absent.
//
#include <iostream>
#include <map>
#include <vector>

#include <sys/param.h>
#include <signal.h>

#include <boost/algorithm/string.hpp>

#include "vios_ghost.h"
#include "vios_utility.h"

using namespace Utility;

//
// Exit flag
//
int g_keepRunning = 1;

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
    << "usage: " << argv0 << " [host_dir [service_port [log_level]]]" << std::endl
    << "where" << std::endl
    << " host_dir     - path containing virtioserial endpoints to the host." << std::endl
    << "                Default = /dev/virtio-ports" << std::endl
    << " service_port - the service port on localhost that is proxied to the guests." << std::endl
    << "                Default = 5672" << std::endl
    << " log_level    - log verbosity setting." << std::endl
    << "                One of FATAL, ALERT, ERROR, WARN, NOTICE, INFO, DEBUG." << std::endl
    << "                Default = INFO" << std::endl;
}


int main ( int argc, char * argv[] )
{
    // The root directory of the client-side virtioserial endpoints.
    // Each file in this directory is a client-side protocol endpoint.
    char hostDirectoryRoot[MAXPATHLEN];

    // The port on the guest localhost that this tunnel offers to clients.
    int  listenPort;

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
    strncpy ( hostDirectoryRoot,
              ( argc >= 2 ? argv[1] : "/dev/virtio-ports" ), MAXPATHLEN - 1 );

    //
    // Get the listen socket address from argv[2] else default to 5672.
    // This is the socket we serve to our clients.
    //
    listenPort = argc >= 3 ? atoi ( argv[2] ) : 5672;

    if (!( listenPort >= 0 && listenPort <= 65535 ))
    {
        std::cout << "Specify listen port in range 1..65535" << std::endl;
        exit (EXIT_FAILURE);
    }

    //
    // Log level
    //
    LogSetLevel ("INFO");
    
    if (argc >= 4 &&
        !LogSetLevel (argv[3]) )
    {
        exit(EXIT_FAILURE);
    }

    // TODO: Specify reconnect timeout and connection timeout on command line.
    // TODO: See various protocol settings below
    
    // Log startup
    LOG (ALERT, "Client proxy start. host directory: "
        + std::string(hostDirectoryRoot) + ", service port: " + to_string(listenPort)
        + ", log level: " + logLevelNames[g_logLevel] );

    //
    // Set token generator seed.
    // Leave this out for repeatable token patterns.
    //
    time_t seed = time ( 0 );
    ViosFraming::GenerateTokenSetSeed ( seed );

    //
    // Various protocol settings
    //
    int const nPerReconnect  = 5;   // back off this many seconds between
                                    // failed connection attempts
    int connectTimeoutSeconds = 30; // allow this long for connection setup
                                    // time between SYN sent and SYNACK rcvd.
    
    //
    // create the guest manager rooted at the specified directory
    //
    boost::shared_ptr <ViosGHostManager>
    hostManager ( new ViosGHostManager (
        hostDirectoryRoot, listenPort,connectTimeoutSeconds ) );

    //
    // Print it out every N seconds
    //
    int nCReconnect;

    while ( g_keepRunning )
    {
        // Find and connect to host directories.
        // Reconnect guest-side endpoints that failed previously.
        hostManager->EnumerateHostDirectories ( true );
        //hostManager->PrintHostFacts();

        // Loop for a while processing the protocol
        for ( nCReconnect = 0;
              nCReconnect < nPerReconnect && g_keepRunning;
              nCReconnect++ )
        {
            // spend one second doing protocol work
            hostManager->ViosGHostPollOneSecond ();

            // One second has elapsed.
            // Find/destroy host directories but don't reconnect those with
            // errors.
            if (g_keepRunning)
            {
                hostManager->EnumerateHostDirectories ( false );

                // Run the per-second clock tick.
                // This gives connections making no progress a chance to time out.
                hostManager->ViosGHostClockTick ();
            }
        }
    }

    hostManager->ViosGHostDestroyChannels();
    
    // Log shutdown
    LOG (ALERT, "Client proxy stop. host directory: "
    + std::string(hostDirectoryRoot) + ", service port: " + to_string(listenPort));

    exit ( EXIT_SUCCESS );
}
// kate: indent-mode cstyle; space-indent on; indent-width 0; 
