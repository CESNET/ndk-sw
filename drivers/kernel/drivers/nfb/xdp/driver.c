/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - main driver module
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>

#include <libfdt.h>

#include "../nfb.h"
#include <netcope/eth.h>

#include "driver.h"
#include "ethdev.h"
#include "sysfs.h"

#define COMP_NETCOPE_RX "netcope,dma_ctrl_ndp_rx"
#define COMP_NETCOPE_TX "netcope,dma_ctrl_ndp_tx"

static bool xdp_enable = 0;

int nfb_xdp_attach(struct nfb_device *nfb, void **priv)
{
	struct nfb_xdp *module;
	u16 ethc, rxqc, txqc;
	int ret;

	if (!xdp_enable) {
		return 0;
	}

	// count the ports and queues
	ethc = nfb_comp_count(nfb, COMP_NETCOPE_ETH);
	if (ethc <= 0) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to attach: No eth interfaces available\n");
		return -EINVAL;
	}

	rxqc = nfb_comp_count(nfb, COMP_NETCOPE_RX);
	if (rxqc <= 0) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to attach: No RX queues available\n");
		return -EINVAL;
	}

	txqc = nfb_comp_count(nfb, COMP_NETCOPE_TX);
	if (txqc <= 0) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to attach: No TX queues available\n");
		return -EINVAL;
	}

	// sanity checks; we expect there will be the same amount of queue pairs for each eth port
	if (rxqc != txqc) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to attach: TX and RX queue count differs, xdp operates with queue pairs TXc: %u, RXc: %u\n", txqc, rxqc);
		return -EINVAL;
	}
	if (rxqc % ethc != 0) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to attach: Queue pairs are not divisible by ports, don't know how to initilize TXc: %u, RXc: %u, ETHc: %u\n", txqc, rxqc, ethc);
		return -EINVAL;
	}

	module = kzalloc(sizeof(*module), GFP_KERNEL);
	if (!module) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to alloc module\n");
		return -ENOMEM;
	}
	*priv = module;

	INIT_LIST_HEAD(&module->list_devices);

	module->ethc = ethc;
	module->channelc = rxqc;
	module->nfb = nfb;

	memset(&module->dev, 0, sizeof(struct device));
	device_initialize(&module->dev);
	module->dev.parent = nfb->dev;
	dev_set_name(&module->dev, "nfb_xdp");
	dev_set_drvdata(&module->dev, module);
	nfb_xdp_sysfs_init_module_attributes(module);
	ret = device_add(&module->dev);
	if (ret) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to add kernel device\n");
		goto err_dev_add;
	}

	ret = nfb_xdp_sysfs_init_channels(module);
	if (ret) {
		dev_warn(&nfb->pci->dev, "nfb_xdp: Failed to init sysfs\n");
		goto err_channel_sysfs;
	}

	dev_info(&nfb->pci->dev, "nfb_xdp: Successfully attached\n");
	return 0;

err_channel_sysfs:
err_dev_add:
	put_device(&module->dev);
	kfree(module);
	*priv = NULL;
	return ret;
}

void nfb_xdp_detach(struct nfb_device *nfb, void *priv)
{
	struct nfb_xdp *module = priv;

	if (!module) {
		return;
	}

	nfb_xdp_sysfs_deinit_channels(module);
	destroy_ethdev(module, -1);
	device_del(&module->dev);
	kfree(module);
	dev_info(&nfb->pci->dev, "nfb_xdp: detached\n");
}

module_param(xdp_enable, bool, S_IRUGO);
MODULE_PARM_DESC(xdp_enable, "Creates XDP capable netdevice for each Ethernet interface [no]");
