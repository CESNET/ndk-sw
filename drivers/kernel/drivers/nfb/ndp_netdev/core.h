/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP backend network interface driver of the NFB platform - main header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Dominik Tran <xtrand00@stud.fit.vutbr.cz>
 */

#ifndef NFB_NDP_NETDEV_CORE_H
#define NFB_NDP_NETDEV_CORE_H

#include <linux/netdevice.h>
#include "../ndp/kndp.h"

#include <nfb/ndp.h>

struct nfb_mod_ndp_netdev {
	struct nfb_device *nfb;
	struct list_head list_ethdev;
	struct device dev;
};

struct nfb_ndp_netdev {
	struct nfb_device *nfb;
	struct nfb_mod_ndp_netdev *eth;
	struct net_device *ndev;
	struct list_head list_item;
	struct ndp_queue *tx_q;
	struct ndp_queue *rx_q;
	int index;
	struct task_struct *rx_task;
	struct net_device_stats ndev_stats;
	struct device device;
};

int nfb_ndp_netdev_attach(struct nfb_device *nfb, void **priv);
void nfb_ndp_netdev_detach(struct nfb_device *nfb, void *priv);

#endif /* NFB_NDP_NETDEV_CORE_H */
