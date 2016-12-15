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

#include "os/os_port.h"

#define OS_SEM_MAX_SIZE                       10

/**
 * Initialize a task.
 *
 * This function initializes the task structure pointed to by t,
 * clearing and setting it's stack pointer, provides sane defaults
 * and sets the task as ready to run, and inserts it into the operating
 * system scheduler.
 *
 * @param task The task to initialize
 * @param name The name of the task to initialize
 * @param func The task function to call
 * @param arg The argument to pass to this task function
 * @param prio The priority at which to run this task
 * @param sanity_itvl The time at which this task should check in with the
 *                    sanity task.  OS_WAIT_FOREVER means never check in
 *                    here.
 * @param stack_bottom A pointer to the bottom of a task's stack
 * @param stack_size The overall size of the task's stack.
 *
 * @return 0 on success, non-zero on failure.
 */
os_error_t
os_task_init(struct os_task *task, char *name, os_task_func_t func, void *arg,
        uint8_t prio, os_time_t sanity_itvl, os_stack_t *stack_bottom,
        uint16_t stack_size)
{
    BaseType_t status;
    os_error_t ret;

    status = xTaskGenericCreate(func, name, stack_size, arg, prio, &task->handle,
                                stack_bottom, NULL);
    switch(status) {
        case pdPASS:
            ret = OS_OK;
            break;
        case errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY:
            ret = OS_ENOMEM;
            break;
        default:
            ret = OS_EINVAL;
            break;
    }

    return ret;
}

/**
 * os sem create
 *  
 * Create a semaphore and initialize it. 
 * 
 * @param sem Pointer to semaphore 
 * @param tokens: # of tokens the semaphore should contain initially.   
 * 
 * @return os_error_t 
 *      OS_INVALID_PARM     Semaphore passed in was NULL.
 *      OS_OK               no error.
 */
os_error_t
os_sem_init(struct os_sem *sem, uint16_t tokens)
{
    sem->handle = xSemaphoreCreateCounting(OS_SEM_MAX_SIZE, tokens);
    return (NULL == sem->handle) ? OS_ENOMEM : OS_OK;
}

/**
 * os sem release
 *  
 * Release a semaphore. 
 * 
 * @param sem Pointer to the semaphore to be released
 * 
 * @return os_error_t 
 *      OS_INVALID_PARM Semaphore passed in was NULL.
 *      OS_OK No error
 */
os_error_t
os_sem_release(struct os_sem *sem)
{
    BaseType_t status;
    os_error_t ret;

    if (NULL == sem->handle) {
        return OS_INVALID_PARM;
    }

    status = xSemaphoreGive(sem->handle);
    switch(status) {
        case pdPASS:
            ret = OS_OK;
            break;
        case errQUEUE_FULL:
            ret = OS_EINVAL;
            break;
        default:
            ret = OS_ENOENT;
            break;
    }

    return ret;
}

/**
 * os sem pend 
 *  
 * Pend (wait) for a semaphore. 
 * 
 * @param mu Pointer to semaphore.
 * @param timeout Timeout, in os ticks. A timeout of 0 means do 
 *                not wait if not available. A timeout of
 *                0xFFFFFFFF means wait forever.
 *              
 * 
 * @return os_error_t 
 *      OS_INVALID_PARM     Semaphore passed in was NULL.
 *      OS_TIMEOUT          Semaphore was owned by another task and timeout=0
 *      OS_OK               no error.
 */ 
os_error_t
os_sem_pend(struct os_sem *sem, uint32_t timeout)
{
    BaseType_t status;
    os_error_t ret;

    if (NULL == sem->handle) {
        return OS_INVALID_PARM;
    }

    status = xSemaphoreTake(sem->handle, timeout);
    switch(status) {
        case pdPASS:
            ret = OS_OK;
            break;
        case errQUEUE_EMPTY:
            ret = OS_TIMEOUT;
            break;
        default:
            ret = OS_ENOENT;
            break;
    }

    return ret;
}

/**
 * os mutex create
 *  
 * Create a mutex and initialize it. 
 * 
 * @param mu Pointer to mutex
 * 
 * @return os_error_t 
 *      OS_INVALID_PARM     Mutex passed in was NULL.
 *      OS_OK               no error.
 */
os_error_t
os_mutex_init(struct os_mutex *mu)
{

}

/**
 * os mutex release
 *  
 * Release a mutex. 
 * 
 * @param mu Pointer to the mutex to be released
 * 
 * @return os_error_t 
 *      OS_INVALID_PARM Mutex passed in was NULL.
 *      OS_BAD_MUTEX    Mutex was not granted to current task (not owner).
 *      OS_OK           No error
 */
os_error_t
os_mutex_release(struct os_mutex *mu)
{

}

/**
 * os mutex pend 
 *  
 * Pend (wait) for a mutex. 
 * 
 * @param mu Pointer to mutex.
 * @param timeout Timeout, in os ticks. A timeout of 0 means do 
 *                not wait if not available. A timeout of
 *                0xFFFFFFFF means wait forever.
 *              
 * 
 * @return os_error_t 
 *      OS_INVALID_PARM     Mutex passed in was NULL.
 *      OS_TIMEOUT          Mutex was owned by another task and timeout=0
 *      OS_OK               no error.
 */ 
os_error_t
os_mutex_pend(struct os_mutex *mu, uint32_t timeout)
{

}

