/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_BLE_HS_ENDIAN_
#define H_BLE_HS_ENDIAN_

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#define TOFROMLE16(x)   ((uint16_t) (x))
#define TOFROMLE32(x)   ((uint32_t) (x))
#define TOFROMLE64(x)   ((uint64_t) (x))

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#define TOFROMLE16(x)   ((uint16_t)                                 \
                         ((((x) & 0xff00) >> 8) |                   \
                          (((x) & 0x00ff) << 8)))

#define TOFROMLE32(x)  ((uint32_t)                                  \
                         ((((x) & 0xff000000) >> 24) |              \
                          (((x) & 0x00ff0000) >>  8) |              \
                          (((x) & 0x0000ff00) <<  8) |              \
                          (((x) & 0x000000ff) << 24)))

#define TOFROMLE64(x)  ((uint64_t)                                  \
                         ((((x) & 0xff00000000000000ull) >> 56) |   \
                          (((x) & 0x00ff000000000000ull) >> 40) |   \
                          (((x) & 0x0000ff0000000000ull) >> 24) |   \
                          (((x) & 0x000000ff00000000ull) >>  8) |   \
                          (((x) & 0x00000000ff000000ull) <<  8) |   \
                          (((x) & 0x0000000000ff0000ull) << 24) |   \
                          (((x) & 0x000000000000ff00ull) << 40) |   \
                          (((x) & 0x00000000000000ffull) << 56)))

#else

#error Unsupported endianness.

#endif

#endif
