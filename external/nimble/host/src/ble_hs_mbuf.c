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

#include "host/ble_hs.h"
#include "ble_hs_priv.h"

/**
 * Allocates an mbuf for use by the nimble host.
 */
static struct os_mbuf *
ble_hs_mbuf_gen_pkt(uint16_t leading_space)
{
    struct os_mbuf *om;
    int rc;

    om = os_msys_get_pkthdr(0, 0);
    if (om == NULL) {
        return NULL;
    }

    if (om->om_omp->omp_databuf_len < leading_space) {
        rc = os_mbuf_free_chain(om);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
        return NULL;
    }

    om->om_data += leading_space;

    return om;
}

/**
 * Allocates an mbuf with no leading space.
 *
 * @return                  An empty mbuf on success; null on memory
 *                              exhaustion.
 */
struct os_mbuf *
ble_hs_mbuf_bare_pkt(void)
{
    return ble_hs_mbuf_gen_pkt(0);
}

/**
 * Allocates an mbuf suitable for an HCI ACM data packet.
 *
 * @return                  An empty mbuf on success; null on memory
 *                              exhaustion.
 */
struct os_mbuf *
ble_hs_mbuf_acm_pkt(void)
{
    return ble_hs_mbuf_gen_pkt(BLE_HCI_DATA_HDR_SZ);
}

/**
 * Allocates an mbuf suitable for an L2CAP data packet.  The resulting packet
 * has sufficient leading space for:
 *     o ACM data header
 *     o L2CAP B-frame header
 *
 * @return                  An empty mbuf on success; null on memory
 *                              exhaustion.
 */
struct os_mbuf *
ble_hs_mbuf_l2cap_pkt(void)
{
    return ble_hs_mbuf_gen_pkt(BLE_HCI_DATA_HDR_SZ + BLE_L2CAP_HDR_SZ);
}

/**
 * Allocates an mbuf suitable for an ATT command packet.  The resulting packet
 * has sufficient leading space for:
 *     o ACM data header
 *     o L2CAP B-frame header
 *     o Largest ATT command base (prepare write request / response).
 *
 * @return                  An empty mbuf on success; null on memory
 *                              exhaustion.
 */
struct os_mbuf *
ble_hs_mbuf_att_pkt(void)
{
    /* Prepare write request and response are the larget ATT commands which
     * contain attribute data.
     */
     return ble_hs_mbuf_gen_pkt(BLE_HCI_DATA_HDR_SZ +
                                BLE_L2CAP_HDR_SZ +
                                BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
}

/**
 * Allocates a an mbuf and fills it with the contents of the specified flat
 * buffer.
 *
 * @param buf                   The flat buffer to copy from.
 * @param len                   The length of the flat buffer.
 *
 * @return                      A newly-allocated mbuf on success;
 *                              NULL on memory exhaustion.
 */
struct os_mbuf *
ble_hs_mbuf_from_flat(const void *buf, uint16_t len)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_att_pkt();
    if (om == NULL) {
        return NULL;
    }

    rc = os_mbuf_copyinto(om, 0, buf, len);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        return NULL;
    }

    return om;
}

/**
 * Copies the contents of an mbuf into the specified flat buffer.  If the flat
 * buffer is too small to contain the mbuf's contents, it is filled to capacity
 * and BLE_HS_EMSGSIZE is returned.
 *
 * @param om                    The mbuf to copy from.
 * @param flat                  The destination flat buffer.
 * @param max_len               The size of the flat buffer.
 * @param out_copy_len          The number of bytes actually copied gets
 *                                  written here.
 *
 * @return                      0 on success;
 *                              BLE_HS_EMSGSIZE if the flat buffer is too small
 *                                  to contain the mbuf's contents;
 *                              A BLE host core return code on unexpected
 *                                  error.
 */
int
ble_hs_mbuf_to_flat(const struct os_mbuf *om, void *flat, uint16_t max_len,
                    uint16_t *out_copy_len)
{
    uint16_t copy_len;
    int rc;

    if (OS_MBUF_PKTLEN(om) <= max_len) {
        copy_len = OS_MBUF_PKTLEN(om);
    } else {
        copy_len = max_len;
    }

    rc = os_mbuf_copydata(om, 0, copy_len, flat);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    if (copy_len > max_len) {
        rc = BLE_HS_EMSGSIZE;
    } else {
        rc = 0;
    }

    if (out_copy_len != NULL) {
        *out_copy_len = copy_len;
    }
    return rc;
}

int
ble_hs_mbuf_pullup_base(struct os_mbuf **om, int base_len)
{
    if (OS_MBUF_PKTLEN(*om) < base_len) {
        return BLE_HS_EBADDATA;
    }

    *om = os_mbuf_pullup(*om, base_len);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    return 0;
}
