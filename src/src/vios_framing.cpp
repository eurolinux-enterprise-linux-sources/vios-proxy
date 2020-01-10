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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>

#include "vios_framing.h"

namespace ViosFraming
{
//
// the seed
//
static uint32_t rndNext = 1;

//
// Generate a "printable" ascii byte
//
uint8_t GenerateByte ( void )
{
    int tmp = rand_r ( &rndNext );
    return tmp % 94 + 33; // 0..93 + 33 = 33..126. That's '!' to '~'.
}

//
// GenerateToken
//
// Return a "printable" token
//
uint32_t GenerateToken ( void )
{
    return GenerateByte() << 24 |
           GenerateByte() << 16 |
           GenerateByte() << 8  |
           GenerateByte();
}

//
// SetSeed
//
void GenerateTokenSetSeed ( uint32_t seed )
{
    rndNext = seed;
}

//
// getConnectionId
//
// Return a printable string identifying the currently negotiated connection.
//
std::string& ViosHeader::GetConnectionId ( std::string& retVal, uint32_t gT, uint32_t hT)
{
    std::string sG( reinterpret_cast<char const *>( &gT ), 4);
    std::string sH( reinterpret_cast<char const *>( &hT ), 4);
    
    retVal = "[g:" + sG + ",h:" + sH + "]";
    return retVal;
}

}
