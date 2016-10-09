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

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "os/os.h"
#include "hal/hal_cputime.h"

/* XXX:
 *  - Must determine how to set priority of cpu timer interrupt
 *  - Determine if we should use a mutex as opposed to disabling interrupts
 *  - Should I use macro for compare channel?
 *  - Sync to OSTIME.
 */
#if !defined(HAL_CPUTIME_1MHZ)
extern struct cputime_data g_cputime;
#endif

/* Queue for timers */
static struct list_head cputimer_q;
const nrf_drv_timer_t   cputimer_id = NRF_DRV_TIMER_INSTANCE(0);

/* The CPU time task data structure */
#define CPU_TIME_STACK_SIZE   (80)
struct TaskHandle_t g_cpu_time_task;
SemaphoreHandle_t g_cpu_time_sem = NULL;

/**
 * cputime chk expiration
 *
 * Iterates through the cputimer queue to determine if any timers have expired.
 * If the timer has expired the timer is removed from the queue and the timer
 * callback function is executed.
 *
 */
void
cputime_chk_expiration(void)
{
    os_sr_t sr;
    struct cpu_timer *timer, *timer_tmp;

    OS_ENTER_CRITICAL(sr);
    list_for_each_entry_safe(timer, timer_tmp, &cputimer_q, link) {
        if ((int32_t)(cputime_get32() - timer->cputime) >= 0) {
            list_del(&timer->link);
            timer->cb(timer->arg);
        } else {
            break;
        }
    }

    /* Any timers left on queue? If so, we need to set OCMP */
    if (list_empty(&cputimer_q)) {
        cputime_disable_ocmp();
    } else {
        timer = list_first_entry(&cputimer_q, cpu_timer, link);
        cputime_set_ocmp(timer);
    }
    OS_EXIT_CRITICAL(sr);
}

/**
 * CPU Time task.
 *
 * This is the task that handles the CPU time event.
 *
 * @param arg
 */
void
cputime_handle_task(void *arg)
{
    while(true) {
        /* wait for timeout */
        if(pdTRUE == xSemaphoreTake(g_cpu_time_sem, portMAX_DELAY)) {
            cputime_chk_expiration();
        }
    }
}

/**
 * cputime init
 *
 * Initialize the cputime module. This must be called after os_init is called
 * and before any other timer API are used. This should be called only once
 * and should be called before the hardware timer is used.
 *
 * @param clock_freq The desired cputime frequency, in hertz (Hz).
 *
 * @return int 0 on success; -1 on error.
 */
int
cputime_init(uint8_t cputime_task_prio)
{
    /* Initialize the CPU time semaphore */
    g_cpu_time_sem = xSemaphoreCreateBinary();
    if(NULL == g_cpu_time_sem) {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }

    /* Initialize the CPU time task */
    if(pdPASS != xTaskCreate(cputime_handle_task, "cpu_time", CPU_TIME_STACK_SIZE,
                             NULL, cputime_task_prio, &g_cpu_time_task)) {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }

    INIT_LIST_HEAD(&cputimer_q);

    return cputime_hw_init();
}

/**
 * cputime nsecs to ticks
 *
 * Converts the given number of nanoseconds into cputime ticks.
 *
 * @param usecs The number of nanoseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'nsecs'
 */
uint32_t
cputime_nsecs_to_ticks(uint32_t nsecs)
{
    uint32_t ticks;

#if defined(HAL_CPUTIME_1MHZ)
    ticks = (nsecs + 999) / 1000;
#else
    ticks = ((nsecs * g_cputime.ticks_per_usec) + 999) / 1000;
#endif
    return ticks;
}

/**
 * cputime ticks to nsecs
 *
 * Convert the given number of ticks into nanoseconds.
 *
 * @param ticks The number of ticks to convert to nanoseconds.
 *
 * @return uint32_t The number of nanoseconds corresponding to 'ticks'
 */
uint32_t
cputime_ticks_to_nsecs(uint32_t ticks)
{
    uint32_t nsecs;

#if defined(HAL_CPUTIME_1MHZ)
    nsecs = ticks * 1000;
#else
    nsecs = ((ticks * 1000) + (g_cputime.ticks_per_usec - 1)) /
            g_cputime.ticks_per_usec;
#endif

    return nsecs;
}

#if !defined(HAL_CPUTIME_1MHZ)
/**
 * cputime usecs to ticks
 *
 * Converts the given number of microseconds into cputime ticks.
 *
 * @param usecs The number of microseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'usecs'
 */
uint32_t
cputime_usecs_to_ticks(uint32_t usecs)
{
    uint32_t ticks;

    ticks = (usecs * g_cputime.ticks_per_usec);
    return ticks;
}

/**
 * cputime ticks to usecs
 *
 * Convert the given number of ticks into microseconds.
 *
 * @param ticks The number of ticks to convert to microseconds.
 *
 * @return uint32_t The number of microseconds corresponding to 'ticks'
 */
uint32_t
cputime_ticks_to_usecs(uint32_t ticks)
{
    uint32_t us;

    us =  (ticks + (g_cputime.ticks_per_usec - 1)) / g_cputime.ticks_per_usec;
    return us;
}
#endif

/**
 * cputime delay ticks
 *
 * Wait until the number of ticks has elapsed. This is a blocking delay.
 *
 * @param ticks The number of ticks to wait.
 */
void
cputime_delay_ticks(uint32_t ticks)
{
    uint32_t until;

    until = cputime_get32() + ticks;
    while ((int32_t)(cputime_get32() - until) < 0) {
        /* Loop here till finished */
    }
}

/**
 * cputime delay nsecs
 *
 * Wait until 'nsecs' nanoseconds has elapsed. This is a blocking delay.
 *
 * @param nsecs The number of nanoseconds to wait.
 */
void
cputime_delay_nsecs(uint32_t nsecs)
{
    uint32_t ticks;

    ticks = cputime_nsecs_to_ticks(nsecs);
    cputime_delay_ticks(ticks);
}

/**
 * cputime delay usecs
 *
 * Wait until 'usecs' microseconds has elapsed. This is a blocking delay.
 *
 * @param usecs The number of usecs to wait.
 */
void
cputime_delay_usecs(uint32_t usecs)
{
    uint32_t ticks;

    ticks = cputime_usecs_to_ticks(usecs);
    cputime_delay_ticks(ticks);
}

/**
 * cputime timer init
 *
 *
 * @param timer The timer to initialize. Cannot be NULL.
 * @param fp    The timer callback function. Cannot be NULL.
 * @param arg   Pointer to data object to pass to timer.
 */
void
cputime_timer_init(struct cpu_timer *timer, cputimer_func fp, void *arg)
{
    assert(timer != NULL);
    assert(fp != NULL);

    memset(timer, 0, sizeof(struct cpu_timer));
    timer->cb = fp;
    timer->arg = arg;
}

/**
 * cputime timer start
 *
 * Start a cputimer that will expire at 'cputime'. If cputime has already
 * passed, the timer callback will still be called (at interrupt context).
 * Cannot be called when the timer has already started.
 *
 * @param timer     Pointer to timer to start. Cannot be NULL.
 * @param cputime   The cputime at which the timer should expire.
 */
void
cputime_timer_start(struct cpu_timer *timer, uint32_t cputime)
{
    struct cpu_timer *entry;
    os_sr_t sr;

    assert(timer != NULL);
    assert(timer->link.next == NULL);

    /* XXX: should this use a mutex? not sure... */
    OS_ENTER_CRITICAL(sr);

    timer->cputime = cputime;
    list_for_each_entry(entry, &cputimer_q, link) {
        if ((int32_t)(timer->cputime - entry->cputime) < 0) {
            break;
        }
    }
    list_add_tail(&timer->link, &entry->link);

    /* If this is the head, we need to set new OCMP */
    if (timer == list_first_entry(&cputimer_q, cpu_timer, link)) {
        cputime_set_ocmp(timer);
    }

    OS_EXIT_CRITICAL(sr);
}

/**
 * cputimer timer relative
 *
 * Sets a cpu timer that will expire 'usecs' microseconds from the current
 * cputime.
 *
 * @param timer Pointer to timer. Cannot be NULL.
 * @param usecs The number of usecs from now at which the timer will expire.
 */
void
cputime_timer_relative(struct cpu_timer *timer, uint32_t usecs)
{
    uint32_t cputime;

    assert(timer != NULL);

    cputime = cputime_get32() + cputime_usecs_to_ticks(usecs);
    cputime_timer_start(timer, cputime);
}

/**
 * cputime timer stop
 *
 * Stops a cputimer from running. The timer is removed from the timer queue
 * and interrupts are disabled if no timers are left on the queue. Can be
 * called even if timer is not running.
 *
 * @param timer Pointer to cputimer to stop. Cannot be NULL.
 */
void
cputime_timer_stop(struct cpu_timer *timer)
{
    os_sr_t sr;
    int reset_ocmp;
    struct cpu_timer *entry;

    assert(timer != NULL);

    OS_ENTER_CRITICAL(sr);

    if (timer->link.next != NULL) {
        reset_ocmp = 0;
        if (timer == list_first_entry(&cputimer_q, cpu_timer, link)) {
            /* If first on queue, we will need to reset OCMP */
            entry = list_entry(timer.link.next, cpu_timer, link);
            reset_ocmp = 1;
        }
        list_del(&timer.link);
        if (reset_ocmp) {
            if (entry) {
                cputime_set_ocmp(entry);
            } else {
                cputime_disable_ocmp();
            }
        }
    }

    OS_EXIT_CRITICAL(sr);
}

// Timer even handler.
static void
cputime_event_handler(nrf_timer_event_t event_type, void * p_context)
{
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    switch(event_type) {
        case CPUTIMER_SET_EVENT:
            xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(g_cpu_time_sem, &xHigherPriorityTaskWoken);
            break;
        case CPUTIMER_GET_EVENT:
        default:
            return;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * cputime init
 *
 * Initialize the cputime module. This must be called after os_init is called
 * and before any other timer API are used. This should be called only once
 * and should be called before the hardware timer is used.
 *
 * @param clock_freq The desired cputime frequency, in hertz (Hz).
 *
 * @return int 0 on success; -1 on error.
 */
int
cputime_hw_init(void)
{
    ret_code_t err_code = nrf_drv_timer_init(&cputimer_id, NULL, cputime_event_handler);
    APP_ERROR_CHECK(err_code);

    nrf_drv_timer_enable(&cputimer_id);

    return 0;
}

/* Disable output compare used for cputimer */
void
cputime_disable_ocmp(void)
{
    nrf_drv_timer_compare_int_disable(&cputimer_id, CPUTIMER_SET_CHANNEL);
}

/**
 * cputime set ocmp
 *
 * Set the OCMP used by the cputime module to the desired cputime.
 *
 * NOTE: Must be called with interrupts disabled.
 *
 * @param timer Pointer to timer.
 */
void
cputime_set_ocmp(struct cpu_timer *timer)
{
    /* Disable ocmp interrupt and set output compare register to timer expiration */
    nrf_drv_timer_compare(&cputimer_id, CPUTIMER_SET_CHANNEL, timer->cputime, false);

    /* Clear interrupt flag and enable the output compare interrupt */
    nrf_drv_timer_compare_int_enable(&cputimer_id, CPUTIMER_SET_CHANNEL);

    /* Force interrupt to occur as we may have missed it */
    if ((int32_t)(cputime_get32() - timer->cputime) >= 0) {
        xSemaphoreGive(g_cpu_time_sem);
    }
}

/**
 * cputime get32
 *
 * Returns the low 32 bits of cputime.
 *
 * @return uint32_t The lower 32 bits of cputime
 */
uint32_t
cputime_get32(void)
{
    return nrf_drv_timer_capture(&cputimer_id, CPUTIMER_GET_CHANNEL);
}

