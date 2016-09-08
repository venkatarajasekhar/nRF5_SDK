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
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"
#include "host/ble_gap.h"
#include "ble_hs_priv.h"
#include "ble_hs_dbg_priv.h"

_Static_assert(sizeof (struct hci_data_hdr) == BLE_HCI_DATA_HDR_SZ,
               "struct hci_data_hdr must be 4 bytes");

typedef int ble_hs_hci_evt_fn(uint8_t event_code, uint8_t *data, int len);
static ble_hs_hci_evt_fn ble_hs_hci_evt_disconn_complete;
static ble_hs_hci_evt_fn ble_hs_hci_evt_encrypt_change;
static ble_hs_hci_evt_fn ble_hs_hci_evt_hw_error;
static ble_hs_hci_evt_fn ble_hs_hci_evt_num_completed_pkts;
static ble_hs_hci_evt_fn ble_hs_hci_evt_enc_key_refresh;
static ble_hs_hci_evt_fn ble_hs_hci_evt_le_meta;

typedef int ble_hs_hci_evt_le_fn(uint8_t subevent, uint8_t *data, int len);
static ble_hs_hci_evt_le_fn ble_hs_hci_evt_le_conn_complete;
static ble_hs_hci_evt_le_fn ble_hs_hci_evt_le_adv_rpt;
static ble_hs_hci_evt_le_fn ble_hs_hci_evt_le_conn_upd_complete;
static ble_hs_hci_evt_le_fn ble_hs_hci_evt_le_lt_key_req;
static ble_hs_hci_evt_le_fn ble_hs_hci_evt_le_conn_parm_req;
static ble_hs_hci_evt_le_fn ble_hs_hci_evt_le_dir_adv_rpt;

/* Statistics */
struct host_hci_stats
{
    uint32_t events_rxd;
    uint32_t good_acks_rxd;
    uint32_t bad_acks_rxd;
    uint32_t unknown_events_rxd;
};

#define BLE_HS_HCI_EVT_TIMEOUT        50      /* Milliseconds. */

/** Dispatch table for incoming HCI events.  Sorted by event code field. */
struct ble_hs_hci_evt_dispatch_entry {
    uint8_t event_code;
    ble_hs_hci_evt_fn *cb;
};

static const struct ble_hs_hci_evt_dispatch_entry ble_hs_hci_evt_dispatch[] = {
    { BLE_HCI_EVCODE_DISCONN_CMP, ble_hs_hci_evt_disconn_complete },
    { BLE_HCI_EVCODE_ENCRYPT_CHG, ble_hs_hci_evt_encrypt_change },
    { BLE_HCI_EVCODE_HW_ERROR, ble_hs_hci_evt_hw_error },
    { BLE_HCI_EVCODE_NUM_COMP_PKTS, ble_hs_hci_evt_num_completed_pkts },
    { BLE_HCI_EVCODE_ENC_KEY_REFRESH, ble_hs_hci_evt_enc_key_refresh },
    { BLE_HCI_EVCODE_LE_META, ble_hs_hci_evt_le_meta },
};

#define BLE_HS_HCI_EVT_DISPATCH_SZ \
    (sizeof ble_hs_hci_evt_dispatch / sizeof ble_hs_hci_evt_dispatch[0])

/** Dispatch table for incoming LE meta events.  Sorted by subevent field. */
struct ble_hs_hci_evt_le_dispatch_entry {
    uint8_t subevent;
    ble_hs_hci_evt_le_fn *cb;
};

static const struct ble_hs_hci_evt_le_dispatch_entry
        ble_hs_hci_evt_le_dispatch[] = {
    { BLE_HCI_LE_SUBEV_CONN_COMPLETE, ble_hs_hci_evt_le_conn_complete },
    { BLE_HCI_LE_SUBEV_ADV_RPT, ble_hs_hci_evt_le_adv_rpt },
    { BLE_HCI_LE_SUBEV_CONN_UPD_COMPLETE,
          ble_hs_hci_evt_le_conn_upd_complete },
    { BLE_HCI_LE_SUBEV_LT_KEY_REQ, ble_hs_hci_evt_le_lt_key_req },
    { BLE_HCI_LE_SUBEV_REM_CONN_PARM_REQ, ble_hs_hci_evt_le_conn_parm_req },
    { BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE, ble_hs_hci_evt_le_conn_complete },
    { BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT, ble_hs_hci_evt_le_dir_adv_rpt },
};

#define BLE_HS_HCI_EVT_LE_DISPATCH_SZ \
    (sizeof ble_hs_hci_evt_le_dispatch / sizeof ble_hs_hci_evt_le_dispatch[0])

static const struct ble_hs_hci_evt_dispatch_entry *
ble_hs_hci_evt_dispatch_find(uint8_t event_code)
{
    const struct ble_hs_hci_evt_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_HS_HCI_EVT_DISPATCH_SZ; i++) {
        entry = ble_hs_hci_evt_dispatch + i;
        if (entry->event_code == event_code) {
            return entry;
        }
    }

    return NULL;
}

static const struct ble_hs_hci_evt_le_dispatch_entry *
ble_hs_hci_evt_le_dispatch_find(uint8_t event_code)
{
    const struct ble_hs_hci_evt_le_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_HS_HCI_EVT_LE_DISPATCH_SZ; i++) {
        entry = ble_hs_hci_evt_le_dispatch + i;
        if (entry->subevent == event_code) {
            return entry;
        }
    }

    return NULL;
}

static int
ble_hs_hci_evt_disconn_complete(uint8_t event_code, uint8_t *data, int len)
{
    struct hci_disconn_complete evt;

    if (len < BLE_HCI_EVENT_DISCONN_COMPLETE_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.status = data[2];
    evt.connection_handle = le16toh(data + 3);
    evt.reason = data[5];

    ble_gap_rx_disconn_complete(&evt);

    return 0;
}

static int
ble_hs_hci_evt_encrypt_change(uint8_t event_code, uint8_t *data, int len)
{
    struct hci_encrypt_change evt;

    if (len < BLE_HCI_EVENT_ENCRYPT_CHG_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.status = data[2];
    evt.connection_handle = le16toh(data + 3);
    evt.encryption_enabled = data[5];

    ble_sm_enc_change_rx(&evt);

    return 0;
}

static int
ble_hs_hci_evt_hw_error(uint8_t event_code, uint8_t *data, int len)
{
    uint8_t hw_code;

    if (len < BLE_HCI_EVENT_HW_ERROR_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    hw_code = data[0];
    ble_hs_hw_error(hw_code);

    return 0;
}

static int
ble_hs_hci_evt_enc_key_refresh(uint8_t event_code, uint8_t *data, int len)
{
    struct hci_encrypt_key_refresh evt;

    if (len < BLE_HCI_EVENT_ENC_KEY_REFRESH_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.status = data[2];
    evt.connection_handle = le16toh(data + 3);

    ble_sm_enc_key_refresh_rx(&evt);

    return 0;
}

static int
ble_hs_hci_evt_num_completed_pkts(uint8_t event_code, uint8_t *data, int len)
{
    uint16_t num_pkts;
    uint16_t handle;
    uint8_t num_handles;
    int off;
    int i;

    if (len < BLE_HCI_EVENT_HDR_LEN + BLE_HCI_EVENT_NUM_COMP_PKTS_HDR_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    off = BLE_HCI_EVENT_HDR_LEN;
    num_handles = data[off];
    if (len < BLE_HCI_EVENT_NUM_COMP_PKTS_HDR_LEN +
              num_handles * BLE_HCI_EVENT_NUM_COMP_PKTS_ENT_LEN) {
        return BLE_HS_ECONTROLLER;
    }
    off++;

    for (i = 0; i < num_handles; i++) {
        handle = le16toh(data + off + 2 * i);
        num_pkts = le16toh(data + off + 2 * num_handles + 2 * i);

        /* XXX: Do something with these values. */
        (void)handle;
        (void)num_pkts;
    }

    return 0;
}

static int
ble_hs_hci_evt_le_meta(uint8_t event_code, uint8_t *data, int len)
{
    const struct ble_hs_hci_evt_le_dispatch_entry *entry;
    uint8_t subevent;
    int rc;

    if (len < BLE_HCI_EVENT_HDR_LEN + BLE_HCI_LE_MIN_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    subevent = data[2];
    entry = ble_hs_hci_evt_le_dispatch_find(subevent);
    if (entry != NULL) {
        rc = entry->cb(subevent, data + BLE_HCI_EVENT_HDR_LEN,
                           len - BLE_HCI_EVENT_HDR_LEN);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static int
ble_hs_hci_evt_le_conn_complete(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_conn_complete evt;
    int extended_offset = 0;
    int rc;

    if (len < BLE_HCI_LE_CONN_COMPLETE_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    /* this code processes two different events that are really similar */
    if ((subevent == BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE) &&
        ( len < BLE_HCI_LE_ENH_CONN_COMPLETE_LEN)) {
        return BLE_HS_ECONTROLLER;
    }

    evt.subevent_code = data[0];
    evt.status = data[1];
    evt.connection_handle = le16toh(data + 2);
    evt.role = data[4];
    evt.peer_addr_type = data[5];
    memcpy(evt.peer_addr, data + 6, BLE_DEV_ADDR_LEN);

    /* enhanced connection event has the same information with these
     * extra fields stuffed into the middle */
    if (subevent == BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE) {
        memcpy(evt.local_rpa, data + 12, BLE_DEV_ADDR_LEN);
        memcpy(evt.peer_rpa, data + 18, BLE_DEV_ADDR_LEN);
        extended_offset = 12;
    } else {
        memset(evt.local_rpa, 0, BLE_DEV_ADDR_LEN);
        memset(evt.peer_rpa, 0, BLE_DEV_ADDR_LEN);
    }

    evt.conn_itvl = le16toh(data + 12 + extended_offset);
    evt.conn_latency = le16toh(data + 14 + extended_offset);
    evt.supervision_timeout = le16toh(data + 16 + extended_offset);
    evt.master_clk_acc = data[18 + extended_offset];

    if (evt.status == 0) {
        if (evt.role != BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER &&
            evt.role != BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE) {

            return BLE_HS_EBADDATA;
        }
    }

    rc = ble_gap_rx_conn_complete(&evt);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_hci_evt_le_adv_rpt_first_pass(uint8_t *data, int len,
                                     uint8_t *out_num_reports,
                                     int *out_rssi_off)
{
    uint8_t num_reports;
    int data_len;
    int off;
    int i;

    if (len < BLE_HCI_LE_ADV_RPT_MIN_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    num_reports = data[1];
    if (num_reports < BLE_HCI_LE_ADV_RPT_NUM_RPTS_MIN ||
        num_reports > BLE_HCI_LE_ADV_RPT_NUM_RPTS_MAX) {

        return BLE_HS_EBADDATA;
    }

    off = 2 +       /* Subevent code and num reports. */
          (1 +      /* Event type. */
           1 +      /* Address type. */
           6        /* Address. */
          ) * num_reports;
    if (off + num_reports >= len) {
        return BLE_HS_ECONTROLLER;
    }

    data_len = 0;
    for (i = 0; i < num_reports; i++) {
        data_len += data[off];
        off++;
    }

    off += data_len;

    /* Check if RSSI fields fit in the packet. */
    if (off + num_reports > len) {
        return BLE_HS_ECONTROLLER;
    }

    *out_num_reports = num_reports;
    *out_rssi_off = off;

    return 0;
}

static int
ble_hs_hci_evt_le_adv_rpt(uint8_t subevent, uint8_t *data, int len)
{
    struct ble_gap_disc_desc desc;
    uint8_t num_reports;
    int rssi_off;
    int data_off;
    int suboff;
    int off;
    int rc;
    int i;

    rc = ble_hs_hci_evt_le_adv_rpt_first_pass(data, len, &num_reports,
                                             &rssi_off);
    if (rc != 0) {
        return rc;
    }

    /* Direct address fields not present in a standard advertising report. */
    desc.direct_addr_type = BLE_GAP_ADDR_TYPE_NONE;
    memset(desc.direct_addr, 0, sizeof desc.direct_addr);

    data_off = 0;
    for (i = 0; i < num_reports; i++) {
        suboff = 0;

        off = 2 + suboff * num_reports + i;
        desc.event_type = data[off];
        suboff++;

        off = 2 + suboff * num_reports + i;
        desc.addr_type = data[off];
        suboff++;

        off = 2 + suboff * num_reports + i * 6;
        memcpy(desc.addr, data + off, 6);
        suboff += 6;

        off = 2 + suboff * num_reports + i;
        desc.length_data = data[off];
        suboff++;

        off = 2 + suboff * num_reports + data_off;
        desc.data = data + off;
        data_off += desc.length_data;

        off = rssi_off + 1 * i;
        desc.rssi = data[off];

        ble_gap_rx_adv_report(&desc);
    }

    return 0;
}

static int
ble_hs_hci_evt_le_dir_adv_rpt(uint8_t subevent, uint8_t *data, int len)
{
    struct ble_gap_disc_desc desc;
    uint8_t num_reports;
    int suboff;
    int off;
    int i;

    if (len < BLE_HCI_LE_ADV_DIRECT_RPT_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    num_reports = data[1];
    if (len != 2 + num_reports * BLE_HCI_LE_ADV_DIRECT_RPT_SUB_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    /* Data fields not present in a direct advertising report. */
    desc.data = NULL;
    desc.fields = NULL;

    for (i = 0; i < num_reports; i++) {
        suboff = 0;

        off = 2 + suboff * num_reports + i;
        desc.event_type = data[off];
        suboff++;

        off = 2 + suboff * num_reports + i;
        desc.addr_type = data[off];
        suboff++;

        off = 2 + suboff * num_reports + i * 6;
        memcpy(desc.addr, data + off, 6);
        suboff += 6;

        off = 2 + suboff * num_reports + i;
        desc.direct_addr_type = data[off];
        suboff++;

        off = 2 + suboff * num_reports + i * 6;
        memcpy(desc.direct_addr, data + off, 6);
        suboff += 6;

        off = 2 + suboff * num_reports + i;
        desc.rssi = data[off];
        suboff++;

        ble_gap_rx_adv_report(&desc);
    }

    return 0;
}

static int
ble_hs_hci_evt_le_conn_upd_complete(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_conn_upd_complete evt;

    if (len < BLE_HCI_LE_CONN_UPD_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.subevent_code = data[0];
    evt.status = data[1];
    evt.connection_handle = le16toh(data + 2);
    evt.conn_itvl = le16toh(data + 4);
    evt.conn_latency = le16toh(data + 6);
    evt.supervision_timeout = le16toh(data + 8);

    if (evt.status == 0) {
        if (evt.conn_itvl < BLE_HCI_CONN_ITVL_MIN ||
            evt.conn_itvl > BLE_HCI_CONN_ITVL_MAX) {

            return BLE_HS_EBADDATA;
        }
        if (evt.conn_latency < BLE_HCI_CONN_LATENCY_MIN ||
            evt.conn_latency > BLE_HCI_CONN_LATENCY_MAX) {

            return BLE_HS_EBADDATA;
        }
        if (evt.supervision_timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN ||
            evt.supervision_timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX) {

            return BLE_HS_EBADDATA;
        }
    }

    ble_gap_rx_update_complete(&evt);

    return 0;
}

static int
ble_hs_hci_evt_le_lt_key_req(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_lt_key_req evt;

    if (len < BLE_HCI_LE_LT_KEY_REQ_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.subevent_code = data[0];
    evt.connection_handle = le16toh(data + 1);
    evt.random_number = le64toh(data + 3);
    evt.encrypted_diversifier = le16toh(data + 11);

    ble_sm_ltk_req_rx(&evt);

    return 0;
}

static int
ble_hs_hci_evt_le_conn_parm_req(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_conn_param_req evt;

    if (len < BLE_HCI_LE_REM_CONN_PARM_REQ_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.subevent_code = data[0];
    evt.connection_handle = le16toh(data + 1);
    evt.itvl_min = le16toh(data + 3);
    evt.itvl_max = le16toh(data + 5);
    evt.latency = le16toh(data + 7);
    evt.timeout = le16toh(data + 9);

    if (evt.itvl_min < BLE_HCI_CONN_ITVL_MIN ||
        evt.itvl_max > BLE_HCI_CONN_ITVL_MAX ||
        evt.itvl_min > evt.itvl_max) {

        return BLE_HS_EBADDATA;
    }
    if (evt.latency < BLE_HCI_CONN_LATENCY_MIN ||
        evt.latency > BLE_HCI_CONN_LATENCY_MAX) {

        return BLE_HS_EBADDATA;
    }
    if (evt.timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN ||
        evt.timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX) {

        return BLE_HS_EBADDATA;
    }

    ble_gap_rx_param_req(&evt);

    return 0;
}

int
ble_hs_hci_evt_process(uint8_t *data)
{
    const struct ble_hs_hci_evt_dispatch_entry *entry;
    uint8_t event_code;
    uint8_t param_len;
    int event_len;
    int rc;

    /* Count events received */
    STATS_INC(ble_hs_stats, hci_event);

    /* Display to console */
    ble_hs_dbg_event_disp(data);

    /* Process the event */
    event_code = data[0];
    param_len = data[1];

    event_len = param_len + 2;

    entry = ble_hs_hci_evt_dispatch_find(event_code);
    if (entry == NULL) {
        STATS_INC(ble_hs_stats, hci_unknown_event);
        rc = BLE_HS_ENOTSUP;
    } else {
        rc = entry->cb(event_code, data, event_len);
    }

    ble_hci_trans_buf_free(data);

    return rc;
}

/**
 * Called when a data packet is received from the controller.  This function
 * consumes the supplied mbuf, regardless of the outcome.
 *
 * @param om                    The incoming data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_hs_hci_evt_acl_process(struct os_mbuf *om)
{
    struct hci_data_hdr hci_hdr;
    struct ble_hs_conn *conn;
    ble_l2cap_rx_fn *rx_cb;
    struct os_mbuf *rx_buf;
    uint16_t handle;
    int rc;

    rc = ble_hs_hci_util_data_hdr_strip(om, &hci_hdr);
    if (rc != 0) {
        goto err;
    }

#if (BLETEST_THROUGHPUT_TEST == 0)
    BLE_HS_LOG(DEBUG, "ble_hs_hci_evt_acl_process(): handle=%u pb=%x len=%u "
                      "data=",
               BLE_HCI_DATA_HANDLE(hci_hdr.hdh_handle_pb_bc), 
               BLE_HCI_DATA_PB(hci_hdr.hdh_handle_pb_bc), 
               hci_hdr.hdh_len);
    ble_hs_log_mbuf(om);
    BLE_HS_LOG(DEBUG, "\n");
#endif

    if (hci_hdr.hdh_len != OS_MBUF_PKTHDR(om)->omp_len) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    handle = BLE_HCI_DATA_HANDLE(hci_hdr.hdh_handle_pb_bc);

    ble_hs_lock();

    conn = ble_hs_conn_find(handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        rc = ble_l2cap_rx(conn, &hci_hdr, om, &rx_cb, &rx_buf);
        om = NULL;
    }

    ble_hs_unlock();

    switch (rc) {
    case 0:
        /* Final fragment received. */
        BLE_HS_DBG_ASSERT(rx_cb != NULL);
        BLE_HS_DBG_ASSERT(rx_buf != NULL);
        rc = rx_cb(handle, &rx_buf);
        os_mbuf_free_chain(rx_buf);
        break;

    case BLE_HS_EAGAIN:
        /* More fragments on the way. */
        break;

    default:
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(om);
    return rc;
}
