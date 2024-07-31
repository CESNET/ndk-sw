/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - public header
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#ifndef NFB_XDP_DRIVER_H
#define NFB_XDP_DRIVER_H

#include "../nfb.h"

int nfb_xdp_attach(struct nfb_device *nfb, void **priv);
void nfb_xdp_detach(struct nfb_device *nfb, void *priv);

// struct holding xdp driver module info
struct nfb_xdp {
	struct nfb_device *nfb; /*Top level structure describing nfb device*/
	struct device dev; /*device describing nfb_xdp module*/
	struct list_head list_devices; /*List of eth devices*/

	u16 ethc; /* Number of ETH ports*/
	u16 rxqc; /* Absolute number of rx queues */
	u16 txqc; /* Absolute number of tx queues */
};

#endif // NFB_XDP_DRIVER_H
