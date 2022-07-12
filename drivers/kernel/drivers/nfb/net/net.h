/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network interface driver of the NFB platform - main header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#ifndef NFB_NET_CORE_H
#define NFB_NET_CORE_H

#include "compat.h"

#include "../ndp/kndp.h"
#include "../boot/boot.h"

#include <netcope/ndp_priv.h>

#include <linux/mdio.h>
#include <linux/netdevice.h>

#include <netcope/rxmac.h>
#include <netcope/txmac.h>

#include <netcope/tsu.h>

#include <linux/ptp_clock_kernel.h>

struct nfb_net {
	struct device dev;
	struct nfb_device *nfbdev;
	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp_clock;
	struct nc_tsu *ptp_tsu_comp;
	unsigned long tsu_freq;

	struct list_head list_devices;

	unsigned rxqc;
	unsigned txqc;
};

// driver.c
int nfb_net_attach(struct nfb_device *nfbdev, void **priv);
void nfb_net_detach(struct nfb_device *nfbdev, void *priv);


enum nfb_net_device_flags {
	NFBNET_DISCARD,
	NFBNET_NOCARRIER,
	NFBNET_KEEPIFDOWN,
};

enum nfb_net_device_state {
	NFBNET_SERVICE_SCHED,
};

struct nfb_net_queue {
	struct nfb_net_device *priv;
	struct ndp_subscriber *ndps;
	struct task_struct *task;
	struct ndp_queue ndpq;
	int32_t numa;

	unsigned index;

	struct u64_stats_sync sync;
	u64 packets;
	u64 dropped;
	u64 errors;
	u64 bytes;
};

struct nfb_net_device {
	struct list_head list_item;

	int index;
	unsigned long state;
	unsigned long flags;

	struct device dev;
	struct nfb_net *module;
	struct nfb_device *nfbdev;
	struct net_device *netdev;

	struct nc_rxmac *nc_rxmac;
	struct nc_txmac *nc_txmac;

	struct nfb_comp *nc_trstat;
	struct nc_i2c_ctrl *nc_tri2c;
	unsigned trlanes;

	struct nc_mdio *nc_mdio;
	struct mdio_if_info mdio;

	unsigned rxqs_count;
	unsigned rxqs_offset;
	struct nfb_net_queue *rxqs;

	unsigned txqs_count;
	unsigned txqs_offset;
	struct nfb_net_queue *txqs;

	struct timer_list service_timer;
	struct work_struct service_task;
};

// device.c
struct nfb_net_device *nfb_net_device_create(struct nfb_net *module, int fdt_offset, int index);
void nfb_net_device_destroy(struct nfb_net_device *device);

// ethtool.c
void nfb_net_set_ethtool_ops(struct net_device *netdev);

// sysfs.c
int nfb_net_sysfs_init(struct nfb_net_device *device);
void nfb_net_sysfs_deinit(struct nfb_net_device *device);

#endif /* NFB_NET_CORE_H */
