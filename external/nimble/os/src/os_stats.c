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

#include <os/os.h>

#include <string.h>

#include "os/os_stats.h"

#include <stdio.h>

struct stats_os_stats {
    struct stats_hdr s_hdr;
    STATS_SECT_ENTRY(num_registered)
};

struct stats_os_stats STATS_VARIABLE(os_stats);

struct stats_name_map STATS_NAME_MAP_NAME(os_stats)[] = {
    STATS_NAME(os_stats, num_registered)
};

struct list_head stats_registry = LIST_HEAD_INIT(stats_registry);

static uint8_t stats_module_inited = FALSE;

os_error_t
stats_walk(struct stats_hdr *hdr, stats_walk_func_t walk_func, void *arg)
{
    char *name;
    char name_buf[12];
    uint16_t cur;
    uint16_t end;
    int ent_n;
    int len;
    int rc;
#ifdef STATS_NAME_ENABLE
    int i;
#endif

    cur = sizeof(*hdr);
    end = sizeof(*hdr) + (hdr->s_size * hdr->s_cnt);

    while (cur < end) {
        /*
         * Access and display the statistic name.  Pass that to the
         * walk function
         */
        name = NULL;
#ifdef STATS_NAME_ENABLE
        for (i = 0; i < hdr->s_map_cnt; ++i) {
            if (hdr->s_map[i].snm_off == cur) {
                name = hdr->s_map[i].snm_name;
                break;
            }
        }
#endif
        if (name == NULL) {
            ent_n = (cur - sizeof(*hdr)) / hdr->s_size;
            len = snprintf(name_buf, sizeof(name_buf), "s%d", ent_n);
            name_buf[len] = '\0';
            name = name_buf;
        }

        rc = walk_func(hdr, arg, name, cur);
        if (rc != OS_OK) {
            goto err;
        }

        cur += hdr->s_size;
    }

    return (OS_OK);
err:
    return (rc);
}


os_error_t
stats_module_init(void)
{
    int rc;

    if (stats_module_inited) {
        return OS_OK;
    }
    stats_module_inited = TRUE;

#ifdef SHELL_PRESENT
    rc = stats_shell_register();
    if (rc != OS_OK) {
        goto err;
    }
#endif

#ifdef NEWTMGR_PRESENT
    rc = stats_nmgr_register_group();
    if (rc != OS_OK) {
        goto err;
    }
#endif

    rc = stats_init(STATS_HDR(os_stats),
                    STATS_SIZE_INIT_PARMS(os_stats, STATS_SIZE_32),
                    STATS_NAME_INIT_PARMS(os_stats));
    if (rc != OS_OK) {
        goto err;
    }

    rc = stats_register("os_stats", STATS_HDR(os_stats));
    if (rc != OS_OK) {
        goto err;
    }

    return (OS_OK);
err:
    return (rc);
}

/**
 * Uninitializes all statistic sections.  This is likely only useful for unit
 * tests that need to run in sequence.
 */
void
stats_module_reset(void)
{
    stats_module_inited = FALSE;

    INIT_LIST_HEAD(&stats_registry);
}

os_error_t
stats_init(struct stats_hdr *shdr, uint8_t size, uint8_t cnt,
        struct stats_name_map *map, uint8_t map_cnt)
{
    memset((uint8_t *) shdr, 0, sizeof(*shdr) + (size * cnt));

    shdr->s_size = size;
    shdr->s_cnt = cnt;
#ifdef STATS_NAME_ENABLE
    shdr->s_map = map;
    shdr->s_map_cnt = map_cnt;
#endif

    return (OS_OK);
}

os_error_t
stats_group_walk(stats_group_walk_func_t walk_func, void *arg)
{
    struct stats_hdr *hdr;
    int rc;

    list_for_each_entry(hdr, &stats_registry, s_node) {
        rc = walk_func(hdr, arg);
        if (rc != OS_OK) {
            goto err;
        }
    }
    return (OS_OK);
err:
    return (rc);
}

struct stats_hdr *
stats_group_find(char *name)
{
    struct stats_hdr *cur;

    list_for_each_entry(cur, &stats_registry, s_node) {
        if (!strcmp(cur->s_name, name)) {
            break;
        }
    }

    return (&cur->s_node == &stats_registry) ? NULL : cur;
}

os_error_t
stats_register(char *name, struct stats_hdr *shdr)
{
    struct stats_hdr *cur;
    int rc;

    /* Don't allow duplicate entries, return an error if this stat
     * is already registered.
     */
    list_for_each_entry(cur, &stats_registry, s_node) {
        if (!strcmp(cur->s_name, name)) {
            rc = OS_EINVAL;
            goto err;
        }
    }

    shdr->s_name = name;

    list_add_tail(&shdr->s_node, &stats_registry);

    STATS_INC(os_stats, num_registered);

    return (OS_OK);
err:
    return (rc);
}

/**
 * Initializes and registers the specified statistics section.
 */
os_error_t
stats_init_and_reg(struct stats_hdr *shdr, uint8_t size, uint8_t cnt,
                   struct stats_name_map *map, uint8_t map_cnt, char *name)
{
    int rc;

    rc = stats_init(shdr, size, cnt, map, map_cnt);
    if (rc != OS_OK) {
        return rc;
    }

    rc = stats_register(name, shdr);
    if (rc != OS_OK) {
        return rc;
    }

    return rc;
}
