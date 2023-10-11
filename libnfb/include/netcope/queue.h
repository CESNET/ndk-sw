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

#include "dma_ctrl_ndp.h"
#include "dma_ctrl_sze.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
enum queue_type {
	QUEUE_TYPE_UNDEF = -1,
	QUEUE_TYPE_SZE = 0,
	QUEUE_TYPE_NDP
};

// -------------- Counter control commands --------------------
#define CNTR_CMD_RST       0
#define CNTR_CMD_STRB      1
#define CNTR_CMD_STRB_RST  2

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_QUEUE_H */
