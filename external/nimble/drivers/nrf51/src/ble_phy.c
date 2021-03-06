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
#include <assert.h>
#include "os/os.h"
#include "bsp/cmsis_nvic.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "mcu/nrf51_bitfields.h"

/* XXX: 4) Make sure RF is higher priority interrupt than schedule */

/*
 * XXX: Maximum possible transmit time is 1 msec for a 60ppm crystal
 * and 16ms for a 30ppm crystal! We need to limit PDU size based on
 * crystal accuracy. Look at this in the spec.
 */

/* XXX: private header file? */
extern uint8_t g_nrf_num_irks;
extern uint32_t g_nrf_irk_list[];

/* To disable all radio interrupts */
#define NRF_RADIO_IRQ_MASK_ALL  ((RADIO_INTENCLR_READY_Clear << RADIO_INTENCLR_READY_Pos)       | \
                                 (RADIO_INTENCLR_ADDRESS_Clear << RADIO_INTENCLR_ADDRESS_Pos)   | \
                                 (RADIO_INTENCLR_PAYLOAD_Clear << RADIO_INTENCLR_PAYLOAD_Pos)   | \
                                 (RADIO_INTENCLR_END_Clear << RADIO_INTENCLR_END_Pos)           | \
                                 (RADIO_INTENCLR_DISABLED_Clear << RADIO_INTENCLR_DISABLED_Pos) | \
                                 (RADIO_INTENCLR_DEVMATCH_Clear << RADIO_INTENCLR_DEVMATCH_Pos) | \
                                 (RADIO_INTENCLR_DEVMISS_Clear << RADIO_INTENCLR_DEVMISS_Pos)   | \
                                 (RADIO_INTENCLR_RSSIEND_Clear << RADIO_INTENCLR_RSSIEND_Pos)   | \
                                 (RADIO_INTENCLR_BCMATCH_Clear << RADIO_INTENCLR_BCMATCH_Pos))

/* To enable all radio shorts */
#define NRF_RADIO_SHORTS_Enable ((RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos)             | \
                                 (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos)             | \
                                 (RADIO_SHORTS_DISABLED_TXEN_Enabled << RADIO_SHORTS_DISABLED_TXEN_Pos)         | \
                                 (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos)         | \
                                 (RADIO_SHORTS_ADDRESS_RSSISTART_Enabled << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) | \
                                 (RADIO_SHORTS_END_START_Enabled << RADIO_SHORTS_END_START_Pos)                 | \
                                 (RADIO_SHORTS_ADDRESS_BCSTART_Enabled << RADIO_SHORTS_ADDRESS_BCSTART_Pos)     | \
                                 (RADIO_SHORTS_DISABLED_RSSISTOP_Enabled << RADIO_SHORTS_DISABLED_RSSISTOP_Pos))

/* To disable all radio shorts */
#define NRF_RADIO_SHORTS_Disable (0UL)

/*
 * We configure the nrf with a 1 byte S0 field, 8 bits length field, and
 * 0 bit S1 field in normal mode. The preamble is 8 bits long.
 */
#define NRF_LFLEN_Normal        (8UL)
#define NRF_S0LEN_Normal        (1UL)
#define NRF_S1LEN_Normal        (0UL)

/*
 * We configure the nrf with a 1 byte S0 field, 5 bits length field, and
 * 3 bits S1 field in encrypt mode. The preamble is 8 bits long.
 */
#define NRF_LFLEN_Encrypt       (5UL)
#define NRF_S0LEN_Encrypt       (1UL)
#define NRF_S1LEN_Encrypt       (3UL)

/* Clear all events */
#define NRF_EVENTS_Clear        (0UL)

/* Trigger task to run */
#define NRF_TASK_Trigger        (1UL)

/* Set bit counter compare register */
#define NRF_BCC_BITS(x)         ((uint32_t)(x) << 3)

/* Maximum length of frames */
#define NRF_MAXLEN              (37)
#define NRF_STATLEN             (0)
#define NRF_BALEN               (3)     /* For base address of 3 bytes */
#define NRF_RX_START_OFFSET     (5)

/* Access address */
#define NRF_BASE(addr)          ((uint32_t)addr << 8)
#define NRF_PREFIX(addr,offset) (((uint32_t)addr & 0xFF000000) >> offset)
#define NRF_AP0_OFFSET          (24)
#define NRF_AP1_OFFSET          (16)

/* Transmit address select */
#define NRF_TXADDRESS(x)        (x & RADIO_TXADDRESS_TXADDRESS_Msk)

/* Set AAR number of identity root keys */
#define NRF_NIRK_Number(x)      (x & AAR_NIRK_NIRK_Msk)

/* Radio channel frequency */
#define NRF_FREQUENCY(freq)     (((freq) - 2400) & RADIO_FREQUENCY_FREQUENCY_Msk)

/* Maximum tx power */
#define NRF_TX_PWR_MAX_DBM      (4)
#define NRF_TX_PWR_MIN_DBM      (-40)

/* Max. encrypted payload length */
#define NRF_MAX_ENCRYPTED_PYLD_LEN  (27)
#define NRF_ENC_HDR_SIZE            (3)
#define NRF_ENC_BUF_SIZE            \
    (NRF_MAX_ENCRYPTED_PYLD_LEN + NRF_ENC_HDR_SIZE + BLE_LL_DATA_MIC_LEN)

/* BLE PHY data structure */
struct ble_phy_obj
{
    uint8_t phy_stats_initialized;
    int8_t  phy_txpwr_dbm;
    uint8_t phy_chan;
    uint8_t phy_state;
    uint8_t phy_transition;
    uint8_t phy_rx_started;
    uint8_t phy_encrypted;
    uint8_t phy_privacy;
    uint8_t phy_tx_pyld_len;
    uint8_t *rxdptr;
    uint32_t phy_aar_scratch;
    uint32_t phy_access_address;
    struct ble_mbuf_hdr rxhdr;
    void *txend_arg;
    ble_phy_tx_end_func txend_cb;
};
struct ble_phy_obj g_ble_phy_data;

/* XXX: if 27 byte packets desired we can make this smaller */
/* Global transmit/receive buffer */
static uint32_t g_ble_phy_tx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];
static uint32_t g_ble_phy_rx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
/* Make sure word-aligned for faster copies */
static uint32_t g_ble_phy_enc_buf[(NRF_ENC_BUF_SIZE + 3) / 4];
#endif

/* Statistics */
struct stats_ble_phy_stats {
    struct stats_hdr s_hdr;
    STATS_SECT_ENTRY(phy_isrs)
    STATS_SECT_ENTRY(tx_good)
    STATS_SECT_ENTRY(tx_fail)
    STATS_SECT_ENTRY(tx_late)
    STATS_SECT_ENTRY(tx_bytes)
    STATS_SECT_ENTRY(rx_starts)
    STATS_SECT_ENTRY(rx_aborts)
    STATS_SECT_ENTRY(rx_valid)
    STATS_SECT_ENTRY(rx_crc_err)
    STATS_SECT_ENTRY(rx_late)
    STATS_SECT_ENTRY(radio_state_errs)
    STATS_SECT_ENTRY(rx_hw_err)
    STATS_SECT_ENTRY(tx_hw_err)
};
struct stats_ble_phy_stats STATS_VARIABLE(ble_phy_stats);

struct stats_name_map STATS_NAME_MAP_NAME(ble_phy_stats)[] = {
    STATS_NAME(ble_phy_stats, phy_isrs)
    STATS_NAME(ble_phy_stats, tx_good)
    STATS_NAME(ble_phy_stats, tx_fail)
    STATS_NAME(ble_phy_stats, tx_late)
    STATS_NAME(ble_phy_stats, tx_bytes)
    STATS_NAME(ble_phy_stats, rx_starts)
    STATS_NAME(ble_phy_stats, rx_aborts)
    STATS_NAME(ble_phy_stats, rx_valid)
    STATS_NAME(ble_phy_stats, rx_crc_err)
    STATS_NAME(ble_phy_stats, rx_late)
    STATS_NAME(ble_phy_stats, radio_state_errs)
    STATS_NAME(ble_phy_stats, rx_hw_err)
    STATS_NAME(ble_phy_stats, tx_hw_err)
};

/*
 * NOTE:
 * Tested the following to see what would happen:
 *  -> NVIC has radio irq enabled (interrupt # 1, mask 0x2).
 *  -> Set up nrf to receive. Clear ADDRESS event register.
 *  -> Enable ADDRESS interrupt on nrf5 by writing to INTENSET.
 *  -> Enable RX.
 *  -> Disable interrupts globally using OS_ENTER_CRITICAL().
 *  -> Wait until a packet is received and the ADDRESS event occurs.
 *  -> Call ble_phy_disable().
 *
 *  At this point I wanted to see the state of the cortex NVIC. The IRQ
 *  pending bit was TRUE for the radio interrupt (as expected) as we never
 *  serviced the radio interrupt (interrupts were disabled).
 *
 *  What was unexpected was this: without clearing the pending IRQ in the NVIC,
 *  when radio interrupts were re-enabled (address event bit in INTENSET set to
 *  1) and the radio ADDRESS event register read 1 (it was never cleared after
 *  the first address event), the radio did not enter the ISR! I would have
 *  expected that if the following were true, an interrupt would occur:
 *      -> NVIC ISER bit set to TRUE
 *      -> NVIC ISPR bit reads TRUE, meaning interrupt is pending.
 *      -> Radio peripheral interrupts are enabled for some event (or events).
 *      -> Corresponding event register(s) in radio peripheral read 1.
 *
 *  Not sure what the end result of all this is. We will clear the pending
 *  bit in the NVIC just to be sure when we disable the PHY.
 */

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
/* XXX: test this. only needs 43 bytes. Should just not use the macro for this*/
/* Per nordic, the number of bytes needed for scratch is 16 + MAX_PKT_SIZE */
#define NRF_ENC_SCRATCH_WORDS   (((NIMBLE_OPT_LL_MAX_PKT_SIZE + 16) + 3) / 4)

uint32_t g_nrf_encrypt_scratchpad[NRF_ENC_SCRATCH_WORDS];

struct nrf_ccm_data
{
    uint8_t key[16];
    uint64_t pkt_counter;
    uint8_t dir_bit;
    uint8_t iv[8];
} __attribute__((packed));

struct nrf_ccm_data g_nrf_ccm_data;
#endif

/**
 * Copies the data from the phy receive buffer into a mbuf chain.
 *
 * @param dptr Pointer to receive buffer
 * @param rxpdu Pointer to already allocated mbuf chain
 *
 * NOTE: the packet header already has the total mbuf length in it. The
 * lengths of the individual mbufs are not set prior to calling.
 *
 */
void
ble_phy_rxpdu_copy(uint8_t *dptr, struct os_mbuf *rxpdu)
{
    uint16_t rem_bytes;
    uint16_t mb_bytes;
    uint16_t copylen;
    uint32_t *dst;
    uint32_t *src;
    struct os_mbuf *m;
    struct ble_mbuf_hdr *ble_hdr;
    struct os_mbuf_pkthdr *pkthdr;

    /* Better be aligned */
    assert(((uint32_t)dptr & 3) == 0);

    pkthdr = OS_MBUF_PKTHDR(rxpdu);
    rem_bytes = pkthdr->omp_len;

    /* Fill in the mbuf pkthdr first. */
    dst = (uint32_t *)(rxpdu->om_data);
    src = (uint32_t *)dptr;

    mb_bytes = (rxpdu->om_omp->omp_databuf_len - rxpdu->om_pkthdr_len - 4);
    copylen = min(mb_bytes, rem_bytes);
    copylen &= 0xFFFC;
    rem_bytes -= copylen;
    mb_bytes -= copylen;
    rxpdu->om_len = copylen;
    while (copylen > 0) {
        *dst = *src;
        ++dst;
        ++src;
        copylen -= 4;
    }

    /* Copy remaining bytes */
    m = rxpdu;
    while (rem_bytes > 0) {
        /* If there are enough bytes in the mbuf, copy them and leave */
        if (rem_bytes <= mb_bytes) {
            memcpy(m->om_data + m->om_len, src, rem_bytes);
            m->om_len += rem_bytes;
            break;
        }

        m = SLIST_NEXT(m, om_next);
        assert(m != NULL);

        mb_bytes = m->om_omp->omp_databuf_len;
        copylen = min(mb_bytes, rem_bytes);
        copylen &= 0xFFFC;
        rem_bytes -= copylen;
        mb_bytes -= copylen;
        m->om_len = copylen;
        dst = (uint32_t *)m->om_data;
        while (copylen > 0) {
            *dst = *src;
            ++dst;
            ++src;
            copylen -= 4;
        }
    }

    /* Copy ble header */
    ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
    memcpy(ble_hdr, &g_ble_phy_data.rxhdr, sizeof(struct ble_mbuf_hdr));
}

/**
 * Called when we want to wait if the radio is in either the rx or tx
 * disable states. We want to wait until that state is over before doing
 * anything to the radio
 */
static void
nrf_wait_disabled(void)
{
    uint32_t state;

    state = NRF_RADIO->STATE;
    if (state != RADIO_STATE_STATE_Disabled) {
        if ((state == RADIO_STATE_STATE_RxDisable) ||
            (state == RADIO_STATE_STATE_TxDisable)) {
            /* This will end within a short time (6 usecs). Just poll */
            while (NRF_RADIO->STATE == state) {
                /* If this fails, something is really wrong. Should last
                 * no more than 6 usecs */
            }
        }
    }
}

/**
 * Setup transceiver for receive.
 */
static void
ble_phy_rx_xcvr_setup(void)
{
    uint8_t *dptr;

    dptr = (uint8_t *)&g_ble_phy_rx_buf[0];

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
    if (g_ble_phy_data.phy_encrypted) {
        dptr += 3;
        NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_enc_buf[0];
        NRF_CCM->INPTR = (uint32_t)&g_ble_phy_enc_buf[0];
        NRF_CCM->OUTPTR = (uint32_t)dptr;
        NRF_CCM->SCRATCHPTR = (uint32_t)&g_nrf_encrypt_scratchpad[0];
        NRF_CCM->MODE = CCM_MODE_MODE_Decryption << CCM_MODE_MODE_Pos;
        NRF_CCM->CNFPTR = (uint32_t)&g_nrf_ccm_data;
        NRF_CCM->SHORTS = CCM_SHORTS_ENDKSGEN_CRYPT_Disabled << CCM_SHORTS_ENDKSGEN_CRYPT_Pos;
        NRF_CCM->EVENTS_ERROR = NRF_EVENTS_Clear;
        NRF_CCM->EVENTS_ENDCRYPT = NRF_EVENTS_Clear;
        nrf_ppi_channels_disable(PPI_CHENCLR_CH24_Msk | PPI_CHENCLR_CH25_Msk);
    } else {
        NRF_RADIO->PACKETPTR = (uint32_t)dptr;
    }
#else
    NRF_RADIO->PACKETPTR = (uint32_t)dptr;
#endif

#if (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
    if (g_ble_phy_data.phy_privacy) {
        dptr += 3;
        NRF_RADIO->PACKETPTR = (uint32_t)dptr;
        NRF_RADIO->PCNF0 = (NRF_LFLEN_Encrypt << RADIO_PCNF0_LFLEN_Pos) |
                           (NRF_S0LEN_Encrypt << RADIO_PCNF0_S0LEN_Pos) |
                           (NRF_S1LEN_Encrypt << RADIO_PCNF0_S1LEN_Pos);
        NRF_AAR->ENABLE = AAR_ENABLE_ENABLE_Enabled << AAR_ENABLE_ENABLE_Pos;
        NRF_AAR->IRKPTR = (uint32_t)&g_nrf_irk_list[0];
        NRF_AAR->ADDRPTR = (uint32_t)dptr;
        NRF_AAR->SCRATCHPTR = (uint32_t)&g_ble_phy_data.phy_aar_scratch;
        NRF_AAR->EVENTS_END = NRF_EVENTS_Clear;
        NRF_AAR->EVENTS_RESOLVED = NRF_EVENTS_Clear;
        NRF_AAR->EVENTS_NOTRESOLVED = NRF_EVENTS_Clear;
    } else {
        NRF_RADIO->PCNF0 = (NRF_LFLEN_Normal << RADIO_PCNF0_LFLEN_Pos) |
                           (NRF_S0LEN_Normal << RADIO_PCNF0_S0LEN_Pos) |
                           (NRF_S1LEN_Normal << RADIO_PCNF0_S1LEN_Pos);
        /* XXX: do I only need to do this once? Figure out what I can do
           once. */
        NRF_AAR->ENABLE = AAR_ENABLE_ENABLE_Disabled << AAR_ENABLE_ENABLE_Pos;
    }
#endif

    /* Turn off trigger TXEN on output compare match and AAR on bcmatch */
    nrf_ppi_channels_disable(PPI_CHENCLR_CH20_Msk | PPI_CHENCLR_CH23_Msk);

    /* Reset the rx started flag. Used for the wait for response */
    g_ble_phy_data.phy_rx_started = FALSE;
    g_ble_phy_data.phy_state = BLE_PHY_STATE_RX;
    g_ble_phy_data.rxdptr = dptr;

    /* I want to know when 1st byte received (after address) */
    NRF_RADIO->BCC = NRF_BCC_BITS(1); /* in bits */
    NRF_RADIO->EVENTS_ADDRESS = NRF_EVENTS_Clear;
    NRF_RADIO->EVENTS_DEVMATCH = NRF_EVENTS_Clear;
    NRF_RADIO->EVENTS_BCMATCH = NRF_EVENTS_Clear;
    NRF_RADIO->EVENTS_RSSIEND = NRF_EVENTS_Clear;
    NRF_RADIO->SHORTS = NRF_RADIO_SHORTS_Enable;
    NRF_RADIO->INTENSET = RADIO_INTENSET_ADDRESS_Set << RADIO_INTENSET_ADDRESS_Pos;
}

/**
 * Called from interrupt context when the transmit ends
 *
 */
static void
ble_phy_tx_end_isr(void)
{
    uint8_t was_encrypted;
    uint8_t transition;
    uint8_t txlen;
    uint32_t wfr_time;
    uint32_t txstart;

    /*
     * Read captured tx start time. This is not the actual transmit start
     * time but it is the time at which the address event occurred
     * (after transmission of access address)
     */
    txstart = cputime_get32();

    /* If this transmission was encrypted we need to remember it */
    was_encrypted = g_ble_phy_data.phy_encrypted;

    /* Better be in TX state! */
    assert(g_ble_phy_data.phy_state == BLE_PHY_STATE_TX);

    /* Log the event */
    ble_ll_log(BLE_LL_LOG_ID_PHY_TXEND, (g_ble_phy_tx_buf[0] >> 8) & 0xFF,
               was_encrypted, txstart);

    /* Clear events and clear interrupt on disabled event */
    NRF_RADIO->EVENTS_DISABLED = NRF_EVENTS_Clear;
    NRF_RADIO->INTENCLR = RADIO_INTENCLR_DISABLED_Clear << RADIO_INTENCLR_DISABLED_Pos;
    NRF_RADIO->EVENTS_END = NRF_EVENTS_Clear;
    wfr_time = NRF_RADIO->SHORTS;

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
    /*
     * XXX: not sure what to do. We had a HW error during transmission.
     * For now I just count a stat but continue on like all is good.
     */
    if (was_encrypted) {
        if (NRF_CCM->EVENTS_ERROR) {
            STATS_INC(ble_phy_stats, tx_hw_err);
            NRF_CCM->EVENTS_ERROR = NRF_EVENTS_Clear;
        }
    }
#endif

    /* Call transmit end callback */
    if (g_ble_phy_data.txend_cb) {
        g_ble_phy_data.txend_cb(g_ble_phy_data.txend_arg);
    }

    transition = g_ble_phy_data.phy_transition;
    if (transition == BLE_PHY_TRANSITION_TX_RX) {
        /* Packet pointer needs to be reset. */
        ble_phy_rx_xcvr_setup();

        /*
         * Enable the wait for response timer. Note that cc #1 on
         * timer 0 contains the transmit start time
         */
        txlen = g_ble_phy_data.phy_tx_pyld_len;
        if (txlen && was_encrypted) {
            txlen += BLE_LL_DATA_MIC_LEN;
        }
        wfr_time = txstart - BLE_TX_LEN_USECS_M(NRF_RX_START_OFFSET);
        wfr_time += BLE_TX_DUR_USECS_M(txlen);
        wfr_time += cputime_usecs_to_ticks(BLE_LL_WFR_USECS);
        ble_ll_wfr_enable(wfr_time);
    } else {
        /* Disable automatic TXEN */
        nrf_ppi_channel_disable(NRF_PPI_CHANNEL20);
        assert(transition == BLE_PHY_TRANSITION_NONE);
    }
}

static void
ble_phy_rx_end_isr(void)
{
    int rc;
    uint8_t *dptr;
    uint8_t crcok;
    struct ble_mbuf_hdr *ble_hdr;

    /* Clear events and clear interrupt */
    NRF_RADIO->EVENTS_END = NRF_EVENTS_Clear;
    NRF_RADIO->INTENCLR = RADIO_INTENCLR_END_Clear << RADIO_INTENCLR_END_Pos;

    /* Disable automatic RXEN */
    nrf_ppi_channel_disable(NRF_PPI_CHANNEL21);

    /* Set RSSI and CRC status flag in header */
    ble_hdr = &g_ble_phy_data.rxhdr;
    assert(NRF_RADIO->EVENTS_RSSIEND != 0);
    ble_hdr->rxinfo.rssi = -1 * NRF_RADIO->RSSISAMPLE;

    dptr = g_ble_phy_data.rxdptr;

    /* Count PHY crc errors and valid packets */
    crcok = (uint8_t)NRF_RADIO->CRCSTATUS;
    if (!crcok) {
        STATS_INC(ble_phy_stats, rx_crc_err);
    } else {
        STATS_INC(ble_phy_stats, rx_valid);
        ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_CRC_OK;
#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
        if (g_ble_phy_data.phy_encrypted) {
            /* Only set MIC failure flag if frame is not zero length */
            if ((dptr[1] != 0) && (NRF_CCM->MICSTATUS == 0)) {
                ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_MIC_FAILURE;
            }

            /*
             * XXX: not sure how to deal with this. This should not
             * be a MIC failure but we should not hand it up. I guess
             * this is just some form of rx error and that is how we
             * handle it? For now, just set CRC error flags
             */
            if (NRF_CCM->EVENTS_ERROR) {
                STATS_INC(ble_phy_stats, rx_hw_err);
                ble_hdr->rxinfo.flags &= ~BLE_MBUF_HDR_F_CRC_OK;
            }

            /*
             * XXX: This is a total hack work-around for now but I dont
             * know what else to do. If ENDCRYPT is not set and we are
             * encrypted we need to not trust this frame and drop it.
             */
            if (NRF_CCM->EVENTS_ENDCRYPT == NRF_EVENTS_Clear) {
                STATS_INC(ble_phy_stats, rx_hw_err);
                ble_hdr->rxinfo.flags &= ~BLE_MBUF_HDR_F_CRC_OK;
            }
        }
#endif
    }

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1) || (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
    if (g_ble_phy_data.phy_encrypted || g_ble_phy_data.phy_privacy) {
        /*
         * XXX: This is a horrible ugly hack to deal with the RAM S1 byte.
         * This should get fixed as we should not be handing up the header
         * and length as part of the pdu.
         */
        dptr[2] = dptr[1];
        dptr[1] = dptr[0];
        ++dptr;
    }
#endif
    rc = ble_ll_rx_end(dptr, ble_hdr);
    if (rc < 0) {
        ble_phy_disable();
    }
}

static void
ble_phy_rx_start_isr(void)
{
    int rc;
    uint32_t state;
    struct ble_mbuf_hdr *ble_hdr;

    /* Clear events and clear interrupt */
    NRF_RADIO->EVENTS_ADDRESS = NRF_EVENTS_Clear;
    NRF_RADIO->INTENCLR = RADIO_INTENCLR_ADDRESS_Clear << RADIO_INTENCLR_ADDRESS_Pos;

    /* Wait to get 1st byte of frame */
    while (1) {
        state = NRF_RADIO->STATE;
        if (NRF_RADIO->EVENTS_BCMATCH != NRF_EVENTS_Clear) {
            break;
        }

        /*
         * If state is disabled, we should have the BCMATCH. If not,
         * something is wrong!
         */
        if (state == RADIO_STATE_STATE_Disabled) {
            NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;
            NRF_RADIO->SHORTS = NRF_RADIO_SHORTS_Disable;
            return;
        }
    }

    /* Initialize flags, channel and state in ble header at rx start */
    ble_hdr = &g_ble_phy_data.rxhdr;
    ble_hdr->rxinfo.flags = ble_ll_state_get();
    ble_hdr->rxinfo.channel = g_ble_phy_data.phy_chan;
    ble_hdr->rxinfo.handle = NULL;
    ble_hdr->beg_cputime = cputime_get32() - BLE_TX_LEN_USECS_M(NRF_RX_START_OFFSET);

    /* Call Link Layer receive start function */
    rc = ble_ll_rx_start(g_ble_phy_data.rxdptr, g_ble_phy_data.phy_chan,
                         &g_ble_phy_data.rxhdr);
    if (rc >= 0) {
        /* Set rx started flag and enable rx end ISR */
        g_ble_phy_data.phy_rx_started = TRUE;
        NRF_RADIO->INTENSET = RADIO_INTENSET_END_Set << RADIO_INTENSET_END_Pos;

#if (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
        /* Must start aar if we need to  */
        if (g_ble_phy_data.phy_privacy) {
            NRF_RADIO->EVENTS_BCMATCH = NRF_EVENTS_Clear;
            nrf_ppi_channel_disable(NRF_PPI_CHANNEL23);
            NRF_RADIO->BCC = NRF_BCC_BITS(BLE_DEV_ADDR_LEN + BLE_LL_PDU_HDR_LEN);
        }
#endif
    } else {
        /* Disable PHY */
        ble_phy_disable();
        STATS_INC(ble_phy_stats, rx_aborts);
    }

    /* Count rx starts */
    STATS_INC(ble_phy_stats, rx_starts);
}

void
RADIO_IRQHandler(void)
{
    uint32_t irq_en;

    /* Read irq register to determine which interrupts are enabled */
    irq_en = NRF_RADIO->INTENCLR;

    /* Check for disabled event. This only happens for transmits now */
    if ((irq_en & RADIO_INTENCLR_DISABLED_Msk) && NRF_RADIO->EVENTS_DISABLED) {
        ble_phy_tx_end_isr();
    }

    /* We get this if we have started to receive a frame */
    if ((irq_en & RADIO_INTENCLR_ADDRESS_Msk) && NRF_RADIO->EVENTS_ADDRESS) {
        ble_phy_rx_start_isr();
    }

    /* Receive packet end (we dont enable this for transmit) */
    if ((irq_en & RADIO_INTENCLR_END_Msk) && NRF_RADIO->EVENTS_END) {
        ble_phy_rx_end_isr();
    }

    /* Ensures IRQ is cleared */
    irq_en = NRF_RADIO->SHORTS;

    /* Count # of interrupts */
    STATS_INC(ble_phy_stats, phy_isrs);
}

/**
 * ble phy init
 *
 * Initialize the PHY.
 *
 * @return int 0: success; PHY error code otherwise
 */
int
ble_phy_init(void)
{
    int rc;

    // Handle BLE Radio tuning parameters from production for DTM if required.
    // Only needed for DTM without SoftDevice, as the SoftDevice normally handles this.
    // PCN-083.
    if ((NRF_FICR->OVERRIDEEN & FICR_OVERRIDEEN_BLE_1MBIT_Msk) ==
        (FICR_OVERRIDEEN_BLE_1MBIT_Override << FICR_OVERRIDEEN_BLE_1MBIT_Pos))
    {
        NRF_RADIO->OVERRIDE0 = NRF_FICR->BLE_1MBIT[0];
        NRF_RADIO->OVERRIDE1 = NRF_FICR->BLE_1MBIT[1];
        NRF_RADIO->OVERRIDE2 = NRF_FICR->BLE_1MBIT[2];
        NRF_RADIO->OVERRIDE3 = NRF_FICR->BLE_1MBIT[3];
        NRF_RADIO->OVERRIDE4 = NRF_FICR->BLE_1MBIT[4];
    }

    /* Set phy channel to an invalid channel so first set channel works */
    g_ble_phy_data.phy_chan = BLE_PHY_NUM_CHANS;

    /* Toggle peripheral power to reset (just in case) */
    NRF_RADIO->POWER = RADIO_POWER_POWER_Disabled << RADIO_POWER_POWER_Pos;
    NRF_RADIO->POWER = RADIO_POWER_POWER_Enabled << RADIO_POWER_POWER_Pos;

    /* Disable all interrupts */
    NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;

    /* Set configuration registers */
    NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;
    NRF_RADIO->PCNF0 = (NRF_LFLEN_Normal << RADIO_PCNF0_LFLEN_Pos) |
                       (NRF_S0LEN_Normal << RADIO_PCNF0_S0LEN_Pos) |
                       (NRF_S1LEN_Normal << RADIO_PCNF0_S1LEN_Pos);
    /* XXX: should maxlen be 251 for encryption? */
    NRF_RADIO->PCNF1 = (NRF_MAXLEN << RADIO_PCNF1_MAXLEN_Pos)   |
                       (NRF_STATLEN << RADIO_PCNF1_STATLEN_Pos) |
                       (NRF_BALEN << RADIO_PCNF1_BALEN_Pos)     |
                       (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos);

    /* Set base0 with the advertising access address */
    NRF_RADIO->BASE0 = NRF_BASE(BLE_ACCESS_ADDR_ADV);
    NRF_RADIO->PREFIX0 = NRF_PREFIX(BLE_ACCESS_ADDR_ADV, NRF_AP0_OFFSET);

    /* Configure the CRC registers */
    NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) |
                        (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);

    /* Configure BLE poly */
    NRF_RADIO->CRCPOLY = BLE_PHY_CRC_POLY & RADIO_CRCPOLY_CRCPOLY_Msk;

    /* Configure IFS */
    NRF_RADIO->TIFS = BLE_LL_IFS & RADIO_TIFS_TIFS_Msk;

    /* Captures tx/rx start in timer0 capture 1 */
    nrf_ppi_channel_enable(NRF_PPI_CHANNEL26);

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
    NRF_CCM->INTENCLR = (CCM_INTENCLR_ENDKSGEN_Clear << CCM_INTENCLR_ENDKSGEN_Pos) |
                        (CCM_INTENCLR_ENDCRYPT_Clear << CCM_INTENCLR_ENDCRYPT_Pos) |
                        (CCM_INTENCLR_ERROR_Clear << CCM_INTENCLR_ERROR_Pos);
    NRF_CCM->SHORTS = CCM_SHORTS_ENDKSGEN_CRYPT_Enabled << CCM_SHORTS_ENDKSGEN_CRYPT_Pos;
    NRF_CCM->EVENTS_ERROR = NRF_EVENTS_Clear;
    memset(g_nrf_encrypt_scratchpad, 0, sizeof(g_nrf_encrypt_scratchpad));
#endif

#if (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
    g_ble_phy_data.phy_aar_scratch = FALSE;
    NRF_AAR->IRKPTR = (uint32_t)&g_nrf_irk_list[0];
    NRF_AAR->INTENCLR = (AAR_INTENCLR_END_Clear << AAR_INTENCLR_END_Pos)           |
                        (AAR_INTENCLR_RESOLVED_Clear << AAR_INTENCLR_RESOLVED_Pos) |
                        (AAR_INTENCLR_NOTRESOLVED_Clear << AAR_INTENCLR_NOTRESOLVED_Pos);
    NRF_AAR->EVENTS_END = NRF_EVENTS_Clear;
    NRF_AAR->EVENTS_RESOLVED = NRF_EVENTS_Clear;
    NRF_AAR->EVENTS_NOTRESOLVED = NRF_EVENTS_Clear;
    NRF_AAR->NIRK = NRF_NIRK_Number(0);
#endif

    /* Set isr in vector table and enable interrupt */
    NVIC_SetPriority(RADIO_IRQn, 0);
    NVIC_EnableIRQ(RADIO_IRQn);

    /* Register phy statistics */
    if (!g_ble_phy_data.phy_stats_initialized) {
        rc = stats_init_and_reg(STATS_HDR(ble_phy_stats),
                                STATS_SIZE_INIT_PARMS(ble_phy_stats,
                                                      STATS_SIZE_32),
                                STATS_NAME_INIT_PARMS(ble_phy_stats),
                                "ble_phy");
        assert(rc == OS_OK);

        g_ble_phy_data.phy_stats_initialized = 1;
    }

    return 0;
}

/**
 * Puts the phy into receive mode.
 *
 * @return int 0: success; BLE Phy error code otherwise
 */
int
ble_phy_rx(void)
{
    /* Check radio state */
    nrf_wait_disabled();
    if ((NRF_RADIO->STATE & RADIO_STATE_STATE_Msk) !=
        (RADIO_STATE_STATE_Disabled << RADIO_STATE_STATE_Pos)) {
        ble_phy_disable();
        STATS_INC(ble_phy_stats, radio_state_errs);
        return BLE_PHY_ERR_RADIO_STATE;
    }

    /* Make sure all interrupts are disabled */
    NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;

    /* Clear events prior to enabling receive */
    NRF_RADIO->EVENTS_END = NRF_EVENTS_Clear;
    NRF_RADIO->EVENTS_DISABLED = NRF_EVENTS_Clear;

    /* Setup for rx */
    ble_phy_rx_xcvr_setup();

    /* Start the receive task in the radio if not automatically going to rx */
    if (NRF_PPI_CHANNEL_DISABLED == nrf_ppi_channel_enable_get(NRF_PPI_CHANNEL21)) {
        NRF_RADIO->TASKS_RXEN = NRF_TASK_Trigger;
    }

    ble_ll_log(BLE_LL_LOG_ID_PHY_RX, g_ble_phy_data.phy_encrypted, 0, 0);

    return 0;
}

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
/**
 * Called to enable encryption at the PHY. Note that this state will persist
 * in the PHY; in other words, if you call this function you have to call
 * disable so that future PHY transmits/receives will not be encrypted.
 *
 * @param pkt_counter
 * @param iv
 * @param key
 * @param is_master
 */
void
ble_phy_encrypt_enable(uint64_t pkt_counter, uint8_t *iv, uint8_t *key,
                       uint8_t is_master)
{
    memcpy(g_nrf_ccm_data.key, key, 16);
    g_nrf_ccm_data.pkt_counter = pkt_counter;
    memcpy(g_nrf_ccm_data.iv, iv, 8);
    g_nrf_ccm_data.dir_bit = is_master;
    g_ble_phy_data.phy_encrypted = 1;

    /* Encryption uses LFLEN = 5, S1LEN = 3. */
    NRF_RADIO->PCNF0 = (NRF_LFLEN_Encrypt << RADIO_PCNF0_LFLEN_Pos) |
                       (NRF_S0LEN_Encrypt << RADIO_PCNF0_S0LEN_Pos) |
                       (NRF_S1LEN_Encrypt << RADIO_PCNF0_S1LEN_Pos);

    /* Enable the module (AAR cannot be on while CCM on) */
    NRF_AAR->ENABLE = AAR_ENABLE_ENABLE_Disabled << AAR_ENABLE_ENABLE_Pos;
    NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Enabled << CCM_ENABLE_ENABLE_Pos;
}

void
ble_phy_encrypt_set_pkt_cntr(uint64_t pkt_counter, int dir)
{
    g_nrf_ccm_data.pkt_counter = pkt_counter;
    g_nrf_ccm_data.dir_bit = dir;
}

void
ble_phy_encrypt_disable(void)
{
    nrf_ppi_channels_disable(PPI_CHEN_CH24_Msk | PPI_CHEN_CH25_Msk);
    NRF_CCM->TASKS_STOP = NRF_TASK_Trigger;
    NRF_CCM->EVENTS_ERROR = NRF_EVENTS_Clear;
    NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Disabled << CCM_ENABLE_ENABLE_Pos;

    /* Switch back to normal length */
    NRF_RADIO->PCNF0 = (NRF_LFLEN_Normal << RADIO_PCNF0_LFLEN_Pos) |
                       (NRF_S0LEN_Normal << RADIO_PCNF0_S0LEN_Pos) |
                       (NRF_S1LEN_Normal << RADIO_PCNF0_S1LEN_Pos);

    g_ble_phy_data.phy_encrypted = FALSE;
}
#endif

void
ble_phy_set_txend_cb(ble_phy_tx_end_func txend_cb, void *arg)
{
    /* Set transmit end callback and arg */
    g_ble_phy_data.txend_cb = txend_cb;
    g_ble_phy_data.txend_arg = arg;
}

/**
 * Called to set the start time of a transmission.
 *
 * This function is called to set the start time when we are not going from
 * rx to tx automatically.
 *
 * NOTE: care must be taken when calling this function. The channel should
 * already be set.
 *
 * @param cputime
 *
 * @return int
 */
int
ble_phy_tx_set_start_time(uint32_t cputime)
{
    int rc;

    cputime_phy_set(cputime);
    nrf_ppi_channel_enable(NRF_PPI_CHANNEL20);
    nrf_ppi_channel_disable(NRF_PPI_CHANNEL21);
    if ((int32_t)(cputime_get32() - cputime) >= 0) {
        STATS_INC(ble_phy_stats, tx_late);
        ble_phy_disable();
        rc = BLE_PHY_ERR_TX_LATE;
    } else {
        rc = 0;
    }
    return rc;
}

/**
 * Called to set the start time of a reception
 *
 * This function acts a bit differently than transmit. If we are late getting
 * here we will still attempt to receive.
 *
 * NOTE: care must be taken when calling this function. The channel should
 * already be set.
 *
 * @param cputime
 *
 * @return int
 */
int
ble_phy_rx_set_start_time(uint32_t cputime)
{
    int rc;

    cputime_phy_set(cputime);
    nrf_ppi_channel_disable(NRF_PPI_CHANNEL20);
    nrf_ppi_channel_enable(NRF_PPI_CHANNEL21);
    if ((int32_t)(cputime_get32() - cputime) >= 0) {
        STATS_INC(ble_phy_stats, rx_late);
        nrf_ppi_channel_disable(NRF_PPI_CHANNEL21);
        NRF_RADIO->TASKS_RXEN = NRF_TASK_Trigger;
        rc = BLE_PHY_ERR_TX_LATE;
    } else {
        rc = 0;
    }
    return rc;
}

int
ble_phy_tx(struct os_mbuf *txpdu, uint8_t end_trans)
{
    int rc;
    uint8_t *dptr;
    uint8_t payload_len;
    uint32_t state;
    uint32_t shortcuts;
    struct ble_mbuf_hdr *ble_hdr;

    /* Better have a pdu! */
    assert(txpdu != NULL);

    /*
     * This check is to make sure that the radio is not in a state where
     * it is moving to disabled state. If so, let it get there.
     */
    nrf_wait_disabled();

    ble_hdr = BLE_MBUF_HDR_PTR(txpdu);
    payload_len = ble_hdr->txinfo.pyld_len;

#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
    if (g_ble_phy_data.phy_encrypted) {
        /* RAM representation has S0, LENGTH and S1 fields. (3 bytes) */
        dptr = (uint8_t *)&g_ble_phy_enc_buf[0];
        dptr[0] = ble_hdr->txinfo.hdr_byte;
        dptr[1] = payload_len;
        dptr[2] = 0;
        dptr += 3;

        NRF_CCM->SHORTS = CCM_SHORTS_ENDKSGEN_CRYPT_Enabled << CCM_SHORTS_ENDKSGEN_CRYPT_Pos;
        NRF_CCM->INPTR = (uint32_t)&g_ble_phy_enc_buf[0];
        NRF_CCM->OUTPTR = (uint32_t)&g_ble_phy_tx_buf[0];
        NRF_CCM->SCRATCHPTR = (uint32_t)&g_nrf_encrypt_scratchpad[0];
        NRF_CCM->EVENTS_ERROR = NRF_EVENTS_Clear;
        NRF_CCM->MODE = CCM_MODE_MODE_Encryption << CCM_MODE_MODE_Pos;
        NRF_CCM->CNFPTR = (uint32_t)&g_nrf_ccm_data;
        nrf_ppi_channels_disable(PPI_CHENCLR_CH23_Msk | PPI_CHENCLR_CH25_Msk);
        nrf_ppi_channel_enable(NRF_PPI_CHANNEL24);
    } else {

#if (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
        /* Reconfigure PCNF0 */
        NRF_RADIO->PCNF0 = (NRF_LFLEN_Normal << RADIO_PCNF0_LFLEN_Pos) |
                           (NRF_S0LEN_Normal << RADIO_PCNF0_S0LEN_Pos) |
                           (NRF_S1LEN_Normal << RADIO_PCNF0_S1LEN_Pos);
        nrf_ppi_channel_disable(NRF_PPI_CHANNEL23);
        NRF_AAR->IRKPTR = (uint32_t)&g_nrf_irk_list[0];
#endif
        /* RAM representation has S0 and LENGTH fields (2 bytes) */
        dptr = (uint8_t *)&g_ble_phy_tx_buf[0];
        dptr[0] = ble_hdr->txinfo.hdr_byte;
        dptr[1] = payload_len;
        dptr += 2;
    }
#else

#if (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
    /* Reconfigure PCNF0 */
    NRF_RADIO->PCNF0 = (NRF_LFLEN_Normal << RADIO_PCNF0_LFLEN_Pos) |
                       (NRF_S0LEN_Normal << RADIO_PCNF0_S0LEN_Pos) |
                       (NRF_S1LEN_Normal << RADIO_PCNF0_S1LEN_Pos);
    nrf_ppi_channel_disable(NRF_PPI_CHANNEL23);
#endif

    /* RAM representation has S0 and LENGTH fields (2 bytes) */
    dptr = (uint8_t *)&g_ble_phy_tx_buf[0];
    dptr[0] = ble_hdr->txinfo.hdr_byte;
    dptr[1] = payload_len;
    dptr += 2;
#endif

    NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_tx_buf[0];

    /* Clear the ready, end and disabled events */
    NRF_RADIO->EVENTS_READY = NRF_EVENTS_Clear;
    NRF_RADIO->EVENTS_END = NRF_EVENTS_Clear;
    NRF_RADIO->EVENTS_DISABLED = NRF_EVENTS_Clear;

    /* Enable shortcuts for transmit start/end. */
    shortcuts = (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos) |
                (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos);
    if (end_trans == BLE_PHY_TRANSITION_TX_RX) {
        shortcuts |= RADIO_SHORTS_DISABLED_RXEN_Msk;
    }
    NRF_RADIO->SHORTS = shortcuts;
    NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Set << RADIO_INTENSET_DISABLED_Pos;

    /* Set transmitted payload length */
    g_ble_phy_data.phy_tx_pyld_len = payload_len;

    /* Set the PHY transition */
    g_ble_phy_data.phy_transition = end_trans;

    /* If we already started transmitting, abort it! */
    state = NRF_RADIO->STATE;
    if (state != RADIO_STATE_STATE_Tx) {
        /* Copy data from mbuf into transmit buffer */
        os_mbuf_copydata(txpdu, ble_hdr->txinfo.offset, payload_len, dptr);

        /* Set phy state to transmitting and count packet statistics */
        g_ble_phy_data.phy_state = BLE_PHY_STATE_TX;
        STATS_INC(ble_phy_stats, tx_good);
        STATS_INCN(ble_phy_stats, tx_bytes, payload_len + BLE_LL_PDU_HDR_LEN);
        rc = BLE_ERR_SUCCESS;
    } else {
        ble_phy_disable();
        STATS_INC(ble_phy_stats, tx_late);
        rc = BLE_PHY_ERR_RADIO_STATE;
    }

    return rc;
}

/**
 * ble phy txpwr set
 *
 * Set the transmit output power (in dBm).
 *
 * NOTE: If the output power specified is within the BLE limits but outside
 * the chip limits, we "rail" the power level so we dont exceed the min/max
 * chip values.
 *
 * @param dbm Power output in dBm.
 *
 * @return int 0: success; anything else is an error
 */
int
ble_phy_txpwr_set(int dbm)
{
    /* Check valid range */
    assert(dbm <= BLE_PHY_MAX_PWR_DBM);

    /* "Rail" power level if outside supported range */
    if (dbm > NRF_TX_PWR_MAX_DBM) {
        dbm = NRF_TX_PWR_MAX_DBM;
    } else {
        if (dbm < NRF_TX_PWR_MIN_DBM) {
            dbm = NRF_TX_PWR_MIN_DBM;
        }
    }

    NRF_RADIO->TXPOWER = (uint32_t)dbm & RADIO_TXPOWER_TXPOWER_Msk;
    g_ble_phy_data.phy_txpwr_dbm = (int8_t)NRF_RADIO->TXPOWER;

    return 0;
}

/**
 * ble phy txpwr get
 *
 * Get the transmit power.
 *
 * @return int  The current PHY transmit power, in dBm
 */
int
ble_phy_txpwr_get(void)
{
    return g_ble_phy_data.phy_txpwr_dbm;
}

/**
 * ble phy setchan
 *
 * Sets the logical frequency of the transceiver. The input parameter is the
 * BLE channel index (0 to 39, inclusive). The NRF frequency register works like
 * this: logical frequency = 2400 + FREQ (MHz).
 *
 * Thus, to get a logical frequency of 2402 MHz, you would program the
 * FREQUENCY register to 2.
 *
 * @param chan This is the Data Channel Index or Advertising Channel index
 *
 * @return int 0: success; PHY error code otherwise
 */
int
ble_phy_setchan(uint8_t chan, uint32_t access_addr, uint32_t crcinit)
{
    uint8_t freq;
    uint32_t prefix;

    assert(chan < BLE_PHY_NUM_CHANS);

    /* Check for valid channel range */
    if (chan >= BLE_PHY_NUM_CHANS) {
        return BLE_PHY_ERR_INV_PARAM;
    }

    /* Get correct frequency */
    if (chan < BLE_PHY_NUM_DATA_CHANS) {
        if (chan < 11) {
            /* Data channel 0 starts at 2404. 0 - 10 are contiguous */
            freq = NRF_FREQUENCY(BLE_PHY_DATA_CHAN0_FREQ_MHZ +
                                 BLE_PHY_CHAN_SPACING_MHZ * chan);
        } else {
            /* Data channel 11 starts at 2428. 11 - 36 are contiguous */
            freq = NRF_FREQUENCY(BLE_PHY_DATA_CHAN0_FREQ_MHZ +
                                 BLE_PHY_CHAN_SPACING_MHZ * (chan + 1));
        }

        /* Set current access address */
        g_ble_phy_data.phy_access_address = access_addr;

        /* Configure logical address 1 and crcinit */
        prefix = NRF_RADIO->PREFIX0;
        prefix &= ~RADIO_PREFIX0_AP1_Msk;
        prefix |= NRF_PREFIX(access_addr, NRF_AP1_OFFSET);
        NRF_RADIO->BASE1 = NRF_BASE(access_addr);
        NRF_RADIO->PREFIX0 = prefix;
        NRF_RADIO->TXADDRESS = NRF_TXADDRESS(1);
        NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR1_Enabled << RADIO_RXADDRESSES_ADDR1_Pos;
        NRF_RADIO->CRCINIT = crcinit;
    } else {
        if (chan == 37) {
            /* This advertising channel is at 2402 MHz */
            freq = NRF_FREQUENCY(2402);
        } else if (chan == 38) {
            /* This advertising channel is at 2426 MHz */
            freq = NRF_FREQUENCY(2426);
        } else {
            /* This advertising channel is at 2480 MHz */
            freq = NRF_FREQUENCY(2480);
        }

        /* Logical adddress 0 preconfigured */
        NRF_RADIO->TXADDRESS = NRF_TXADDRESS(0);
        NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos;
        NRF_RADIO->CRCINIT = BLE_LL_CRCINIT_ADV & RADIO_CRCINIT_CRCINIT_Msk;

        /* Set current access address */
        g_ble_phy_data.phy_access_address = BLE_ACCESS_ADDR_ADV;
    }

    /* Set the frequency and the data whitening initial value */
    g_ble_phy_data.phy_chan = chan;
    NRF_RADIO->FREQUENCY = freq;
    NRF_RADIO->DATAWHITEIV = chan & RADIO_DATAWHITEIV_DATAWHITEIV_Msk;

    ble_ll_log(BLE_LL_LOG_ID_PHY_SETCHAN, chan, freq, access_addr);

    return 0;
}

/**
 * Disable the PHY. This will do the following:
 *  -> Turn off all phy interrupts.
 *  -> Disable internal shortcuts.
 *  -> Disable the radio.
 *  -> Make sure we wont automatically go to rx/tx on output compare
 *  -> Sets phy state to idle.
 *  -> Clears any pending irqs in the NVIC. Might not be necessary but we do
 *  it as a precaution.
 */
void
ble_phy_disable(void)
{
    ble_ll_log(BLE_LL_LOG_ID_PHY_DISABLE, g_ble_phy_data.phy_state, 0, 0);

    NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;
    NRF_RADIO->SHORTS = NRF_RADIO_SHORTS_Disable;
    NRF_RADIO->TASKS_DISABLE = NRF_TASK_Trigger;
    nrf_ppi_channels_disable(PPI_CHENCLR_CH20_Msk | PPI_CHENCLR_CH21_Msk | PPI_CHENCLR_CH23_Msk);
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    g_ble_phy_data.phy_state = BLE_PHY_STATE_IDLE;
}

/* Gets the current access address */
uint32_t ble_phy_access_addr_get(void)
{
    return g_ble_phy_data.phy_access_address;
}

/**
 * Return the phy state
 *
 * @return int The current PHY state.
 */
int
ble_phy_state_get(void)
{
    return g_ble_phy_data.phy_state;
}

/**
 * Called to see if a reception has started
 *
 * @return int
 */
int
ble_phy_rx_started(void)
{
    return g_ble_phy_data.phy_rx_started;
}

/**
 * Return the transceiver state
 *
 * @return int transceiver state.
 */
uint8_t
ble_phy_xcvr_state_get(void)
{
    uint32_t state;
    state = NRF_RADIO->STATE;
    return (uint8_t)state;
}

/**
 * Called to return the maximum data pdu payload length supported by the
 * phy. For this chip, if encryption is enabled, the maximum payload is 27
 * bytes.
 *
 * @return uint8_t Maximum data channel PDU payload size supported
 */
uint8_t
ble_phy_max_data_pdu_pyld(void)
{
#if (BLE_LL_CFG_FEAT_LE_ENCRYPTION == 1)
    return NRF_MAX_ENCRYPTED_PYLD_LEN;
#else
    return BLE_LL_DATA_PDU_MAX_PYLD;
#endif
}

#if (BLE_LL_CFG_FEAT_LL_PRIVACY == 1)
void
ble_phy_resolv_list_enable(void)
{
    NRF_AAR->NIRK = NRF_NIRK_Number(g_nrf_num_irks);
    g_ble_phy_data.phy_privacy = TRUE;
}

void
ble_phy_resolv_list_disable(void)
{
    g_ble_phy_data.phy_privacy = FALSE;
}
#endif
