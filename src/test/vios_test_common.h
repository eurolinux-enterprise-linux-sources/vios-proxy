#ifndef VIOS_TEST_COMMON_H
#define VIOS_TEST_COMMON_H

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
#include <stdint.h>
#include <netinet/in.h>

#if defined __linux__
typedef int SOCKET;
#   define INVALID_SOCKET -1
#   define SOCKET_ERROR   -1
#endif

//
// Define the served port for tests
//
#define TEST_PORT (5672)

#endif
// kate: indent-mode cstyle; space-indent on; indent-width 4; 
