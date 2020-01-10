#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
project (vios-proxy)

cmake_minimum_required(VERSION 2.4.0 FATAL_ERROR)

if (CMAKE_COMPILER_IS_GNUCXX)
   set (COMPILER_FLAGS "")
   # Warnings: Enable as many as possible, keep the code clean.
   #
   # The following warnings are deliberately omitted, they warn on valid code.
   # -Wunreachable-code -Wpadded -Winline
   # -Wshadow - warns about boost headers.
   set (WARNING_FLAGS
        "-Werror -pedantic -Wall -Wextra -Wno-shadow -Wpointer-arith -Wcast-qual -Wcast-align -Wno-long-long -Wvolatile-register-var -Winvalid-pch -Wno-system-headers -Woverloaded-virtual")
endif (CMAKE_COMPILER_IS_GNUCXX)

add_executable(vios_proxy_host
    vios_proxy_host.cpp
    vios_hguest.cpp
    vios_hchannel.cpp
    vios_framing.cpp
    vios_utility.cpp)

set_target_properties(vios_proxy_host PROPERTIES OUTPUT_NAME vios-proxy-host)

INSTALL(TARGETS vios_proxy_host
        RUNTIME
        DESTINATION bin)

add_executable(vios_proxy_guest
    vios_proxy_guest.cpp
    vios_ghost.cpp
    vios_gchannel.cpp
    vios_framing.cpp
    vios_utility.cpp)

set_target_properties(vios_proxy_guest PROPERTIES OUTPUT_NAME vios-proxy-guest)

INSTALL(TARGETS vios_proxy_guest
        RUNTIME
        DESTINATION bin)

add_subdirectory(test)

INSTALL(FILES ../doc/man/vios-proxy-host.1.gz ../doc/man/vios-proxy-guest.1.gz DESTINATION share/man/man1)
