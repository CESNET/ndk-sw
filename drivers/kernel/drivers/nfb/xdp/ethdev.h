/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - header file for ethdev
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#ifndef NFB_XDP_ETHDEV_H
#define NFB_XDP_ETHDEV_H

#include <linux/netdevice.h>
#include "../nfb.h"

// structure describing one ETH device
struct nfb_ethdev {
	struct list_head list; // List of eth devices
	struct nfb_device *nfb; // Top level driver struct
	struct nfb_xdp *module; // module info
	struct net_device *netdev;
	struct device sysfsdev;

	int index; // index of ETH device

	u16 channel_count;
	struct nfb_xdp_channel *channels;

	// work setting interface up or down based on the state of the mac
	struct timer_list link_timer;
	struct work_struct link_work;

	// nfb components
	u16 mac_count; // XDP netdevice can span multiple physical interfaces
	struct nc_rxmac **nc_rxmacs;

	// prog is rcu protected pointer
	struct bpf_prog *prog; // xdp prog
	spinlock_t prog_lock;
};

int create_ethdev(struct nfb_xdp *module, u16 index, unsigned * channel_indexes, unsigned channel_count);
int destroy_ethdev(struct nfb_xdp *module, int index);
#endif // NFB_XDP_ETHDEV
