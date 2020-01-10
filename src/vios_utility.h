#ifndef VIOS_UTILITY_H
#define VIOS_UTILITY_H

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
#include <sstream>

namespace Utility
{
    //
    // MsSleep - a millisecond sleeper
    //
    void MsSleep(int ms);

    //
    // Logging
    //
    // Define the logging levels
    enum LOG_LEVEL
        { PANIC, ALERT, ERROR, WARN, NOTICE, INFO, DEBUG, UNKNOWN };

    extern std::string const logLevelNames[8];
    extern LOG_LEVEL g_logLevel;

    // Routine to set log level from command line arg text.
    // On failure print available log level names.
    bool LogSetLevel ( std::string theLevel );

    // Routine to write a log
    void Log (LOG_LEVEL theLevel, std::string theMsg);

    // Macro to evaluate log level before constructing the log args.
    // This suppresses the dirty work for logs that aren't produced.
#define LOG(lvl, logMsg) { if ((lvl) <= g_logLevel) Log ((lvl), (logMsg)); }

    //
    // LogError: log a message at the given level.
    //
    void LogError ( const LOG_LEVEL level,
                    const std::string & msg,
                    int error );

    // variable-to-string helper
    //
    // to use:
    //   int x = 3;
    //   std::string s = to_string(x);
    //
    template <class T>
    inline std::string to_string (const T& t)
    {
        std::stringstream ss;
        ss << t;
        return ss.str();
    }

    // string-to-variable helper
    //
    // to use:
    //  int i;
    //  if (from_string<int>(i, std::string("ff"), std::hex))
    //      std::cout << i << std::endl;
    //  else
    //      std::cout << "from_string failed << std::endl;
    //
    template <class T>
    bool from_string (T & t,
                      const std::string & s)
//                      std::ios_base & (*f)(std::iso_base &))
    {
        std::istringstream iss(s);
        return !(iss >> t).fail();
    }
}

#endif
