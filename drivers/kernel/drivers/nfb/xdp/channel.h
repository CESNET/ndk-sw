/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - header file for channel
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#ifndef NFB_XDP_CHANNEL_H
#define NFB_XDP_CHANNEL_H

#include <linux/netdevice.h>

#define NFB_XDP_DESC_CNT 8192

struct nfb_xdp_queue {
	// dma controller
	struct xctrl *ctrl;

	// queue thread
	struct task_struct *thread;

	// napi structs - so far only xsk mode uses tx napi
	struct napi_struct napi;
};

// structure describing one queue pair
#define NFB_STATUS_IS_XSK BIT(0)
#define NFB_STATUS_IS_RUNNING BIT(1)
struct nfb_xdp_channel {
	struct nfb_ethdev *ethdev; // reference to ETH device holding this channel
	u16 index; // In the context of ETH device
	u16 nfb_index; // In the context of the card
	int numa; // numa node of the pci device

	struct nfb_xdp_queue txq;
	struct nfb_xdp_queue rxq;

	// Synchronize the state of rx and tx queue switching
	struct mutex state_mutex;
	unsigned long status;

	struct xsk_buff_pool *pool;
};

int channel_start_pp(struct nfb_xdp_channel *channel);
int channel_start_xsk(struct nfb_xdp_channel *channel);
int channel_stop(struct nfb_xdp_channel *channel);

#endif // NFB_XDP_CHANNEL_H
