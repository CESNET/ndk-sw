/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * DMA Controller - SZE/v1 type
 *
 * Copyright (C) 2023-2023 CESNET
 * Author(s):
 *   Vladislav Valek <valekv@cesnet.cz>
 */

#ifndef DMA_CTRL_SZE_H
#define DMA_CTRL_SZE_H

#ifdef __cplusplus
extern "C" {
#endif

// ----------------- Contro/status registers ----------
#define SZE_CTRL_REG_CONTROL      0x00
	#define SZE_CTRL_REG_CONTROL_STOP	0x0
	#define SZE_CTRL_REG_CONTROL_START	0x1
	#define SZE_CTRL_REG_CONTROL_DISCARD	0x2
#define SZE_CTRL_REG_STATUS       0x04
	#define SZE_CTRL_REG_STATUS_RUNNING	0x1
#define SZE_CTRL_REG_SW_POINTER   0x08
#define SZE_CTRL_REG_HW_POINTER   0x0C
#define SZE_CTRL_REG_BUFFER_SIZE  0x10
#define SZE_CTRL_REG_IRQ          0x14
	#define SZE_CTRL_REG_IRQ_TIMEOUTE	0x1
	#define SZE_CTRL_REG_IRQ_PTRE		0x2
#define SZE_CTRL_REG_TIMEOUT      0x18
#define SZE_CTRL_REG_MAX_REQUEST  0x1C
#define SZE_CTRL_REG_DESC_BASE    0x20
#define SZE_CTRL_REG_UPDATE_BASE  0x28

// ----------------- Counters ------------------------
// Processed packets on TX
#define SZE_CTRL_REG_CNTR_SENT    0x30
// Processed packets on RX
#define SZE_CTRL_REG_CNTR_RECV    0x30
// Discarded packets
#define SZE_CTRL_REG_CNTR_DISC    0x38

// ---------------- Other parameters -----------------
#define SZE_CTRL_UPDATE_SIZE (PAGE_SIZE)
#define SZE_CTRL_DESC_PTR    0x1

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* DMA_CTRL_SZE_H */
