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


#include "os/os.h"
#include <string.h>

#define OS_EVENT_QUEUE_MAX_COUNTER       (32)

/**
 * Initialize the event queue
 *
 * @param evq The event queue to initialize
 */
void
os_eventq_init(struct os_eventq *evq)
{
    INIT_LIST_HEAD(evq->evq_hdr);
    evq->evq_sem = xSemaphoreCreateCounting(OS_EVENT_QUEUE_MAX_COUNTER, 0);
}

/**
 * Put an event on the event queue.
 *
 * @param evq The event queue to put an event on 
 * @param ev The event to put on the queue
 */
void
os_eventq_put(struct os_eventq *evq, struct os_event *ev)
{
    os_sr_t sr;
    int event_put = 1;

    OS_ENTER_CRITICAL(sr);

    /* Do not queue if already queued */
    if (OS_EVENT_QUEUED(ev)) {
        event_put = 0;
        goto exit;
    }

    /* Queue the event */
    ev->ev_queued = true;
    list_add_tail(&ev->ev_node, &evq->evq_hdr);

exit:
    OS_EXIT_CRITICAL(sr);
    if(event_put) {
        xSemaphoreGive(evq->evq_sem);
    }
}

/**
 * Pull a single item from an event queue.  This function blocks until there 
 * is an item on the event queue to read.
 *
 * @param evq The event queue to pull an event from
 *
 * @return The event from the queue
 */
struct os_event *
os_eventq_get(struct os_eventq *evq)
{
    struct os_event *ev;
    os_sr_t sr;

    while(pdFALSE == xSemaphoreTake(evq->evq_sem, portMAX_DELAY));

    OS_ENTER_CRITICAL(sr);

    ev = list_first_entry(&evq->evq_hdr, os_event, ev_node);
    list_del(&ev->ev_node);
    ev->ev_queued = false;

    OS_EXIT_CRITICAL(sr);

    return (ev);
}

/**
 * Remove an event from the queue.
 *
 * @param evq The event queue to remove the event from
 * @param ev  The event to remove from the queue
 */
void
os_eventq_remove(struct os_eventq *evq, struct os_event *ev)
{
    os_sr_t sr;
    os_event *ev_next, *ev_cur;
    int event_remove = 0;

    if(pdFALSE == xSemaphoreTake(evq->evq_sem, 0)) {
        return;
    }

    OS_ENTER_CRITICAL(sr);

    if (!OS_EVENT_QUEUED(ev)) {
        goto exit;
    }

    list_for_each_entry_safe(ev_cur, ev_next, &evq->evq_hdr, ev_node) {
        if(ev == ev_cur) {
            list_del(&ev->ev_node);
            ev->ev_queued = false;
            event_remove = 1;
            break;
        }
    }
    
exit:
    OS_EXIT_CRITICAL(sr);
    if(!event_remove) {
        xSemaphoreGive(evq->evq_sem);
    }
}
