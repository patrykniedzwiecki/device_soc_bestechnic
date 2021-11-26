/***************************************************************************
 *
 * Copyright 2015-2019 BES.
 * All rights reserved. All unpublished rights reserved.
 *
 * No part of this work may be used or reproduced in any form or by any
 * means, or stored in a database or retrieval system, without prior written
 * permission of BES.
 *
 * Use of this work is governed by a license granted by BES.
 * This work contains confidential and proprietary information of
 * BES. which is protected by copyright, trade secret,
 * trademark and other intellectual property rights.
 *
 ****************************************************************************/
#ifdef CHIP_HAS_TRANSQ
//#define EXTEND_TRANSQ_2003 1
#ifdef EXTEND_TRANSQ_2003
#include "hal_transq_2003.h"
#else
#include "plat_addr_map.h"
#include "reg_transq.h"
#include "hal_transq.h"
#include "hal_trace.h"
#include "hal_cmu.h"
#include "hal_sleep.h"
#include "hal_cache.h"
#include "hal_chipid.h"
//#include "string.h"
#include "stdbool.h"
#include "cmsis_nvic.h"
#define READ_REG(b,a) \
     (*(volatile uint32_t *)(b+a))

#define WRITE_REG(v,b,a) \
     ((*(volatile uint32_t *)(b+a)) = v)
     
#define REG(a)          *(volatile uint32_t *)(a)

#if defined(__ARM_ARCH_ISA_ARM)
void WEAK transq_lock_init(void)
{
    return;
}
uint32_t WEAK transq_lock(void)
{
    return int_lock();
}
void WEAK transq_unlock(uint32_t cpsr)
{
    int_unlock(cpsr);
}
#endif

// BITMAP:
// [High Priority Slots] ...... [Normal Priority Slots]
// 31 30 29 28 27 26 25  ...... 10 9 8 7 6 5 4 3 2 1 0

static struct TRANSQ_T * const transq[HAL_TRANSQ_ID_QTY] = {
    (struct TRANSQ_T *)TRANSQ0_BASE,
#if (CHIP_HAS_TRANSQ > 1)
    (struct TRANSQ_T *)TRANSQ1_BASE,
#endif
};

static struct TRANSQ_T * const peer_transq[HAL_TRANSQ_ID_QTY] = {
    (struct TRANSQ_T *)TRANSQ0_PEER_BASE,
#if (CHIP_HAS_TRANSQ > 1)
    (struct TRANSQ_T *)TRANSQ1_PEER_BASE,
#endif
};

static const enum HAL_CMU_MOD_ID_T transq_mod[HAL_TRANSQ_ID_QTY] = {
    HAL_CMU_MOD_P_TRANSQ0,
#if (CHIP_HAS_TRANSQ > 1)
    HAL_CMU_MOD_P_TRANSQ1,
#endif
};

static const IRQn_Type remote_irq_num[HAL_TRANSQ_ID_QTY] = {
    TRANSQ0_RMT_IRQn,
#if (CHIP_HAS_TRANSQ > 1)
    TRANSQ1_RMT_IRQn,
#endif
};

static const IRQn_Type local_irq_num[HAL_TRANSQ_ID_QTY] = {
    TRANSQ0_LCL_IRQn,
#if (CHIP_HAS_TRANSQ > 1)
    TRANSQ1_LCL_IRQn,
#endif
};

static struct HAL_TRANSQ_CFG_T transq_cfg[HAL_TRANSQ_ID_QTY];

static uint8_t next_tx_slot[HAL_TRANSQ_ID_QTY][HAL_TRANSQ_PRI_QTY];

static uint8_t active_tx_slot[HAL_TRANSQ_ID_QTY][HAL_TRANSQ_PRI_QTY];

static bool tx_slot_full[HAL_TRANSQ_ID_QTY][HAL_TRANSQ_PRI_QTY];

static uint8_t next_rx_slot[HAL_TRANSQ_ID_QTY][HAL_TRANSQ_PRI_QTY];

static uint32_t rx_irq_mask[HAL_TRANSQ_ID_QTY][HAL_TRANSQ_PRI_QTY];

static uint32_t tx_irq_mask[HAL_TRANSQ_ID_QTY][HAL_TRANSQ_PRI_QTY];

static uint32_t TxOutstanding = 0;

static uint32_t construct_mask(uint32_t lsb, uint32_t width)
{
    uint32_t i;
    uint32_t result;

    if (lsb >= 32 || width == 0) {
        return 0;
    }

    result = 0;
    for (i = lsb; i < lsb + width; i++) {
        result |= (1 << i);
    }

    return result;
}

static uint32_t get_next_rx_slot(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, uint32_t slot)
{
    slot++;
    if (pri == HAL_TRANSQ_PRI_HIGH) {
        if (slot >= TRANSQ_SLOT_NUM) {
            slot = TRANSQ_SLOT_NUM - transq_cfg[id].slot.rx_num[pri];
        }
    } else {
        if (slot >= transq_cfg[id].slot.rx_num[pri]) {
            slot = 0;
        }
    }

    return slot;
}

static uint32_t get_next_tx_slot(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, uint32_t slot)
{
    slot++;
    if (pri == HAL_TRANSQ_PRI_HIGH) {
        if (slot >= TRANSQ_SLOT_NUM) {
            slot = TRANSQ_SLOT_NUM - transq_cfg[id].slot.tx_num[pri];
        }
    } else {
        if (slot >= transq_cfg[id].slot.tx_num[pri]) {
            slot = 0;
        }
    }

    return slot;
}
#ifdef __ARM_ARCH_ISA_ARM
#ifdef RTOS
static void hal_transq_remote_irq_handler(int irq_num, void * irq_data)
#else
static void hal_transq_remote_irq_handler(void)
#endif
#else
static void hal_transq_remote_irq_handler(void)
#endif
{
    enum HAL_TRANSQ_ID_T id;
    enum HAL_TRANSQ_PRI_T pri = HAL_TRANSQ_PRI_NORMAL;
    uint32_t status;
    uint32_t slot;
    uint32_t lock;

#if (CHIP_HAS_TRANSQ > 1)
#ifdef __ARM_ARCH_ISA_ARM
#ifdef RTOS
#ifdef KERNEL_LITEOS_A
    IRQn_Type irq = NVIC_GetCurrentActiveIRQ();
#else
    IRQn_Type irq = (IRQn_Type)irq_num;
#endif
#else
    IRQn_Type irq = NVIC_GetCurrentActiveIRQ();
#endif
#else
    IRQn_Type irq = NVIC_GetCurrentActiveIRQ();
#endif

    for (id = HAL_TRANSQ_ID_0; id < HAL_TRANSQ_ID_QTY; id++) {
        if (irq == remote_irq_num[id]) {
            break;
        }
    }
    if (id >= HAL_TRANSQ_ID_QTY) {
        return;
    }
#else
    id = HAL_TRANSQ_ID_0;
#endif

    while ((status = peer_transq[id]->RMT_MIS) != 0) {
#if defined(__ARM_ARCH_ISA_ARM)
        lock = transq_lock();
#else
        lock = int_lock();
#endif
        if (status & rx_irq_mask[id][HAL_TRANSQ_PRI_HIGH]) {
            pri = HAL_TRANSQ_PRI_HIGH;
        } else if (status & rx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL]) {
            pri = HAL_TRANSQ_PRI_NORMAL;
        } else {
            ASSERT(false, "TRANSQ-%d: Corrupted rx mask: status=0x%08X rx_mask=0x%08X / 0x%08X",
                id, status, rx_irq_mask[id][HAL_TRANSQ_PRI_HIGH], rx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL]);
        }
        slot = next_rx_slot[id][pri];
        ASSERT(slot < TRANSQ_SLOT_NUM && (status & (1 << slot)),
            "TRANSQ-%d: Rx IRQ when no slot or out of order: status=0x%08X prio=%d rx_mask=0x%08X slot=%d",
            id, status, pri, rx_irq_mask[id][pri], slot);

        // Mask IRQ from corresponding slots
        peer_transq[id]->RMT_INTMASK &= ~rx_irq_mask[id][pri];
        // Force to flush the write FIFO in the bus path
        peer_transq[id]->RMT_INTMASK;

#if defined(__ARM_ARCH_ISA_ARM)
        transq_unlock(lock);
#else
        int_unlock(lock);
#endif
        transq_cfg[id].rx_irq_count ++;
        if (transq_cfg[id].rx_handler) {
            transq_cfg[id].rx_handler(pri);
        }
    }
}

static int hal_transq_active_tx_valid(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri)
{
    return ((active_tx_slot[id][pri] >= TRANSQ_SLOT_NUM) ||
            (active_tx_slot[id][pri] != next_tx_slot[id][pri]) ||
            tx_slot_full[id][pri]);
}

void hal_transq_local_irq_handler_body(enum HAL_TRANSQ_ID_T id)
{
    enum HAL_TRANSQ_PRI_T pri = HAL_TRANSQ_PRI_NORMAL;
    uint32_t status;
    uint32_t slot, next_slot;
    uint32_t lock;

    while ((status = transq[id]->LERR_MIS) != 0) {
        transq[id]->LERR_ISC.LERR_INTCLR = status;
        ASSERT(false, "TRANSQ-%d: Tx on active slot: 0x%08x", id, status);
    }

    while ((status = transq[id]->LDONE_MIS) != 0) {
        if (transq_cfg[id].tx_handler) {
#if defined(__ARM_ARCH_ISA_ARM)
            lock = transq_lock();
#else
            lock = int_lock();
#endif
            ASSERT(hal_transq_active_tx_valid(id, HAL_TRANSQ_PRI_HIGH), "TRANSQ-%d: Corrupted pri active tx: active=%d next=%d full=%d",
                id, active_tx_slot[id][HAL_TRANSQ_PRI_HIGH], next_tx_slot[id][HAL_TRANSQ_PRI_HIGH], tx_slot_full[id][HAL_TRANSQ_PRI_HIGH]);
            ASSERT(hal_transq_active_tx_valid(id, HAL_TRANSQ_PRI_NORMAL), "TRANSQ-%d: Corrupted active tx: active=%d next=%d full=%d",
                id, active_tx_slot[id][HAL_TRANSQ_PRI_NORMAL], next_tx_slot[id][HAL_TRANSQ_PRI_NORMAL], tx_slot_full[id][HAL_TRANSQ_PRI_NORMAL]);

            if (status & tx_irq_mask[id][HAL_TRANSQ_PRI_HIGH]) {
                pri = HAL_TRANSQ_PRI_HIGH;
            } else if (status & tx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL]) {
                pri = HAL_TRANSQ_PRI_NORMAL;
            } else {
                ASSERT(false, "TRANSQ-%d: Corrupted tx mask: status=0x%08X tx_mask=0x%08X / 0x%08X",
                    id, status, tx_irq_mask[id][HAL_TRANSQ_PRI_HIGH], tx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL]);
            }
            slot = active_tx_slot[id][pri];
            ASSERT(slot < TRANSQ_SLOT_NUM && (status & (1 << slot)),
                "TRANSQ-%d: Tx done IRQ when slot empty or out of order: status=0x%08x next=%d pri_next=%d",
                id, status, active_tx_slot[id][HAL_TRANSQ_PRI_NORMAL], active_tx_slot[id][HAL_TRANSQ_PRI_HIGH]);

            // Clear the interrupt
            transq[id]->LDONE_ISC.LDONE_INTCLR = (1 << slot);

            next_slot = get_next_tx_slot(id, pri, slot);

            if (!tx_slot_full[id][pri] && (next_slot == next_tx_slot[id][pri])) {
                // No tx in progress
                active_tx_slot[id][pri] = TRANSQ_SLOT_NUM;
            } else {
                tx_slot_full[id][pri] = false;
                if (transq_cfg[id].slot.tx_num[pri] == 1) {
                    // No tx in progress
                    active_tx_slot[id][pri] = TRANSQ_SLOT_NUM;
                } else {
                    // Some tx in progress
                    active_tx_slot[id][pri] = next_slot;
                }
            }
            if (TxOutstanding) {
                TxOutstanding--;
                if (!TxOutstanding) {
                    hal_sys_wake_unlock(HAL_SYS_WAKE_LOCK_USER_TRANSQ);
                }
            }
#if defined(__ARM_ARCH_ISA_ARM)
            transq_unlock(lock);
#else
            int_unlock(lock);
#endif
            transq_cfg[id].tx_handler(pri,
                (const uint8_t *)transq[id]->WSLOT[slot].ADDR,
                transq[id]->WSLOT[slot].LEN);
        } else {
            transq[id]->LDONE_INTMASK = 0;
        }
    }
}

#ifdef __ARM_ARCH_ISA_ARM
#ifdef RTOS
void hal_transq_local_irq_handler(int irq_num, void *irq_data)
#else
static void hal_transq_local_irq_handler(void)
#endif
#else
static void hal_transq_local_irq_handler(void)
#endif
{
    enum HAL_TRANSQ_ID_T id;
#if (CHIP_HAS_TRANSQ > 1)
#ifdef __ARM_ARCH_ISA_ARM
#ifdef RTOS
#ifdef KERNEL_LITEOS_A
    IRQn_Type irq = NVIC_GetCurrentActiveIRQ();
#else
    IRQn_Type irq = (IRQn_Type)irq_num;
#endif
#else
    IRQn_Type irq = NVIC_GetCurrentActiveIRQ();
#endif
#else
    IRQn_Type irq = NVIC_GetCurrentActiveIRQ();
#endif

    for (id = HAL_TRANSQ_ID_0; id < HAL_TRANSQ_ID_QTY; id++) {
        if (irq == local_irq_num[id]) {
            break;
        }
    }
    if (id >= HAL_TRANSQ_ID_QTY) {
        return;
    }
#else
    id = HAL_TRANSQ_ID_0;
#endif

    hal_transq_local_irq_handler_body(id);
}

enum HAL_TRANSQ_RET_T hal_transq_get_rx_status(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, bool *ready)
{
    uint32_t lock;
    uint32_t slot;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (pri >= HAL_TRANSQ_PRI_QTY) {
        return HAL_TRANSQ_RET_BAD_PRI;
    }
    if (transq_cfg[id].slot.rx_num[pri] == 0) {
        return HAL_TRANSQ_RET_BAD_RX_NUM;
    }
    if (transq_cfg[id].rx_handler) {
        // Rx will be processed by IRQ handler
        return HAL_TRANSQ_RET_BAD_MODE;
    }

    lock = int_lock();

    slot = next_rx_slot[id][pri];

    if ((slot < TRANSQ_SLOT_NUM) && (peer_transq[id]->RMT_ISC.RMT_RIS & (1 << slot))) {
        *ready = true;
    } else {
        *ready = false;
    }

    int_unlock(lock);

    return HAL_TRANSQ_RET_OK;
}

enum HAL_TRANSQ_RET_T hal_transq_get_tx_status(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, bool *done)
{
    uint32_t lock;
    uint32_t slot, next_slot;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (pri >= HAL_TRANSQ_PRI_QTY) {
        return HAL_TRANSQ_RET_BAD_PRI;
    }
    if (transq_cfg[id].slot.tx_num[pri] == 0) {
        return HAL_TRANSQ_RET_BAD_TX_NUM;
    }
    if (transq_cfg[id].tx_handler) {
        // Tx done will be processed by IRQ handler
        return HAL_TRANSQ_RET_BAD_MODE;
    }

    lock = int_lock();

    slot = active_tx_slot[id][pri];

    ASSERT(hal_transq_active_tx_valid(id, pri), "TRANSQ-%d: Corrupted active tx: pri=%d active=%d next=%d full=%d",
        id, pri, active_tx_slot[id][pri], next_tx_slot[id][pri], tx_slot_full[id][pri]);

    if (transq[id]->LDONE_ISC.LDONE_RIS & (1 << slot)) {
        *done = true;

        // Clear the interrupt
        transq[id]->LDONE_ISC.LDONE_INTCLR = (1 << slot);

        next_slot = get_next_tx_slot(id, pri, slot);

        if (!tx_slot_full[id][pri] && (next_slot == next_tx_slot[id][pri])) {
            // No tx in progress
            active_tx_slot[id][pri] = TRANSQ_SLOT_NUM;
        } else {
            tx_slot_full[id][pri] = false;
            if (transq_cfg[id].slot.tx_num[pri] == 1) {
                // No tx in progress
                active_tx_slot[id][pri] = TRANSQ_SLOT_NUM;
            } else {
                // Some tx in progress
                active_tx_slot[id][pri] = next_slot;
            }
        }
    } else {
        *done = false;
    }

    int_unlock(lock);

    return HAL_TRANSQ_RET_OK;
}

bool hal_transq_tx_busy(enum HAL_TRANSQ_ID_T id)
{
    bool ret;
    uint32_t lock;
    lock = int_lock();

    if ((active_tx_slot[id][HAL_TRANSQ_PRI_NORMAL] != TRANSQ_SLOT_NUM ||
                active_tx_slot[id][HAL_TRANSQ_PRI_HIGH] != TRANSQ_SLOT_NUM)
            && (transq[id]->LDONE_MIS == 0)) {
        ret = true;
    } else {
        ret = false;
    }

    int_unlock(lock);
    return ret;
}
enum HAL_TRANSQ_RET_T hal_transq_rx_first(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, const uint8_t **data, uint32_t *len)
{
    enum HAL_TRANSQ_RET_T ret;
    uint32_t slot;
    uint32_t lock;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (pri >= HAL_TRANSQ_PRI_QTY) {
        return HAL_TRANSQ_RET_BAD_PRI;
    }
    if (transq_cfg[id].slot.rx_num[pri] == 0) {
        return HAL_TRANSQ_RET_BAD_RX_NUM;
    }

#if defined(__ARM_ARCH_ISA_ARM)
    lock = transq_lock();
#else
    lock = int_lock();
#endif

    slot = next_rx_slot[id][pri];

    if ((slot < TRANSQ_SLOT_NUM) && (peer_transq[id]->RMT_ISC.RMT_RIS & (1 << slot))) {
        // Msg available
        ret = HAL_TRANSQ_RET_OK;

        if (data) {
            *data = (const uint8_t *)peer_transq[id]->RSLOT[slot].ADDR;
        }
        if (len) {
            *len = peer_transq[id]->RSLOT[slot].LEN;
        }
    } else {
        // No msg. Re-enable IRQ
        ret = HAL_TRANSQ_RET_RX_EMPTY;
       // wlan_func.transq_irq_count =0;
        transq_cfg[id].rx_irq_count =0;
        if (data) {
            *data = NULL;
        }
        if (len) {
            *len = 0;
        }
        peer_transq[id]->RMT_INTMASK |= rx_irq_mask[id][pri];
    }

#if defined(__ARM_ARCH_ISA_ARM)
    transq_unlock(lock);
#else
    int_unlock(lock);
#endif

    return ret;
}

enum HAL_TRANSQ_RET_T hal_transq_rx_next(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, const uint8_t **data, uint32_t *len)
{
    enum HAL_TRANSQ_RET_T ret;
    uint32_t slot;
    uint32_t lock;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (pri >= HAL_TRANSQ_PRI_QTY) {
        return HAL_TRANSQ_RET_BAD_PRI;
    }
    if (transq_cfg[id].slot.rx_num[pri] == 0) {
        return HAL_TRANSQ_RET_BAD_RX_NUM;
    }

    ret = HAL_TRANSQ_RET_OK;

#if defined(__ARM_ARCH_ISA_ARM)
    lock = transq_lock();
#else
    lock = int_lock();
#endif

    slot = next_rx_slot[id][pri];

    if ((slot < TRANSQ_SLOT_NUM) && (peer_transq[id]->RMT_ISC.RMT_RIS & (1 << slot))) {
        // Clear cur IRQ
        peer_transq[id]->RMT_ISC.RMT_INTCLR = (1 << slot);

        // Update next_rx_slot
        slot = get_next_rx_slot(id, pri, slot);
        next_rx_slot[id][pri] = slot;

        if ((slot < TRANSQ_SLOT_NUM) && (peer_transq[id]->RMT_ISC.RMT_RIS & (1 << slot))) {
            // Next msg available
            if (data) {
                *data = (const uint8_t *)peer_transq[id]->RSLOT[slot].ADDR;
            }
            if (len) {
                *len = peer_transq[id]->RSLOT[slot].LEN;
            }
        } else {
            // No msg
            ret = HAL_TRANSQ_RET_RX_EMPTY;
        }
    } else {
        // No msg
        ret = HAL_TRANSQ_RET_RX_EMPTY;
    }

    if (ret == HAL_TRANSQ_RET_RX_EMPTY) {
        if (data) {
            *data = NULL;
        }
        if (len) {
            *len = 0;
        }
        //wlan_func.transq_irq_count =0;
        transq_cfg[id].rx_irq_count =0;
        // Re-enable IRQ
        peer_transq[id]->RMT_INTMASK |= rx_irq_mask[id][pri];
    }

#if defined(__ARM_ARCH_ISA_ARM)
    transq_unlock(lock);
#else
    int_unlock(lock);
#endif

    return ret;
}

enum HAL_TRANSQ_RET_T hal_transq_tx(enum HAL_TRANSQ_ID_T id, enum HAL_TRANSQ_PRI_T pri, const uint8_t *data, uint32_t len)
{
    enum HAL_TRANSQ_RET_T ret;
    uint32_t lock;
    uint32_t slot;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (pri >= HAL_TRANSQ_PRI_QTY) {
        return HAL_TRANSQ_RET_BAD_PRI;
    }
    if (transq_cfg[id].slot.tx_num[pri] == 0) {
        return HAL_TRANSQ_RET_BAD_TX_NUM;
    }

#if defined(__ARM_ARCH_ISA_ARM)
    lock = transq_lock();
#else
    lock = int_lock();
#endif

#ifndef __ARM_ARCH_ISA_ARM
#if (defined(PSRAM_ENABLE) || defined(PSRAMUHS_ENABLE)) && defined(CHIP_BEST2001)
    if (!((uint32_t)data > RAM_BASE && (uint32_t)data < (RAM_BASE + RAM_TOTAL_SIZE))) {
        hal_cache_sync_all(HAL_CACHE_ID_D_CACHE);
    }
#endif
#endif
    if (tx_slot_full[id][pri]) {
        ret = HAL_TRANSQ_RET_TX_FULL;
    } else {
        TxOutstanding++;
        if (TxOutstanding == 1) {
            hal_sys_wake_lock(HAL_SYS_WAKE_LOCK_USER_TRANSQ);
        }

        ret = HAL_TRANSQ_RET_OK;

        slot = next_tx_slot[id][pri];

        transq[id]->WSLOT[slot].ADDR = (uint32_t)data;
        transq[id]->WSLOT[slot].LEN = len;
        transq[id]->RMT_INTSET = (1 << slot);

        // Update active_tx_slot if this is the only tx in progress
        if (active_tx_slot[id][pri] >= TRANSQ_SLOT_NUM) {
            active_tx_slot[id][pri] = slot;
        }

        // Update next_tx_slot
        next_tx_slot[id][pri] = get_next_tx_slot(id, pri, slot);

        if (next_tx_slot[id][pri] == active_tx_slot[id][pri]) {
            tx_slot_full[id][pri] = true;
        }
    }

#if defined(__ARM_ARCH_ISA_ARM)
    transq_unlock(lock);
#else
    int_unlock(lock);
#endif
    return ret;
}

enum HAL_TRANSQ_RET_T hal_transq_update_num(enum HAL_TRANSQ_ID_T id, const struct HAL_TRANSQ_SLOT_NUM_T *slot)
{
    uint32_t tx_mask;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (slot == NULL) {
        return HAL_TRANSQ_RET_BAD_SLOT;
    }
    if (slot->tx_num[HAL_TRANSQ_PRI_NORMAL] + slot->tx_num[HAL_TRANSQ_PRI_HIGH] > TRANSQ_SLOT_NUM) {
        return HAL_TRANSQ_RET_BAD_TX_NUM;
    }
    if (slot->rx_num[HAL_TRANSQ_PRI_NORMAL] + slot->rx_num[HAL_TRANSQ_PRI_HIGH] > TRANSQ_SLOT_NUM) {
        return HAL_TRANSQ_RET_BAD_RX_NUM;
    }

    transq_cfg[id].slot = *slot;

    rx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL] = construct_mask(0, slot->rx_num[HAL_TRANSQ_PRI_NORMAL]);
    rx_irq_mask[id][HAL_TRANSQ_PRI_HIGH] = construct_mask(TRANSQ_SLOT_NUM - slot->rx_num[HAL_TRANSQ_PRI_HIGH], slot->rx_num[HAL_TRANSQ_PRI_HIGH]);

    tx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL] = construct_mask(0, slot->tx_num[HAL_TRANSQ_PRI_NORMAL]);
    tx_irq_mask[id][HAL_TRANSQ_PRI_HIGH] = construct_mask(TRANSQ_SLOT_NUM - slot->tx_num[HAL_TRANSQ_PRI_HIGH], slot->tx_num[HAL_TRANSQ_PRI_HIGH]);
    tx_mask = tx_irq_mask[id][HAL_TRANSQ_PRI_NORMAL] | tx_irq_mask[id][HAL_TRANSQ_PRI_HIGH];

    transq[id]->RMT_INTMASK = tx_mask;
    transq[id]->LERR_INTMASK = tx_mask;
    if (transq_cfg[id].tx_handler) {
        transq[id]->LDONE_INTMASK = tx_mask;
    } else {
        transq[id]->LDONE_INTMASK = 0;
    }

    return HAL_TRANSQ_RET_OK;
}

enum HAL_TRANSQ_RET_T hal_transq_open(enum HAL_TRANSQ_ID_T id, const struct HAL_TRANSQ_CFG_T *cfg)
{
    const struct HAL_TRANSQ_SLOT_NUM_T *slot;
    uint32_t ctrl;
    enum HAL_TRANSQ_RET_T ret;

    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }
    if (cfg == NULL) {
        return HAL_TRANSQ_RET_BAD_CFG;
    }

    hal_cmu_clock_enable(transq_mod[id]);
    hal_cmu_reset_clear(transq_mod[id]);

    transq_cfg[id] = *cfg;

    slot = &cfg->slot;

    ret = hal_transq_update_num(id, slot);
    if (ret) {
        return ret;
    }

    next_tx_slot[id][HAL_TRANSQ_PRI_NORMAL] = slot->tx_num[HAL_TRANSQ_PRI_NORMAL] ? 0 : TRANSQ_SLOT_NUM;
    active_tx_slot[id][HAL_TRANSQ_PRI_NORMAL] = TRANSQ_SLOT_NUM;
    tx_slot_full[id][HAL_TRANSQ_PRI_NORMAL] = false;
    next_rx_slot[id][HAL_TRANSQ_PRI_NORMAL] = slot->rx_num[HAL_TRANSQ_PRI_NORMAL] ? 0 : TRANSQ_SLOT_NUM;

    next_tx_slot[id][HAL_TRANSQ_PRI_HIGH] = slot->tx_num[HAL_TRANSQ_PRI_HIGH] ?
        (TRANSQ_SLOT_NUM - slot->tx_num[HAL_TRANSQ_PRI_HIGH]) : TRANSQ_SLOT_NUM;
    active_tx_slot[id][HAL_TRANSQ_PRI_HIGH] = TRANSQ_SLOT_NUM;
    tx_slot_full[id][HAL_TRANSQ_PRI_HIGH] = false;
    next_rx_slot[id][HAL_TRANSQ_PRI_HIGH] = slot->rx_num[HAL_TRANSQ_PRI_HIGH] ?
        (TRANSQ_SLOT_NUM - slot->rx_num[HAL_TRANSQ_PRI_HIGH]) : TRANSQ_SLOT_NUM;

    transq[id]->RMT_ISC.RMT_INTCLR = ~0UL;
    transq[id]->LDONE_ISC.LDONE_INTCLR = ~0UL;
    transq[id]->LERR_ISC.LERR_INTCLR = ~0UL;

    ctrl = CTRL_REMOTE_IRQ_EN | CTRL_LOCAL_ERR_IRQ_EN;
    if (cfg->tx_handler) {
        ctrl |= CTRL_LOCAL_DONE_IRQ_EN;
    }
    transq[id]->CTRL = ctrl;

    if (cfg->rx_handler) {
        NVIC_SetVector(remote_irq_num[id], (uint32_t)hal_transq_remote_irq_handler);
        NVIC_SetPriority(remote_irq_num[id], IRQ_PRIORITY_NORMAL);
        NVIC_ClearPendingIRQ(remote_irq_num[id]);
        NVIC_EnableIRQ(remote_irq_num[id]);
    }

    NVIC_SetVector(local_irq_num[id], (uint32_t)hal_transq_local_irq_handler);
    NVIC_SetPriority(local_irq_num[id], IRQ_PRIORITY_NORMAL);
    NVIC_ClearPendingIRQ(local_irq_num[id]);
    NVIC_EnableIRQ(local_irq_num[id]);

#if defined(__ARM_ARCH_ISA_ARM)
    transq_lock_init();
#endif

    return HAL_TRANSQ_RET_OK;
}

enum HAL_TRANSQ_RET_T hal_transq_close(enum HAL_TRANSQ_ID_T id)
{
    if (id >= HAL_TRANSQ_ID_QTY) {
        return HAL_TRANSQ_RET_BAD_ID;
    }

    NVIC_DisableIRQ(remote_irq_num[id]);
    NVIC_DisableIRQ(local_irq_num[id]);

    transq[id]->CTRL = 0;

    hal_cmu_reset_set(transq_mod[id]);
    hal_cmu_clock_disable(transq_mod[id]);

    return HAL_TRANSQ_RET_OK;
}
enum HAL_TRANSQ_RET_T hal_transq_flush(enum HAL_TRANSQ_ID_T id)
{
    transq[id]->RMT_ISC.RMT_INTCLR = ~0UL;

    return HAL_TRANSQ_RET_OK;
}
uint8_t get_rx_irq_count(enum HAL_TRANSQ_ID_T id)
{
    return transq_cfg[id].rx_irq_count;
}

/* wifi specific */
struct wsm_hdr_t {
	uint16_t len;
	uint16_t id;
};

void show_reg(void)
{
    struct wsm_hdr_t * wsm;
    TRACE(0, " peer_transq[0]->RMT_MIS: 0x%08x, RMT_RIS: 0x%08x, RMT_INTMASK: 0x%08x, slot=%d ", 
        peer_transq[0]->RMT_MIS,
        peer_transq[0]->RMT_ISC.RMT_RIS,
        peer_transq[0]->RMT_INTMASK,
        next_rx_slot[0][0]);
    for(int i=0;i<32;i++)
    {
         wsm  = (struct wsm_hdr_t*) (peer_transq[0]->RSLOT[i].ADDR);
         if ((uint32_t)wsm >= 0x80000000 && (uint32_t)wsm <= 0x90000000)
            TRACE(0, "peer_transq[0]->RSLOT[%d].addr= 0x%08x, wsm->id: 0x%x, len= %d", i, (uint32_t)wsm, wsm->id,wsm->len);
         else
            TRACE(0, "error or unenabled transq msg. 0x%08x", (uint32_t)wsm);
    }
}
#endif
#endif // CHIP_HAS_TRANSQ
