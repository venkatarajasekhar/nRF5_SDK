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

#ifndef H_BLE_PORT_
#define H_BLE_PORT_

#include "main.h"

#define assert(expr)                ASSERT(expr)

#define OS_ENTER_CRITICAL(sr)       (void)sr;    \
                                    taskENTER_CRITICAL()
#define OS_EXIT_CRITICAL(sr)        (void)sr;    \
                                    taskEXIT_CRITICAL()

#define OS_CFG_ALIGN_4              (4)
#define OS_CFG_ALIGN_8              (8)
#define OS_CFG_ALIGNMENT            OS_CFG_ALIGN_4
#define OS_ALIGNMENT                OS_CFG_ALIGNMENT
#define OS_ALIGN(__n, __a) (                              \
        (((__n) & ((__a) - 1)) == 0) ?                 \
            (__n)                      :                 \
            ((__n) + ((__a) - ((__n) & ((__a) - 1)))) \
        )

/* OS error enumerations */
enum os_error {
    OS_OK = 0,
    OS_ENOMEM = 1,
    OS_EINVAL = 2,
    OS_INVALID_PARM = 3,
    OS_MEM_NOT_ALIGNED = 4,
    OS_BAD_MUTEX = 5,
    OS_TIMEOUT = 6,
    OS_ERR_IN_ISR = 7,      /* Function cannot be called from ISR */
    OS_ERR_PRIV = 8,        /* Privileged access error */
    OS_NOT_STARTED = 9,     /* Operating must be started to call this function, but isn't */
    OS_ENOENT = 10,         /* No such thing */
};

typedef enum os_error os_error_t;
typedef uint8_t os_sr_t;

/**
 * A mbuf pool from which to allocate mbufs. This contains a pointer to the os 
 * mempool to allocate mbufs out of, the total number of elements in the pool, 
 * and the amount of "user" data in a non-packet header mbuf. The total pool 
 * size, in bytes, should be: 
 *  os_mbuf_count * (omp_databuf_len + sizeof(struct os_mbuf))
 */
struct os_mbuf_pool {
    /** 
     * Total length of the databuf in each mbuf.  This is the size of the 
     * mempool block, minus the mbuf header
     */
    uint16_t omp_databuf_len;
    /**
     * Total number of memblock's allocated in this mempool.
     */
    uint16_t omp_mbuf_count;
    /**
     * The memory pool which to allocate mbufs out of 
     */
    struct os_mempool *omp_pool;

    /**
     * Link to the next mbuf pool for system memory pools.
     */
    os_mbuf_pool *omp_next;
};

/**
 * A packet header structure that preceeds the mbuf packet headers.
 */
struct os_mbuf_pkthdr {
    /**
     * Overall length of the packet. 
     */
    uint16_t omp_len;
    /**
     * Flags
     */
    uint16_t omp_flags;
    /**
     * Next packet in the mbuf chain.
     */
    os_mbuf_pkthdr *omp_next;
};

/**
 * Chained memory buffer.
 */
struct os_mbuf {
    /**
     * Current pointer to data in the structure
     */
    uint8_t *om_data;
    /**
     * Flags associated with this buffer, see OS_MBUF_F_* defintions
     */
    uint8_t om_flags;
    /**
     * Length of packet header
     */
    uint8_t om_pkthdr_len;
    /**
     * Length of data in this buffer 
     */
    uint16_t om_len;

    /**
     * The mbuf pool this mbuf was allocated out of 
     */
    struct os_mbuf_pool *om_omp;

    /**
     * Pointer to next entry in the chained memory buffer
     */
    os_mbuf *om_next;

    /**
     * Pointer to the beginning of the data, after this buffer
     */
    uint8_t om_databuf[0];
};

#endif
