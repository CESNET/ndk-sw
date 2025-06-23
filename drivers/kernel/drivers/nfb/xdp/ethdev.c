/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - ethdev module 
 *  ethdev corresponds to single physical port on NIC and linux network interface 
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "../nfb.h"

#include <netcope/eth.h>
#include <netcope/rxmac.h>
#include <netcope/txmac.h>

#include "driver.h"
#include "ethdev.h"
#include "channel.h"
#include "ctrl_xdp.h"
#include "sysfs.h"

static int nfb_xdp_channels_init(struct net_device *netdev, unsigned *channel_indexes, unsigned channel_count)
{
	int nfb_idx = 0, map_idx = 0, ch_idx = 0, ret = 0;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	struct nfb_xdp *module = ethdev->module;

	ethdev->channels = kzalloc(sizeof(*ethdev->channels) * channel_count, GFP_KERNEL);
	if (!ethdev->channels) {
		ret = -ENOMEM;
		goto err_channel_alloc;
	}

	// Map the queues - lowest index to lowest index.
	for (nfb_idx = 0; nfb_idx < module->channelc; nfb_idx++) {
		for (ch_idx = 0; ch_idx < channel_count; ch_idx++) {
			if(nfb_idx == channel_indexes[ch_idx]) {
				mutex_init(&ethdev->channels[map_idx].state_mutex);
				ethdev->channels[map_idx].ethdev = ethdev;
				ethdev->channels[map_idx].index = map_idx;
				ethdev->channels[map_idx].nfb_index = nfb_idx;
				ethdev->channels[map_idx].numa = dev_to_node(&ethdev->nfb->pci->dev);
				map_idx++;
			}
		}
	}

err_channel_alloc:
	return ret;
}

static void nfb_xdp_channels_deinit(struct net_device *netdev)
{
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	kfree(ethdev->channels);
}

#ifndef CONFIG_HAVE_XDP_SET_FEATURES_FLAG
#ifdef CONFIG_HAVE_XDP_FEATURES_T
static void xdp_set_features_flag(struct net_device *dev, xdp_features_t val)
{
	if (dev->xdp_features == val)
		return;

	dev->xdp_features = val;

	if (dev->reg_state == NETREG_REGISTERED)
		call_netdevice_notifiers(NETDEV_XDP_FEAT_CHANGE, dev);
}
#endif
#endif

static void nfb_stop_channels(struct net_device *netdev)
{
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	struct nfb_xdp_channel *channel;
	unsigned i;
	struct bpf_prog *old_prog;

	spin_lock(&ethdev->prog_lock);
	old_prog = rcu_replace_pointer(ethdev->prog, NULL, lockdep_is_held(&ethdev->prog_lock));
	if (old_prog) {
		bpf_prog_put(old_prog);
	}
	spin_unlock(&ethdev->prog_lock);
	synchronize_rcu();

	// Stop all TX queues
	netif_tx_stop_all_queues(netdev);

	// Stop all threads
	for (i = 0; i < ethdev->channel_count; i++) {
		channel = &ethdev->channels[i];
		channel_stop(channel);
	}
}

static int nfb_start_channels(struct net_device *netdev)
{
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	struct nfb_xdp_channel *channel;
	int i, ret;

#ifdef CONFIG_HAVE_XDP_FEATURES_T
	// TODO:
	// Support segmented packets
	// set XDP capabilities for netdevice
	// Only works on newer kernels
	xdp_features_t val;
	val = NETDEV_XDP_ACT_BASIC
		| NETDEV_XDP_ACT_REDIRECT
		| NETDEV_XDP_ACT_XSK_ZEROCOPY
		// | NETDEV_XDP_ACT_RX_SG
		| NETDEV_XDP_ACT_NDO_XMIT
		// | NETDEV_XDP_ACT_NDO_XMIT_SG
		;
	xdp_set_features_flag(netdev, val);
#endif

	// Threads take care of setting up and tearing down the queues as XDP demands the abillity to do that on the fly
	for (i = 0; i < ethdev->channel_count; i++) {
		channel = &ethdev->channels[i];
		if ((ret = channel_start_pp(channel))) {
			printk(KERN_ERR "nfb: failed to start channels\n");
		}
	}

	return ret;
}

static int nfb_xdp_open(struct net_device *netdev)
{
	int ret;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	ret = nfb_start_channels(netdev);
	schedule_work(&ethdev->link_work);
	mod_timer(&ethdev->link_timer, jiffies + HZ * 1);
	return ret;
}

static int nfb_xdp_stop(struct net_device *netdev)
{
	nfb_stop_channels(netdev);
	return 0;
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = nfb_xdp_open,
	.ndo_stop = nfb_xdp_stop,
	.ndo_start_xmit = nfb_xctrl_start_xmit,
	// .ndo_get_stats64 = nfb_xdp_get_stats,
	.ndo_bpf = nfb_xdp,
	.ndo_xdp_xmit = nfb_xctrl_xdp_xmit,
	.ndo_xsk_wakeup = nfb_xsk_wakeup,
};

// Destroy xdp netdev, index == -1 means destroy everything
int destroy_ethdev(struct nfb_xdp *module, int index)
{
	int i, ret;
	struct nfb_ethdev *ethdev, *tmp;
	struct net_device *netdev;

	ret = -ENODEV;
	mutex_lock(&module->list_mutex);
	{
		list_for_each_entry_safe(ethdev, tmp, &module->list_devices, list) {
			if(index == ethdev->index || index == -1) {
				ret = 0;
				netdev = ethdev->netdev;
				list_del(&ethdev->list);
				del_timer_sync(&ethdev->link_timer);
				cancel_work_sync(&ethdev->link_work);
				netif_carrier_off(netdev);
				// close mac components
				for (i = 0; i < ethdev->mac_count; i++) {
					if(ethdev->nc_rxmacs[i])
						nc_rxmac_close(ethdev->nc_rxmacs[i]);
				}
				kfree(ethdev->nc_rxmacs);
				// calls nfb_xdp_stop
				unregister_netdev(netdev);
				nfb_xdp_channels_deinit(netdev);
				free_netdev(netdev);
			}
		}
	}
	mutex_unlock(&module->list_mutex);
	return ret;
}

static void link_work_handler(struct work_struct *work)
{
	struct nfb_ethdev *ethdev = container_of(work, struct nfb_ethdev, link_work);
	int ok, link, i;

	if(ethdev->mac_count) {
		// Check if all the opened macs are up
		link = 1;
		for (i = 0; i < ethdev->mac_count; i++) {
			if (ethdev->nc_rxmacs[i]) {
				if(!nc_rxmac_get_link(ethdev->nc_rxmacs[i])) {
					link = 0;
				}
			}
		}
		ok = netif_carrier_ok(ethdev->netdev);
		if (link) {
			if (!ok) {
				netif_carrier_on(ethdev->netdev);
			}
		} else {
			if (ok) {
				netif_carrier_off(ethdev->netdev);
			}
		}
	}
}

static void link_timer_callback(struct timer_list *timer)
{
	struct nfb_ethdev *ethdev = container_of(timer, struct nfb_ethdev, link_timer);
	schedule_work(&ethdev->link_work);
	mod_timer(&ethdev->link_timer, jiffies + HZ * 1);
}

int create_ethdev(struct nfb_xdp *module, u16 index, unsigned * channel_indexes, unsigned channel_count)
{
	struct nfb_device *nfb = module->nfb;
	struct nfb_ethdev *ethdev, *tmp;
	struct net_device *netdev;
	int i, j, mac_idx;
	int fdt_offset;
	int ret;
	unsigned channel_index;
	
	mutex_lock(&module->list_mutex);
	{
		// Check if index and queues are not already in use
		list_for_each_entry_safe(ethdev, tmp, &module->list_devices, list) {
			if(ethdev->index == index) {
				dev_warn(&module->dev, "Failed to add XDP device, another device of index %d already exists.\n", index);
				ret = -EINVAL;
				goto err_sanity;
			}
			for (i = 0; i < channel_count; i++) {
				for (j = 0; j < ethdev->channel_count; j++) {
					if(channel_indexes[i] == ethdev->channels[j].nfb_index) {
						dev_warn(&module->dev, "Failed to add XDP device, queue %d is already used by another XDP device.\n", i);
						ret = -EINVAL;
						goto err_sanity;
					}
				}
			}
		}

		// Allocate net_device
		netdev = alloc_etherdev_mqs(sizeof(*ethdev), channel_count, channel_count);
		if (!netdev) {
			dev_warn(&module->dev, "Failed to add XDP device, error allocating netdevice\n");
			ret = -ENOMEM;
			goto err_alloc_etherdev;
		}

		// sets napi to threaded mode allowing for scheduler control
		// without this mode scheduler cannot really work with napi
		// so multiple napi softirqs can end up on the same cpu
		ret = dev_set_threaded(netdev, true);
		if (ret)
			dev_warn(&module->dev, "Failed to allocate netdevice\n");

		// Set the name of the interface
		snprintf(netdev->name, IFNAMSIZ, "nfb%ux%u", nfb->minor, index);

		// Initialize nfb_ethdev struct
		ethdev = netdev_priv(netdev);
		ethdev->index = index;
		ethdev->channel_count = channel_count;
		ethdev->module = module;
		ethdev->nfb = nfb;
		ethdev->netdev = netdev;

		// Initialize channels
		if ((ret = nfb_xdp_channels_init(netdev, channel_indexes, channel_count))) {
			dev_warn(&module->dev, "Failed to add XDP device, error initializing channels\n");
			goto err_channels_init;
		}

		// NOTE: XDP netdevice can have multiple macs, to set link all relevant macs need to be considered.
		// open rx mac component
		if(!(ethdev->nc_rxmacs = kzalloc(sizeof(struct nc_rxmac *) * module->ethc, GFP_KERNEL))) {
			ret = -ENOMEM;
			goto macs_alloc_error;
		}

		for (mac_idx = 0; mac_idx < module->ethc; mac_idx++) {
			for (i = 0; i < channel_count; i++) {
				channel_index = channel_indexes[i];
				// If channel with the mac index is found, open the mac and test for next one
				if (mac_idx == channel_index / (module->channelc / module->ethc)) {
					if((fdt_offset = nfb_comp_find(nfb, "netcope,rxmac", mac_idx) < 0)) {
						ethdev->nc_rxmacs[ethdev->mac_count] = NULL;
						dev_warn(&module->dev, "Failed to add XDP device, error finding mac offset\n");
						ret = -ENODEV;
						goto macs_open_error;
					}

					ethdev->nc_rxmacs[ethdev->mac_count] = nc_rxmac_open(nfb, fdt_offset);
					if (IS_ERR(ethdev->nc_rxmacs[ethdev->mac_count])) {
						ethdev->nc_rxmacs[ethdev->mac_count] = NULL;
						dev_warn(&module->dev, "Failed to add XDP device, error opening mac\n");
						ret = -ENODEV;
						goto macs_open_error;
					}
					ethdev->mac_count++;
					break;
				}
			}
		}


		SET_NETDEV_DEV(netdev, &nfb->pci->dev);

		// set mac address
		nfb_net_set_dev_addr(nfb, netdev, index);
		// NOTE: Register netdev with all tx queues stopped.
		// 	Otherwise tx can be called when queues are not ready.
		netif_tx_stop_all_queues(netdev);
		// carrier needs to be manually set down on init else its state will show up as UNKNOWN
		netif_carrier_off(netdev);
		// Init periodical checking of link status
		INIT_WORK(&ethdev->link_work, link_work_handler);
		timer_setup(&ethdev->link_timer, link_timer_callback, 0);
		netdev->netdev_ops = &netdev_ops;

		// calls nfb_xdp_open
		if ((ret = register_netdev(netdev))) {
			dev_warn(&module->dev, "Failed to add XDP device, error registering netdevice.\n");
			goto err_register_netdev;
		}
		list_add_tail(&ethdev->list, &module->list_devices);
	}
	mutex_unlock(&module->list_mutex);
	return ret;

err_register_netdev:
	del_timer_sync(&ethdev->link_timer);
	cancel_work_sync(&ethdev->link_work);
macs_open_error:
macs_alloc_error:
	for (i = 0; i < ethdev->mac_count; i++) {
		if(ethdev->nc_rxmacs[i])
			nc_rxmac_close(ethdev->nc_rxmacs[i]);
	}
	kfree(ethdev->nc_rxmacs);
	nfb_xdp_channels_deinit(netdev);
err_channels_init:
	free_netdev(netdev);
err_alloc_etherdev:
err_sanity:
	mutex_unlock(&module->list_mutex);
	return ret;
}
