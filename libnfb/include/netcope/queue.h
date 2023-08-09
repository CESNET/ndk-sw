/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - queue private definitions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_QUEUE_H
#define NETCOPE_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
enum queue_type {
	QUEUE_TYPE_UNDEF = -1,
	QUEUE_TYPE_SZE = 0,
	QUEUE_TYPE_NDP
};

#define DMA_CTRL_NDP_REG_CONTROL      0x00
#define DMA_CTRL_NDP_REG_STATUS       0x04
#define DMA_CTRL_NDP_REG_SDP          0x10
#define DMA_CTRL_NDP_REG_SHP          0x14
#define DMA_CTRL_NDP_REG_HDP          0x18
#define DMA_CTRL_NDP_REG_HHP          0x1C
#define DMA_CTRL_NDP_REG_TIMEOUT      0x20
#define DMA_CTRL_NDP_REG_DESC_BASE    0x40
#define DMA_CTRL_NDP_REG_HDR_BASE     0x48
#define DMA_CTRL_NDP_REG_UPDATE_BASE  0x4C
#define DMA_CTRL_NDP_REG_MDP          0x58
#define DMA_CTRL_NDP_REG_MHP          0x5C

#define DMA_CTRL_NDP_CNTR_CMD_RST       0
#define DMA_CTRL_NDP_CNTR_CMD_STRB      1
#define DMA_CTRL_NDP_CNTR_CMD_STRB_RST  2

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_QUEUE_H */
