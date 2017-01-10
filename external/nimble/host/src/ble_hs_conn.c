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
#include <errno.h>
#include "os/os.h"
#include "host/ble_hs_id.h"
#include "ble_hs_priv.h"

/** At least three channels required per connection (sig, att, sm). */
#define BLE_HS_CONN_MIN_CHANS                     3

static struct list_head ble_hs_conns;
static struct os_mempool ble_hs_conn_pool;

static os_membuf_t *ble_hs_conn_elem_mem = NULL;

static const uint8_t ble_hs_conn_null_addr[BLE_DEV_ADDR_LEN] = {0};

int
ble_hs_conn_can_alloc(void)
{
#if NIMBLE_OPT(CONNECT)
    return ble_hs_conn_pool.mp_num_free > 0 &&
           ble_l2cap_chan_pool.mp_num_free >= BLE_HS_CONN_MIN_CHANS &&
           ble_gatts_conn_can_alloc();
#else
    return FALSE;
#endif
}

struct ble_l2cap_chan *
ble_hs_conn_chan_find(struct ble_hs_conn *conn, uint16_t cid)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_l2cap_chan *chan;

    list_for_each_entry(chan, &conn->bhc_channels.blc_hdr, blc_node) {
        if (chan->blc_cid == cid) {
            return chan;
        }
        if (chan->blc_cid > cid) {
            break;
        }
    }
#endif
    return NULL;
}

int
ble_hs_conn_chan_insert(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_l2cap_chan *cur;

    list_for_each_entry(cur, &conn->bhc_channels.blc_hdr, blc_node) {
        if (cur->blc_cid == chan->blc_cid) {
            return BLE_HS_EALREADY;
        }
        if (cur->blc_cid > chan->blc_cid) {
            break;
        }
    }

    list_add_tail(&chan->blc_node, &cur->blc_node);

    return BLE_HS_ENONE;
#else
    return BLE_HS_ENOTSUP;
#endif
}

struct ble_hs_conn *
ble_hs_conn_alloc(void)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    conn = os_memblock_get(&ble_hs_conn_pool);
    if (conn == NULL) {
        goto err;
    }
    memset(conn, 0, sizeof *conn);

    INIT_LIST_HEAD(&conn->bhc_channels.blc_hdr);

    chan = ble_att_create_chan();
    if (chan == NULL) {
        goto err;
    }
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != BLE_HS_ENONE) {
        goto err;
    }

    chan = ble_l2cap_sig_create_chan();
    if (chan == NULL) {
        goto err;
    }
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != BLE_HS_ENONE) {
        goto err;
    }

    /* XXX: We should create the SM channel even if not configured.  We need it
     * to reject SM messages.
     */
#if NIMBLE_OPT(SM)
    chan = ble_sm_create_chan();
    if (chan == NULL) {
        goto err;
    }
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != BLE_HS_ENONE) {
        goto err;
    }
#endif

    rc = ble_gatts_conn_init(&conn->bhc_gatt_svr);
    if (rc != BLE_HS_ENONE) {
        goto err;
    }

    STATS_INC(ble_hs_stats, conn_create);

    return conn;

err:
    ble_hs_conn_free(conn);
#endif

    return NULL;
}

static void
ble_hs_conn_delete_chan(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    if (conn->bhc_rx_chan == chan) {
        conn->bhc_rx_chan = NULL;
    }

    list_del(&chan->blc_node);
    ble_l2cap_chan_free(chan);
}

void
ble_hs_conn_free(struct ble_hs_conn *conn)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_l2cap_chan *chan;
    int rc;

    if (conn == NULL) {
        return;
    }

    ble_att_svr_prep_clear(&conn->bhc_att_svr.basc_prep_list);

    while (!list_empty(&conn->bhc_channels.blc_hdr)) {
        chan = list_first_entry(&conn->bhc_channels.blc_hdr, ble_l2cap_chan, blc_node);
        ble_hs_conn_delete_chan(conn, chan);
    }

    rc = os_memblock_put(&ble_hs_conn_pool, conn);
    BLE_HS_DBG_ASSERT_EVAL(rc == OS_OK);

    STATS_INC(ble_hs_stats, conn_delete);
#endif
}

void
ble_hs_conn_insert(struct ble_hs_conn *conn)
{
#if NIMBLE_OPT(CONNECT)
    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    BLE_HS_DBG_ASSERT_EVAL(ble_hs_conn_find(conn->bhc_handle) == NULL);
    list_add(&conn->bhc_node, &ble_hs_conns);
#endif
}

void
ble_hs_conn_remove(struct ble_hs_conn *conn)
{
#if NIMBLE_OPT(CONNECT)
    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    list_del(&conn->bhc_node);
#endif
}

struct ble_hs_conn *
ble_hs_conn_find(uint16_t conn_handle)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    list_for_each_entry(conn, &ble_hs_conns, bhc_node) {
        if (conn->bhc_handle == conn_handle) {
            return conn;
        }
    }
#endif
    return NULL;
}

struct ble_hs_conn *
ble_hs_conn_find_assert(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(conn_handle);
    BLE_HS_DBG_ASSERT(conn != NULL);

    return conn;
}

struct ble_hs_conn *
ble_hs_conn_find_by_addr(uint8_t addr_type, uint8_t *addr)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    list_for_each_entry(conn, &ble_hs_conns, bhc_node) {
        if (conn->bhc_peer_addr_type == addr_type &&
            memcmp(conn->bhc_peer_addr, addr, BLE_DEV_ADDR_LEN) == 0) {

            return conn;
        }
    }
#endif
    return NULL;
}

struct ble_hs_conn *
ble_hs_conn_find_by_idx(int idx)
{
#if NIMBLE_OPT(CONNECT)
    struct ble_hs_conn *conn;
    int num = 0;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    list_for_each_entry(conn, &ble_hs_conns, bhc_node) {
        if (num++ == idx) {
            return conn;
        }
    }
#endif
    return NULL;
}

int
ble_hs_conn_exists(uint16_t conn_handle)
{
#if NIMBLE_OPT(CONNECT)
    return ble_hs_conn_find(conn_handle) != NULL;
#else
    return FALSE;
#endif
}

/**
 * Retrieves the first connection in the list.
 */
struct ble_hs_conn *
ble_hs_conn_first(void)
{
#if NIMBLE_OPT(CONNECT)
    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());
    return list_first_entry(&ble_hs_conns, ble_hs_conn, bhc_node);
#else
    return NULL;
#endif
}

void
ble_hs_conn_addrs(const struct ble_hs_conn *conn,
                  struct ble_hs_conn_addrs *addrs)
{
    int rc;

    /* Determine our address information. */
    addrs->our_id_addr_type =
        ble_hs_misc_addr_type_to_id(conn->bhc_our_addr_type);
    rc = ble_hs_id_addr(addrs->our_id_addr_type, &addrs->our_id_addr, NULL);
    assert(rc == BLE_HS_ENONE);

    if (memcmp(conn->bhc_our_rpa_addr, ble_hs_conn_null_addr, BLE_DEV_ADDR_LEN)) {
        addrs->our_ota_addr_type = conn->bhc_our_addr_type;
        addrs->our_ota_addr = conn->bhc_our_rpa_addr;
    } else {
        addrs->our_ota_addr_type = addrs->our_id_addr_type;
        addrs->our_ota_addr = addrs->our_id_addr;
    }

    /* Determine peer address information. */
    addrs->peer_ota_addr_type = conn->bhc_peer_addr_type;
    addrs->peer_id_addr = conn->bhc_peer_addr;
    switch (conn->bhc_peer_addr_type) {
    case BLE_ADDR_TYPE_PUBLIC:
        addrs->peer_id_addr_type = BLE_ADDR_TYPE_PUBLIC;
        addrs->peer_ota_addr = conn->bhc_peer_addr;
        break;

    case BLE_ADDR_TYPE_RANDOM:
        addrs->peer_id_addr_type = BLE_ADDR_TYPE_RANDOM;
        addrs->peer_ota_addr = conn->bhc_peer_addr;
        break;

    case BLE_ADDR_TYPE_RPA_PUB_DEFAULT:
        addrs->peer_id_addr_type = BLE_ADDR_TYPE_PUBLIC;
        addrs->peer_ota_addr = conn->bhc_peer_rpa_addr;
        break;

    case BLE_ADDR_TYPE_RPA_RND_DEFAULT:
        addrs->peer_id_addr_type = BLE_ADDR_TYPE_RANDOM;
        addrs->peer_ota_addr = conn->bhc_peer_rpa_addr;
        break;

    default:
        BLE_HS_DBG_ASSERT(FALSE);
        break;
    }
}

static void
ble_hs_conn_free_mem(void)
{
    if (ble_hs_conn_elem_mem) {
        os_free(ble_hs_conn_elem_mem);
        ble_hs_conn_elem_mem = NULL;
    }
}

int 
ble_hs_conn_init(void)
{
    int rc;

    ble_hs_conn_free_mem();

    ble_hs_conn_elem_mem = os_malloc(
        OS_MEMPOOL_BYTES(g_ble_hs_cfg.max_connections,
                         sizeof (struct ble_hs_conn)));
    if (ble_hs_conn_elem_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    rc = os_mempool_init(&ble_hs_conn_pool, g_ble_hs_cfg.max_connections,
                         sizeof (struct ble_hs_conn),
                         ble_hs_conn_elem_mem, "ble_hs_conn_pool");
    if (rc != OS_OK) {
        rc = BLE_HS_EOS;
        goto err;
    }

    INIT_LIST_HEAD(&ble_hs_conns);

    return BLE_HS_ENONE;

err:
    ble_hs_conn_free_mem();
    return rc;
}
