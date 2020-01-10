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
#include <iostream>

#include <boost/algorithm/string.hpp>

#include "vios_utility.h"

namespace Utility
{
//
// MsSleep - a millisecond sleeper
//
void MsSleep(int ms)
{
    struct timespec time;
    time.tv_sec = ms / 1000;
    time.tv_nsec = (ms % 1000) * (1000 * 1000);
    nanosleep(&time,NULL);
}

//
// Logging
//
std::string const logLevelNames[8] = {
    "FATAL", "ALERT", "ERROR", "WARN", "NOTICE", "INFO", "DEBUG", "UNKNOWN" };

// global log level setting and default setting
LOG_LEVEL g_logLevel = INFO;

// routine to set log level from command line arg text
// on failure print available log level names
bool LogSetLevel ( std::string theLevel )
{
    for (int newL = PANIC; newL < UNKNOWN; newL++)
    {
        if (boost::iequals(theLevel, logLevelNames[newL]))
        {
            g_logLevel = (LOG_LEVEL)newL;
            return true;
        }
    }
    std::cout << "Unknown log level. Use one of: ";
    for (int newL = PANIC; newL < UNKNOWN; newL++)
    {
        std::cout << logLevelNames[newL] << " ";
    }
    std::cout << std::endl;
    return false;
}

//
// Log
//
// routine to write a log entry
void Log ( LOG_LEVEL theLevel, std::string theMsg)
{
    if (theLevel <= g_logLevel)
    {
        time_t rawtime;
        time ( &rawtime );
        std::string t( ctime (&rawtime) );

        std::cout << t.substr ( 0, t.length() - 1 )
                  << " "
                  << logLevelNames[theLevel]
                  << " : "
                  << theMsg
                  << std::endl;
        std::cout.flush();
    }
}


//
// LogError: log a message at the given level.
//
void LogError ( const LOG_LEVEL level,
                const std::string & msg,
                int error )
{
    std::string message = msg + ": " + strerror (error);
    LOG ( level, message );
}

}