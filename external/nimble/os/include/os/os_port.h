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

#define OS_TICKS_PER_SEC            configTICK_RATE_HZ

#define STATS_VARIABLE(__name)      __name

#define assert(expr)                ASSERT(expr)

#define os_malloc(size)             pvPortMalloc(size)
#define os_free(pv)                 vPortFree(pv)

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

struct os_task {
    TaskHandle_t handle;
}

#endif
