/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - main module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <libfdt.h>

#include "ndp.h"
#include "../nfb.h"
#include "../pci.h"
#include "../fdt.h"

struct ndp_ctrl_create {
	const char *compatible;
	struct ndp_channel* (*create)(struct ndp *ndp, int index, int node_offset);
};

struct ndp_ctrl_create ndp_ctrls[] = {
	{.compatible = "netcope,dma_ctrl_sze_rx", .create = ndp_ctrl_v1_create_rx},
	{.compatible = "netcope,dma_ctrl_sze_tx", .create = ndp_ctrl_v1_create_tx},
	{.compatible = "netcope,dma_ctrl_ndp_rx", .create = ndp_ctrl_v2_create_rx},
	{.compatible = "netcope,dma_ctrl_ndp_tx", .create = ndp_ctrl_v2_create_tx},
	{.compatible = "cesnet,dma_ctrl_calypte_rx", .create = ndp_ctrl_v3_create_rx},
	{.compatible = "cesnet,dma_ctrl_calypte_tx", .create = ndp_ctrl_v3_create_tx},
};

static void ndp_create_channels_from_ctrl(struct ndp *ndp, struct ndp_ctrl_create *ctrl)
{
	int index = 0;
	int node_offset;
	void *fdt = ndp->nfb->fdt;
	struct ndp_channel *channel;

	char path[MAX_FDT_PATH_LENGTH];
	uint32_t phandle;

	fdt_for_each_compatible_node(fdt, node_offset, ctrl->compatible) {
		if (fdt_get_path(fdt, node_offset, path, MAX_FDT_PATH_LENGTH) >= 0) {
			phandle = fdt_get_phandle(fdt, node_offset);
			if (phandle == 0) {
				if (_fdt_generate_phandle(fdt, &phandle))
					return;
				if (fdt_setprop_u32(fdt, node_offset, "phandle", phandle))
					return;
			}

			channel = ctrl->create(ndp, index, node_offset);
			if (!IS_ERR(channel))
				ndp_channel_add(channel, ndp, phandle);
			node_offset = fdt_path_offset(fdt, path);
		}
		index++;
	}
}

int nfb_ndp_attach(struct nfb_device *nfb, void **priv)
{
	int i;
	int ret = 0;
	int fdt_offset;
	struct ndp *ndp;

	ndp = kzalloc(sizeof(*ndp), GFP_KERNEL);
	if (ndp == NULL) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	mutex_init(&ndp->lock);
	INIT_LIST_HEAD(&ndp->list_channels);
	INIT_LIST_HEAD(&ndp->list_subscribers);
	ndp->nfb = nfb;
	*priv = ndp;

	device_initialize(&ndp->dev);
	ndp->dev.parent = ndp->nfb->dev;
	dev_set_name(&ndp->dev, "ndp");
	dev_set_drvdata(&ndp->dev, ndp);
	ret = device_add(&ndp->dev);
	if (ret)
		goto err_device_add;

	fdt_offset = fdt_path_offset(ndp->nfb->fdt, "/drivers");
	fdt_offset = fdt_add_subnode(ndp->nfb->fdt, fdt_offset, "ndp");
	fdt_setprop_u32(ndp->nfb->fdt, fdt_offset, "version", 0x1);
	fdt_add_subnode(ndp->nfb->fdt, fdt_offset, "tx_queues");
	fdt_add_subnode(ndp->nfb->fdt, fdt_offset, "rx_queues");

	for (i = 0; i < sizeof(ndp_ctrls) / sizeof(ndp_ctrls[0]); i++) {
		ndp_create_channels_from_ctrl(ndp, &ndp_ctrls[i]);
	}

	dev_info(&nfb->pci->dev, "nfb_ndp: attached successfully\n");

	return ret;

//	fdt_offset = fdt_path_offset(ndp->nfb->fdt, "/drivers/ndp");
//	fdt_del_node(ndp->nfb->fdt, fdt_offset);

//	device_del(&ndp->dev);
err_device_add:
	kfree(ndp);
err_alloc:
	return ret;
}

/**
 * ndp_destroy - cleanup ndp structure
 * @sd: structure to cleanup
 *
 * TODO
 */
void nfb_ndp_detach(struct nfb_device *nfb, void *priv)
{
	int fdt_offset;
	struct ndp *ndp;
	struct ndp_channel *channel, *tmp;
	ndp = priv;

	mutex_lock(&ndp->lock);
	if (!list_empty(&ndp->list_subscribers)) {
		dev_err(nfb->dev, "NDP: Destroyed before list_subscribers empty\n");
	}
	mutex_unlock(&ndp->lock);

	list_for_each_entry_safe(channel, tmp, &ndp->list_channels, list_ndp) {
		ndp_channel_del(channel);
	}

	if (!list_empty(&ndp->list_channels)) {
		dev_err(nfb->dev, "NDP: Destroyed before list_channels empty\n");
	}

	fdt_offset = fdt_path_offset(nfb->fdt, "/drivers/ndp");
	fdt_del_node(nfb->fdt, fdt_offset);

	device_del(&ndp->dev);

	kfree(ndp);
}
