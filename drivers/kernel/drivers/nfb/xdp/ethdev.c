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

static int nfb_xdp_channels_init(struct net_device *netdev)
{
	int i, ret = 0;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	// printk("LOG: %s called", __func__);
	ethdev->channels = kzalloc(sizeof(*ethdev->channels) * ethdev->channel_count, GFP_KERNEL);
	if (!ethdev->channels) {
		ret = -ENOMEM;
		goto err_channel_alloc;
	}

	for (i = 0; i < ethdev->channel_count; i++) {
		mutex_init(&ethdev->channels[i].state_mutex);
		ethdev->channels[i].ethdev = ethdev;
		ethdev->channels[i].index = i;
		ethdev->channels[i].nfb_index = i + ethdev->channel_count * ethdev->index;
		ethdev->channels[i].numa = dev_to_node(&ethdev->nfb->pci->dev);
#ifdef CONFIG_HAVE_NETIF_NAPI_ADD_WITH_WEIGHT
		netif_napi_add(netdev, &ethdev->channels[i].rxq.napi_pp, nfb_xctrl_napi_poll_pp, NAPI_POLL_WEIGHT);
		netif_napi_add(netdev, &ethdev->channels[i].rxq.napi_xsk, nfb_xctrl_napi_poll_rx_xsk, NAPI_POLL_WEIGHT);
#else
		netif_napi_add_weight(netdev, &ethdev->channels[i].rxq.napi_pp, nfb_xctrl_napi_poll_pp, NAPI_POLL_WEIGHT);
		netif_napi_add_weight(netdev, &ethdev->channels[i].rxq.napi_xsk, nfb_xctrl_napi_poll_rx_xsk, NAPI_POLL_WEIGHT);
#endif
#ifdef CONFIG_HAVE_NETIF_NAPI_ADD_TX_WEIGHT
		netif_napi_add_tx_weight(netdev, &ethdev->channels[i].txq.napi_xsk, nfb_xctrl_napi_poll_tx_xsk, NAPI_POLL_WEIGHT);
#else
		netif_tx_napi_add(netdev, &ethdev->channels[i].txq.napi_xsk, nfb_xctrl_napi_poll_tx_xsk, NAPI_POLL_WEIGHT);
#endif
	}

err_channel_alloc:
	return ret;
}

static void nfb_xdp_channels_deinit(struct net_device *netdev)
{
	u32 i;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	// printk("LOG: %s called", __func__);
	for (i = 0; i < ethdev->channel_count; i++) {
		netif_napi_del(&ethdev->channels[i].rxq.napi_pp);
		netif_napi_del(&ethdev->channels[i].rxq.napi_xsk);
		netif_napi_del(&ethdev->channels[i].txq.napi_xsk);
	}
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
	// printk("LOG: %s called", __func__);
	spin_lock(&ethdev->prog_lock);
	old_prog = rcu_replace_pointer(ethdev->prog, NULL, lockdep_is_held(&ethdev->prog_lock));
	if (old_prog) {
		bpf_prog_put(old_prog);
	}
	spin_unlock(&ethdev->prog_lock);
	synchronize_rcu();

	// Stop all TX queues
	netif_tx_stop_all_queues(netdev);

	// disable mac
	if (ethdev->nc_rxmac)
		nc_rxmac_disable(ethdev->nc_rxmac);
	if (ethdev->nc_txmac)
		nc_txmac_disable(ethdev->nc_txmac);

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
			goto err_channel_start;
		}
	}

	// enable mac
	if (ethdev->nc_rxmac)
		nc_rxmac_enable(ethdev->nc_rxmac);
	if (ethdev->nc_txmac)
		nc_txmac_enable(ethdev->nc_txmac);

	return ret;
err_channel_start:
	printk(KERN_ERR "nfb: failed to start channels\n");
	nfb_stop_channels(netdev);
	return ret;
}

static int nfb_xdp_open(struct net_device *netdev)
{
	int ret;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	// printk("LOG: %s called", __func__);
	ret = nfb_start_channels(netdev);
	schedule_work(&ethdev->link_work);
	mod_timer(&ethdev->link_timer, jiffies + HZ * 1);
	return ret;
}

static int nfb_xdp_stop(struct net_device *netdev)
{
	// printk("LOG: %s called\n", __func__);
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

void destroy_ethdev(struct nfb_ethdev *ethdev)
{
	struct net_device *netdev = ethdev->netdev;
	// printk("LOG: %s called\n", __func__);
	netif_carrier_off(netdev);
	unregister_netdev(netdev); // calls nfb_xdp_stop
	del_timer_sync(&ethdev->link_timer);
	cancel_work_sync(&ethdev->link_work);

	// close mac components
	if (ethdev->nc_rxmac)
		nc_rxmac_close(ethdev->nc_rxmac);
	if (ethdev->nc_txmac)
		nc_txmac_close(ethdev->nc_txmac);

	nfb_xdp_channels_deinit(netdev);
	free_netdev(netdev);
}

static void link_work_handler(struct work_struct *work)
{
	struct nfb_ethdev *ethdev = container_of(work, struct nfb_ethdev, link_work);
	int ok;
	if (ethdev->nc_rxmac) {
		ok = netif_carrier_ok(ethdev->netdev);
		if (nc_rxmac_get_link(ethdev->nc_rxmac)) {
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

struct nfb_ethdev *create_ethdev(struct nfb_xdp *module, int fdt_offset, u16 index)
{
	struct nfb_device *nfb = module->nfb;
	struct nfb_ethdev *ethdev;
	struct net_device *netdev;
	int fdt_comp;
	int ret;

	// allocate net_device
	netdev = alloc_etherdev_mqs(sizeof(*ethdev), module->txqc / module->ethc, module->rxqc / module->ethc);
	if (!netdev) {
		ret = -ENOMEM;
		goto err_alloc_etherdev;
	}

	// sets napi to threaded mode allowing for scheduler control
	// without this mode scheduler cannot really work with napi
	// so multiple napi softirqs can end up on the same cpu
	ret = dev_set_threaded(netdev, true);
	if (ret)
		printk(KERN_WARNING "nfb: Couldn't set NAPI threaded mode.\n");

	// Set the name of the interface
	snprintf(netdev->name, IFNAMSIZ - 1, "nfb%up%u", nfb->minor, index);

	// Initialize nfb_ethdev struct
	ethdev = netdev_priv(netdev);
	ethdev->index = index;
	ethdev->channel_count = module->rxqc / module->ethc;
	ethdev->module = module;
	ethdev->nfb = nfb;
	ethdev->netdev = netdev;

	// Initialize channels
	ret = nfb_xdp_channels_init(netdev);
	if (ret)
		goto err_channels_init;

	// NOTE: nc_mac_enable ~ nfb-eth -e1
	// open mac component
	fdt_comp = nc_eth_get_rxmac_node(nfb->fdt, fdt_offset);
	ethdev->nc_rxmac = nc_rxmac_open(nfb, fdt_comp);
	if (IS_ERR(ethdev->nc_rxmac))
		ethdev->nc_rxmac = NULL;

	fdt_comp = nc_eth_get_txmac_node(nfb->fdt, fdt_offset);
	ethdev->nc_txmac = nc_txmac_open(nfb, fdt_comp);
	if (IS_ERR(ethdev->nc_txmac))
		ethdev->nc_txmac = NULL;

	// init periodical checking of link status
	INIT_WORK(&ethdev->link_work, link_work_handler);
	timer_setup(&ethdev->link_timer, link_timer_callback, 0);
	netdev->netdev_ops = &netdev_ops;

	SET_NETDEV_DEV(netdev, &nfb->pci->dev);

	// set mac address
	nfb_net_set_dev_addr(nfb, netdev, index);
	// NOTE: Register netdev with all tx queues stopped.
	// 	Otherwise tx can be called when queues are not ready.
	netif_tx_stop_all_queues(netdev);
	// carrier needs to be manually set down on init else its the state will show up as UNKNOWN
	netif_carrier_off(netdev);
	ret = register_netdev(netdev); // calls nfb_xdp_open
	if (ret) {
		goto err_register_netdev;
	}

	return ethdev;

err_register_netdev:
	nfb_xdp_channels_deinit(netdev);
	if (ethdev->nc_rxmac)
		nc_rxmac_close(ethdev->nc_rxmac);
	if (ethdev->nc_txmac)
		nc_txmac_close(ethdev->nc_txmac);
err_channels_init:
	free_netdev(netdev);
err_alloc_etherdev:
	return NULL;
}
