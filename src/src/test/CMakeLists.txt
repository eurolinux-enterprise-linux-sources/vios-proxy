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

#
# ServerSink
#
# A sink program runs on the host.
# Source programs run on clients.
# Both report statistics on blasts of data across the Vios channel
#
add_executable(vios_test_serversink_host
    ../vios_utility.cpp
    vios_test_serversink_host.cpp
    )

set_target_properties(vios_test_serversink_host PROPERTIES OUTPUT_NAME vios_test_serversink_host)

add_executable(vios_test_serversink_client
    ../vios_utility.cpp
    vios_test_serversink_client.cpp
    )

set_target_properties(vios_test_serversink_client PROPERTIES OUTPUT_NAME vios_test_serversink_client)

#
# SelectPoll
#
# Run both poll() and select() on a guest's virtioserial fd.
# Research...
#
add_executable(vios_test_selectpoll
    ../vios_utility.cpp
    vios_test_selectpoll.cpp
    )

set_target_properties(vios_test_selectpoll PROPERTIES OUTPUT_NAME vios_test_selectpoll)

