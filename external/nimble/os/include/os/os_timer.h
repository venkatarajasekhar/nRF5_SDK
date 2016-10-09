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

#ifndef H_HAL_CPUTIME_
#define H_HAL_CPUTIME_

#ifdef __cplusplus
extern "C" {
#endif

#include "os/os_list.h"

/*
 * NOTE: these definitions allow one to override the cputime frequency used.
 * The reason these definitions exist is to make the code more efficient/smaller
 * when CPUTIME counts at 1 MHz.
 *
 * For those who want a different cputime frequency, you can set the macro
 * HAL_CPUTIME to the desired frequency in your project, target or bsp.
 */
#ifndef HAL_CPUTIME
#define HAL_CPUTIME 1000000
#endif

#if (HAL_CPUTIME == 1000000)
#define HAL_CPUTIME_1MHZ
#endif

#define CPUTIMER_SET_CHANNEL        NRF_TIMER_CC_CHANNEL0
#define CPUTIMER_SET_EVENT          NRF_TIMER_EVENT_COMPARE0

#define CPUTIMER_GET_CHANNEL        NRF_TIMER_CC_CHANNEL1
#define CPUTIMER_GET_EVENT          NRF_TIMER_EVENT_COMPARE1

/* CPU timer callback function */
typedef void (*cputimer_func)(void *arg);

/* CPU timer */
struct cpu_timer {
    cputimer_func   cb;
    void            *arg;
    uint32_t        cputime;
    struct list_head link;
};

/* CPUTIME data. */
struct cputime_data
{
    uint32_t ticks_per_usec;    /* number of ticks per usec */
    uint32_t cputime_high;      /* high word of 64-bit cpu time */
    uint32_t timer_isrs;        /* Number of timer interrupts */
    uint32_t ocmp_ints;         /* Number of ocmp interrupts */
    uint32_t uif_ints;          /* Number of overflow interrupts */
};

/* Helpful macros to compare cputimes */
#define CPUTIME_LT(__t1, __t2) ((int32_t)   ((__t1) - (__t2)) < 0)
#define CPUTIME_GT(__t1, __t2) ((int32_t)   ((__t1) - (__t2)) > 0)
#define CPUTIME_GEQ(__t1, __t2) ((int32_t)  ((__t1) - (__t2)) >= 0)
#define CPUTIME_LEQ(__t1, __t2) ((int32_t)  ((__t1) - (__t2)) <= 0)

/**
 * cputime init
 *
 * Initialize the cputime module. This must be called after os_init is called
 * and before any other timer API are used. This should be called only once
 * and should be called before the hardware timer is used.
 *
 * @param cputime_task_prio The desired cputime task priority.
 *
 * @return int 0 on success; -1 on error.
 */
int cputime_init(uint8_t cputime_task_prio);

/**
 * cputime get64
 *
 * Returns cputime as a 64-bit number.
 *
 * @return uint64_t The 64-bit representation of cputime.
 */
uint64_t cputime_get64(void);

/**
 * cputime get32
 *
 * Returns the low 32 bits of cputime.
 *
 * @return uint32_t The lower 32 bits of cputime
 */
uint32_t cputime_get32(void);

/**
 * cputime nsecs to ticks
 *
 * Converts the given number of nanoseconds into cputime ticks.
 *
 * @param usecs The number of nanoseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'nsecs'
 */
uint32_t cputime_nsecs_to_ticks(uint32_t nsecs);

/**
 * cputime ticks to nsecs
 *
 * Convert the given number of ticks into nanoseconds.
 *
 * @param ticks The number of ticks to convert to nanoseconds.
 *
 * @return uint32_t The number of nanoseconds corresponding to 'ticks'
 */
uint32_t cputime_ticks_to_nsecs(uint32_t ticks);

#if defined(HAL_CPUTIME_1MHZ)
#define cputime_usecs_to_ticks(x)       (x)
#define cputime_ticks_to_usecs(x)       (x)
#else
/**
 * cputime usecs to ticks
 *
 * Converts the given number of microseconds into cputime ticks.
 *
 * @param usecs The number of microseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'usecs'
 */
uint32_t cputime_usecs_to_ticks(uint32_t usecs);

/**
 * cputime ticks to usecs
 *
 * Convert the given number of ticks into microseconds.
 *
 * @param ticks The number of ticks to convert to microseconds.
 *
 * @return uint32_t The number of microseconds corresponding to 'ticks'
 */
uint32_t cputime_ticks_to_usecs(uint32_t ticks);
#endif

/**
 * cputime delay ticks
 *
 * Wait until the number of ticks has elapsed. This is a blocking delay.
 *
 * @param ticks The number of ticks to wait.
 */
void cputime_delay_ticks(uint32_t ticks);

/**
 * cputime delay nsecs
 *
 * Wait until 'nsecs' nanoseconds has elapsed. This is a blocking delay.
 *
 * @param nsecs The number of nanoseconds to wait.
 */
void cputime_delay_nsecs(uint32_t nsecs);

/**
 * cputime delay usecs
 *
 * Wait until 'usecs' microseconds has elapsed. This is a blocking delay.
 *
 * @param usecs The number of usecs to wait.
 */
void cputime_delay_usecs(uint32_t usecs);

/**
 * cputime timer init
 *
 *
 * @param timer The timer to initialize. Cannot be NULL.
 * @param fp    The timer callback function. Cannot be NULL.
 * @param arg   Pointer to data object to pass to timer.
 */
void cputime_timer_init(struct cpu_timer *timer, cputimer_func fp, void *arg);

/**
 * cputime timer start
 *
 * Start a cputimer that will expire at 'cputime'. If cputime has already
 * passed, the timer callback will still be called (at interrupt context).
 *
 * @param timer     Pointer to timer to start. Cannot be NULL.
 * @param cputime   The cputime at which the timer should expire.
 */
void cputime_timer_start(struct cpu_timer *timer, uint32_t cputime);

/**
 * cputimer timer relative
 *
 * Sets a cpu timer that will expire 'usecs' microseconds from the current
 * cputime.
 *
 * @param timer Pointer to timer. Cannot be NULL.
 * @param usecs The number of usecs from now at which the timer will expire.
 */
void cputime_timer_relative(struct cpu_timer *timer, uint32_t usecs);

/**
 * cputime timer stop
 *
 * Stops a cputimer from running. The timer is removed from the timer queue
 * and interrupts are disabled if no timers are left on the queue. Can be
 * called even if timer is not running.
 *
 * @param timer Pointer to cputimer to stop. Cannot be NULL.
 */
void cputime_timer_stop(struct cpu_timer *timer);

/*
 * Used between MCU specific files and generic HAL. Not intended as an API
 * to be called by the user.
 */
void cputime_chk_expiration(void);

/*--- HW specific API. These are not intended to be called by user  ---*/
void cputime_disable_ocmp(void);
void cputime_set_ocmp(struct cpu_timer *timer);
int cputime_hw_init(void);

#ifdef __cplusplus
}
#endif

#endif /* H_HAL_CPUTIMER_ */
