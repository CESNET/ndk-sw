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
	struct nfb_device *nfb; // Top level structure describing nfb device
	struct device dev; // Device describing nfb_xdp module, used with sysfs
	struct mutex list_mutex; // Mutex for list_devices
	struct list_head list_devices; // List of virtual ETH devices

	u16 ethc; // Number of physical ETH ports
	u16 channelc; // Number of usable channels

	struct device *channel_sysfsdevs; // Channel devices for sysfs
};

#endif // NFB_XDP_DRIVER_H
