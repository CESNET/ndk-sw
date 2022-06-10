/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network interface driver of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

//#define NFB_DEBUG

#include <libfdt.h>

#include "../nfb.h"

#include "net.h"

#include <linux/pci.h>
#include <linux/mdio.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/u64_stats_sync.h>

#include <netcope/eth.h>
#include <netcope/mdio.h>
#include <netcope/rxmac.h>
#include <netcope/txmac.h>
#include <netcope/i2c_ctrl.h>
#include <netcope/transceiver.h>


static bool net_mac_control = 1;
module_param(net_mac_control, bool, S_IRUGO);
MODULE_PARM_DESC(net_mac_control, "Control also RX and TX MAC through created interfaces [yes]");

static bool net_transceiver_control = 1;
module_param(net_transceiver_control, bool, S_IRUGO);
MODULE_PARM_DESC(net_transceiver_control, "Control also transceiver when setting netdevs up or down [yes]");

static bool net_nocarrier = 0;
module_param(net_nocarrier, bool, S_IRUGO);
MODULE_PARM_DESC(net_nocarrier, "Default carrier state (force nocarrier for newly created interfaces) [off]");

static bool net_keepifdown = 0;
module_param(net_keepifdown, bool, S_IRUGO);
MODULE_PARM_DESC(net_keepifdown, "Default interface state (keep newly created interfaces down until explicitly enabled) [off]");

static uint net_rxqs_count = 0;
module_param(net_rxqs_count, uint, S_IRUGO);
MODULE_PARM_DESC(net_rxqs_count, "Default RX DMA queues count (per device) [0]");

static uint net_txqs_count = 0;
module_param(net_txqs_count, uint, S_IRUGO);
MODULE_PARM_DESC(net_txqs_count, "Default TX DMA queues count (per device) [0]");

static int net_rxqs_offset = 0;
module_param(net_rxqs_offset, int, S_IRUGO);
MODULE_PARM_DESC(net_rxqs_offset, "Default RX DMA queues offset [0]");

static int net_txqs_offset = 0;
module_param(net_txqs_offset, int, S_IRUGO);
MODULE_PARM_DESC(net_txqs_offset, "Default TX DMA queues offset [0]");


static void nfb_net_link_status(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (test_bit(NFBNET_NOCARRIER, &priv->flags)) {
		netif_carrier_off(netdev);
		return;
	}

	if (priv->nc_rxmac) {
		if (nc_rxmac_get_link(priv->nc_rxmac)) {
			if (netif_carrier_ok(netdev)) return;
			netif_carrier_on(netdev);
		} else {
			if (!netif_carrier_ok(netdev)) return;
			netif_carrier_off(netdev);
		}
	}
}


static const int SFF8636_STXDISABLE = 86;
static void nfb_net_transceiver_on(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	uint8_t currval;

	if (!net_transceiver_control)
		return;

	if (!priv->nc_trstat)
		return;

	if (!nc_transceiver_statusreg_is_present(priv->nc_trstat))
		return;

	// TODO: check transceiver type!
	// TODO: expect QSFP28->I2C->SFF_8636

	if (!priv->nc_tri2c)
		return;

	nc_i2c_set_addr(priv->nc_tri2c, 0xA0);

	nc_i2c_read_reg(priv->nc_tri2c, SFF8636_STXDISABLE, &currval, 1);
	currval &= ~priv->trlanes;
	nc_i2c_write_reg(priv->nc_tri2c, SFF8636_STXDISABLE, &currval, 1);
}


static void nfb_net_transceiver_off(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	uint8_t currval;

	// Empty operation if transceiver control not enabled
	if (!net_transceiver_control)
		return;

	if (!priv->nc_trstat)
		return;

	if (!nc_transceiver_statusreg_is_present(priv->nc_trstat))
		return;

	// TODO: check transceiver type!
	// TODO: expect QSFP28->I2C->SFF_8636

	if (!priv->nc_tri2c)
		return;

	nc_i2c_set_addr(priv->nc_tri2c, 0xA0);

	nc_i2c_read_reg(priv->nc_tri2c, SFF8636_STXDISABLE, &currval, 1);
	currval |= priv->trlanes;
	nc_i2c_write_reg(priv->nc_tri2c, SFF8636_STXDISABLE, &currval, 1);
}


static void nfb_net_mac_on(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	// Empty operation if MAC control not enabled
	if (!net_mac_control) return;

	// Enable RXMAC if available
	if (priv->nc_rxmac)
		nc_rxmac_enable(priv->nc_rxmac);

	// Enable TXMAC if available
	if (priv->nc_txmac)
		nc_txmac_enable(priv->nc_txmac);
}


static void nfb_net_mac_off(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	// Empty operation if MAC control not enabled
	if (!net_mac_control) return;

	// Disable RXMAC if available
	if (priv->nc_rxmac)
		nc_rxmac_disable(priv->nc_rxmac);

	// Disable TXMAC if available
	if (priv->nc_txmac)
		nc_txmac_disable(priv->nc_txmac);
}


#ifdef CONFIG_HAS_TIMER_SETUP
static void nfb_net_service_timer(struct timer_list *t)
{
	struct nfb_net_device *priv = from_timer(priv, t, service_timer);
#else
static void nfb_net_service_timer(unsigned long data)
{
	struct nfb_net_device *priv = (struct nfb_net_device *) data;
#endif
	unsigned long next_event_offset = 1 * HZ;

	// Reset the timer
	mod_timer(&priv->service_timer, next_event_offset + jiffies);

	// Schedule service task
	if (netif_running(priv->netdev) &&
		!test_and_set_bit(NFBNET_SERVICE_SCHED, &priv->state)) {
		schedule_work(&priv->service_task);
	}
}


static void nfb_net_service_task(struct work_struct *work)
{
	struct nfb_net_device *priv = container_of(work, struct nfb_net_device, service_task);

	// Do nothing if interface is down
	if (!netif_running(priv->netdev)) {
		return;
	}

	// Update link status
	nfb_net_link_status(priv->netdev);

	BUG_ON(!test_bit(NFBNET_SERVICE_SCHED, &priv->state));

	// Flush service flag
	smp_mb__before_atomic();
	clear_bit(NFBNET_SERVICE_SCHED, &priv->state);
}


u32 nfb_net_get_link(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (priv->nc_rxmac) {
		return nc_rxmac_get_link(priv->nc_rxmac);
	}

	return 0;
}


static void nfb_set_rx_mode(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (!net_mac_control)
		return;

	// Full promisc mode
	if (netdev->flags & IFF_PROMISC) {
		nc_rxmac_mac_filter_enable(priv->nc_rxmac, RXMAC_MAC_FILTER_PROMISCUOUS);

	// Multicast (broadcast included) + unicast
	} else if (netdev->flags & (IFF_MULTICAST | IFF_ALLMULTI)) {
		nc_rxmac_mac_filter_enable(priv->nc_rxmac, RXMAC_MAC_FILTER_TABLE_BCAST_MCAST);

	// Broadcast + unicast
	} else if (netdev->flags & IFF_BROADCAST) {
		nc_rxmac_mac_filter_enable(priv->nc_rxmac, RXMAC_MAC_FILTER_TABLE_BCAST);

	// Unicast only
	} else {
		nc_rxmac_mac_filter_enable(priv->nc_rxmac, RXMAC_MAC_FILTER_TABLE);
	}

	// TODO: support for multicast, unicast addr lists
}


static void nfb_net_commit_mac_address(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	u64 new_mac = 0;

	// Changing the format to little-endians
	memcpy(&new_mac, netdev->dev_addr, ETH_ALEN);
#ifdef __BIG_ENDIAN
	new_mac >>= 16;
#else
	new_mac <<= 16;
#endif
	be64_to_cpus(&new_mac);
	cpu_to_le64s(&new_mac);

	// Write new MAC into hardware (index 0)
	nc_rxmac_set_mac(priv->nc_rxmac, 0, new_mac, 1);
}


static int nfb_net_set_mac_address(struct net_device *netdev, void *p)
{
	// Check MAC validity, if device supports live change
	int ret = eth_prepare_mac_addr_change(netdev, p);
	if (ret < 0)
		return ret;

	// Write new MAC into netdev structure
	eth_commit_mac_addr_change(netdev, p);

	if (!net_mac_control)
		return 0;

	// Write new MAC into hardware
	nfb_net_commit_mac_address(netdev);

	return 0;
}


static int nfb_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct nc_rxmac *nc_rxmac = priv->nc_rxmac;
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	// MTU < 68 is an error see default eth_change_mtu()
	if ((new_mtu < 68) || (max_frame > nc_rxmac->mtu))
		return -EINVAL;

	// Write new MTU into netdev structure
	netdev->mtu = new_mtu;

	if (!net_mac_control)
		return 0;

	// Write new MTU into hardware
	nc_rxmac_set_frame_length(nc_rxmac, max_frame, RXMAC_REG_FRAME_LEN_MAX);

	return 0;
}


#ifdef CONFIG_HAS_VOID_NDO_GET_STATS64
static void nfb_net_get_stats(struct net_device *netdev, struct rtnl_link_stats64 *total)
#else
static struct rtnl_link_stats64* nfb_net_get_stats(struct net_device *netdev, struct rtnl_link_stats64 *total)
#endif
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	unsigned int i;

	rcu_read_lock();

	for (i = 0; i < priv->module->rxqc; i++) {
		struct nfb_net_queue *rxq = &priv->rxqs[i];
		u64 packets, dropped, errors, bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&rxq->sync);
			packets = rxq->packets;
			dropped = rxq->dropped;
			errors = rxq->errors;
			bytes = rxq->bytes;
		} while (u64_stats_fetch_retry(&rxq->sync, start));

		total->rx_packets += packets;
		total->rx_dropped += dropped;
		total->rx_errors += errors;
		total->rx_bytes += bytes;
	}

	for (i = 0; i < priv->module->txqc; i++) {
		struct nfb_net_queue *txq = &priv->txqs[i];
		u64 packets, errors, bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&txq->sync);
			packets = txq->packets;
			errors = txq->errors;
			bytes = txq->bytes;
		} while (u64_stats_fetch_retry(&txq->sync, start));

		total->tx_packets += packets;
		total->tx_errors += errors;
		total->tx_bytes += bytes;
	}

	rcu_read_unlock();

#ifndef CONFIG_HAS_VOID_NDO_GET_STATS64
	return total;
#endif
}


static int nfb_net_mdio_read(struct net_device *netdev, int prtad, int devad, u16 addr)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (priv->nc_mdio == NULL)
		return -ENODEV;

	return nc_mdio_read(priv->nc_mdio, prtad, devad, addr);
}


static int nfb_net_mdio_write(struct net_device *netdev, int prtad, int devad, u16 addr, u16 val)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (priv->nc_mdio == NULL)
		return -ENODEV;

	return nc_mdio_write(priv->nc_mdio, prtad, devad, addr, val);
}


static int nfb_net_rx_thread(void *rxqptr)
{
	struct nfb_net_queue *rxq = rxqptr;
	struct nfb_net_device *priv = rxq->priv;
	struct net_device *netdev = priv->netdev;

	struct ndp_queue *queue = &rxq->ndpq;
	struct ndp_packet packet;

	struct sk_buff *skb;
	unsigned received;

	while (!kthread_should_stop()) {

		// Get a burst of NDP packets
		received = ndp_rx_burst_get(queue, &packet, 1);

		// If no data, sleep and try again
		if (received == 0) {
			usleep_range(995, 1005);
			continue;
		}

		skb = __netdev_alloc_skb(netdev, packet.data_length + NET_IP_ALIGN, GFP_KERNEL);
		if (!skb) {
			u64_stats_update_begin(&rxq->sync);
			rxq->errors++;
			u64_stats_update_end(&rxq->sync);
			ndp_rx_burst_put(queue);
			continue;
		}

		skb_reserve(skb, NET_IP_ALIGN);
		memcpy(skb->data, packet.data, packet.data_length);

		skb_put(skb, packet.data_length);
		skb->protocol = eth_type_trans(skb, netdev);

		skb_record_rx_queue(skb, rxq->index);

		// Send packet to the kernel network stack
#ifdef CONFIG_HAVE_NETIF_RX_NI
		if (netif_rx_ni(skb) != NET_RX_DROP) {
#else
		if (netif_rx(skb) != NET_RX_DROP) {
#endif
			u64_stats_update_begin(&rxq->sync);
			rxq->packets++;
			rxq->bytes += packet.data_length;
			u64_stats_update_end(&rxq->sync);
		} else {
			u64_stats_update_begin(&rxq->sync);
			rxq->dropped++;
			u64_stats_update_end(&rxq->sync);
		}

		// Packet is processed, unlock ring buffer
		ndp_rx_burst_put(queue);
	}

	return 0;
}


static void nfb_net_transmission_off(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	unsigned i;

	// Stop all RX queues
	for (i = 0; i < priv->rxqs_count; i++) {
		if (priv->rxqs[i].task != NULL) {
			kthread_stop(priv->rxqs[i].task);
			priv->rxqs[i].task = NULL;
		}

		if (priv->rxqs[i].ndps != NULL) {
			struct ndp_queue *queue = &priv->rxqs[i].ndpq;
			ndp_close_queue(queue);
			ndp_subscriber_destroy(priv->rxqs[i].ndps);
			priv->rxqs[i].ndps = NULL;
		}
	}

	// Stop all TX queues
	netif_tx_stop_all_queues(netdev);
	for (i = 0; i < priv->txqs_count; i++) {
		if (priv->txqs[i].ndps != NULL) {
			struct ndp_queue *queue = &priv->txqs[i].ndpq;
			ndp_close_queue(queue);

			ndp_subscriber_destroy(priv->txqs[i].ndps);
			priv->txqs[i].ndps = NULL;
		}
	}
}


static int nfb_net_transmission_on(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct ndp *ndp = priv->nfbdev->list_drivers[NFB_DRIVER_NDP].priv;

	struct ndp_channel *channel;

	unsigned rxqs_index = 0;
	unsigned txqs_index = 0;

	int ret = 0;
	unsigned i;

	// Iterate over channels, create subscriptions, and start it
	list_for_each_entry(channel, &ndp->list_channels, list_ndp) {
		bool is_rx = channel->id.type == NDP_CHANNEL_TYPE_RX;
		bool is_tx = channel->id.type == NDP_CHANNEL_TYPE_TX;

		unsigned count = is_rx ? priv->rxqs_count : priv->txqs_count;
		unsigned offset = is_rx ? priv->rxqs_offset : priv->txqs_offset;
		unsigned mcount = is_rx ? priv->module->rxqc : priv->module->txqc;
		unsigned chid = (channel->id.index + mcount - offset) % mcount;

		struct nfb_net_queue *netq;

		struct ndp_subscriber *subscriber;
		struct ndp_queue *queue;

		uint64_t channel_flags;

		if (!is_rx && !is_tx)
			continue;

		if (chid >= count)
			continue;

		// TODO: add check if the queue is available

		// Create NDP subscriber
		subscriber = ndp_subscriber_create(ndp);
		if (!subscriber) {
			printk(KERN_ERR "%s: %s - failed to create subscriber for queue %s\n",
				__func__, netdev->name, dev_name(&channel->dev));
			ret = -ENOMEM;
			goto err_subscriber_create;
		}

		netq = is_rx ? &priv->rxqs[rxqs_index] : &priv->txqs[txqs_index];
		#ifdef CONFIG_NUMA
		netq->numa = dev_to_node(channel->ring.dev);
		#endif
		netq->ndps = subscriber;

		queue = &netq->ndpq;
		queue->subscriber = subscriber;
		ret = ndp_queue_open_init(ndp->nfb, queue, channel->id.index, channel->id.type);
		if (ret) {
			printk(KERN_ERR "%s: %s - failed to init queue %s (error: %d)\n",
				__func__, netdev->name, dev_name(&channel->dev), ret);
			goto err_queue_init;
		}

		ret = ndp_queue_start(queue);
		if (ret) {
			printk(KERN_ERR "%s: %s - failed to start queue %s (error: %d)\n",
				__func__, netdev->name, dev_name(&channel->dev), ret);
			goto err_queue_start;
		}

		channel_flags = channel->ops->get_flags(channel);

		if (is_rx && test_bit(NFBNET_DISCARD, &priv->flags))
			channel_flags |= NDP_CHANNEL_FLAG_DISCARD;
		else
			channel_flags &= ~NDP_CHANNEL_FLAG_DISCARD;
		channel->ops->set_flags(channel, channel_flags);

		if (is_rx)
			rxqs_index++;
		else
			txqs_index++;
	}

	if (priv->rxqs_count != rxqs_index || priv->txqs_count != txqs_index) {
		printk(KERN_ERR "%s: %s - failed to subscribe requested number of RX or TX queues\n", __func__, netdev->name);
		ret = -EINVAL;
		goto err_queues_count;
	}

	netif_set_real_num_rx_queues(netdev, max_t(unsigned, priv->rxqs_count, 1));
	netif_set_real_num_tx_queues(netdev, max_t(unsigned, priv->txqs_count, 1));

	netif_tx_start_all_queues(netdev);
	for (i = 0; i < priv->rxqs_count; i++) {
		unsigned channel = priv->rxqs[i].ndpq.channel.index;

		// Create RX thread for each queue
		priv->rxqs[i].task = kthread_create_on_node(nfb_net_rx_thread, &priv->rxqs[i], priv->rxqs[i].numa, "%s/%u", netdev->name, channel);
		if (IS_ERR(priv->rxqs[i].task)) {
			printk(KERN_ERR "%s: %s - failed to create rx thread (error: %ld, channel: %d)\n",
				__func__, netdev->name, PTR_ERR(priv->rxqs[i].task), channel);
			ret = PTR_ERR(priv->rxqs[i].task);
			goto err_kthread_create;
		}

		// Wake up thread
		wake_up_process(priv->rxqs[i].task);
	}

	return ret;

err_queue_start:
err_queue_init:
err_subscriber_create:
err_queues_count:
err_kthread_create:
	nfb_net_transmission_off(netdev);
	return ret;
}


netdev_tx_t nfb_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct nfb_net_queue *txq = &priv->txqs[skb->queue_mapping];

	struct ndp_packet packet;
	unsigned cnt;
	int ret;

	if (priv->txqs_count == 0)
		goto free;

	ret = skb_linearize(skb);
	if (ret) {
		if (net_ratelimit())
			printk(KERN_ERR "%s: %s - can't linearize sk_buff (error: %d)\n", __func__, netdev->name, ret);

		u64_stats_update_begin(&txq->sync);
		txq->errors++;
		u64_stats_update_end(&txq->sync);
		goto free;
	}

	// No specific packet metadata, TODO add output interface
	packet.header_length = 0;

	// Packet must have certain minimal size to be transmited by card, if it's smaller it'll be padded with zeros
	packet.data_length = max_t(unsigned int, skb->len, ETH_ZLEN);

	// Allocate free space for packet in ring buffer
	cnt = ndp_tx_burst_get(&txq->ndpq, &packet, 1);
	if (cnt != 1) {
		u64_stats_update_begin(&txq->sync);
		txq->errors++;
		u64_stats_update_end(&txq->sync);
		goto free;
	}

	// Copy packet with optional zeroes padding
	if (skb->len < ETH_ZLEN) memset(packet.data, 0, packet.data_length);
	memcpy(packet.data, skb->data, skb->len);

	// Flush to send immediately
	ndp_tx_burst_flush(&txq->ndpq);

	// Update stats
	u64_stats_update_begin(&txq->sync);
	txq->packets++;
	txq->bytes += packet.data_length;
	u64_stats_update_end(&txq->sync);

free:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}


static int nfb_net_open(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	int ret = 0;

	// Prohibit opening the device if not allowed
	if (test_bit(NFBNET_KEEPIFDOWN, &priv->flags))
		return -EPERM;

	// Enable transmission
	ret = nfb_net_transmission_on(netdev);
	if (ret) return ret;

	nfb_net_mac_on(netdev);
	nfb_net_transceiver_on(netdev);

	mod_timer(&priv->service_timer, jiffies);

	return ret;
}


static int nfb_net_close(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	netif_carrier_off(netdev);

	nfb_net_mac_off(netdev);
	nfb_net_transceiver_off(netdev);

	// Disable transmission
	nfb_net_transmission_off(netdev);

	del_timer_sync(&priv->service_timer);

	return 0;
}


static const struct net_device_ops netdev_ops = {
	.ndo_open = nfb_net_open,
	.ndo_stop = nfb_net_close,
	.ndo_start_xmit = nfb_start_xmit,
	.ndo_set_rx_mode = nfb_set_rx_mode,
	.ndo_set_mac_address = nfb_net_set_mac_address,
	.ndo_change_mtu	= nfb_net_change_mtu,
	.ndo_get_stats64 = nfb_net_get_stats,
};


int nfb_net_queues_init(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	int ret = 0;
	unsigned i;

	// Set to max available channels if default value is larger
	priv->rxqs_count = min(net_rxqs_count, priv->module->rxqc);
	priv->txqs_count = min(net_txqs_count, priv->module->txqc);;

	// Convert the default negative offset to positive using the number of available channels
	priv->rxqs_offset = ((net_rxqs_offset % priv->module->rxqc) + priv->module->rxqc) % priv->module->rxqc;
	priv->txqs_offset = ((net_txqs_offset % priv->module->txqc) + priv->module->txqc) % priv->module->txqc;

	// Allocate RX queue structures
	priv->rxqs = kzalloc(sizeof(*priv->rxqs) * priv->module->rxqc, GFP_KERNEL);
	if (!priv->rxqs) {
		ret = -ENOMEM;
		goto err_alloc_rxqs;
	}

	// Allocate TX queue structures
	priv->txqs = kzalloc(sizeof(*priv->txqs) * priv->module->txqc, GFP_KERNEL);
	if (!priv->txqs) {
		ret = -ENOMEM;
		goto err_alloc_txqs;
	}

	// Initialize RX & TX queue structures
	for (i = 0; i < priv->module->rxqc; i++) {
		priv->rxqs[i].priv = priv;
		priv->rxqs[i].index = i;
	}

	for (i = 0; i < priv->module->txqc; i++) {
		priv->txqs[i].priv = priv;
		priv->txqs[i].index = i;
	}

	return 0;

err_alloc_txqs:
	kfree(priv->rxqs);
err_alloc_rxqs:
	return ret;
}


void nfb_net_queues_deinit(struct net_device *netdev)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	// Free RX & TX queue structures
	kfree(priv->rxqs);
	kfree(priv->txqs);
}


struct nfb_net_device *nfb_net_device_create(struct nfb_net *module, int fdt_offset, int index)
{
	struct nfb_device *nfbdev = module->nfbdev;

	struct nfb_net_device *device;
	struct net_device *netdev;

	const fdt32_t *fdt_prop;
	int fdt_comp;
	int fdt_node;
	int fdt_plen;

	int ret;

	netdev = alloc_etherdev_mqs(sizeof(*device), max_t(unsigned, module->txqc, 1), max_t(unsigned, module->rxqc, 1));
	if (!netdev) {
		ret = -ENOMEM;
		goto err_alloc_etherdev;
	}

	// Set the name of the interface
	snprintf(netdev->name, IFNAMSIZ-1, "nfb%up%u", nfbdev->minor, index);

	// Initialize private device structure
	device = netdev_priv(netdev);
	device->index = index;
	device->flags = 0;
	device->state = 0;
	device->module = module;
	device->nfbdev = nfbdev;
	device->netdev = netdev;
	device->nc_rxmac = NULL;
	device->nc_txmac = NULL;
	device->nc_trstat = NULL;
	device->nc_tri2c = NULL;
	device->nc_mdio = NULL;
	memset(&device->dev, 0, sizeof(struct device));

	// Set default flags
	if (net_nocarrier)
		set_bit(NFBNET_NOCARRIER, &device->flags);
	if (net_keepifdown)
		set_bit(NFBNET_KEEPIFDOWN, &device->flags);

	// Initialize transmission queues
	ret = nfb_net_queues_init(netdev);
	if (ret)
		goto err_queues_init;

	// Initialize sysfs path
	ret = nfb_net_sysfs_init(device);
	if (ret)
		goto err_sysfs_init;

	fdt_comp = nc_eth_get_rxmac_node(nfbdev->fdt, fdt_offset);
	device->nc_rxmac = nc_rxmac_open(nfbdev, fdt_comp);
	if (IS_ERR(device->nc_rxmac))
		device->nc_rxmac = NULL;

	fdt_comp = nc_eth_get_txmac_node(nfbdev->fdt, fdt_offset);
	device->nc_txmac = nc_txmac_open(nfbdev, fdt_comp);
	if (IS_ERR(device->nc_txmac))
		device->nc_txmac = NULL;

	fdt_node = fdt_node_offset_by_phandle_ref(nfbdev->fdt, fdt_offset, "pmd");
	fdt_comp = fdt_node_offset_by_phandle_ref(nfbdev->fdt, fdt_node, "status-reg");
	device->nc_trstat = nfb_comp_open(nfbdev, fdt_comp);

	fdt_node = fdt_node_offset_by_phandle_ref(nfbdev->fdt, fdt_offset, "pmd");
	fdt_comp = fdt_node_offset_by_phandle_ref(nfbdev->fdt, fdt_node, "control");
	device->nc_tri2c = nc_i2c_open(nfbdev, fdt_comp);

	device->trlanes = 0;
	fdt_node = fdt_subnode_offset(nfbdev->fdt, fdt_offset, "pmd-params");
	fdt_prop = fdt_getprop(nfbdev->fdt, fdt_node, "lines", &fdt_plen);
	while (fdt_prop && fdt_plen > 0) {
		device->trlanes |= 1 << fdt32_to_cpu(*fdt_prop);
		fdt_plen -= sizeof(*fdt_prop);
		fdt_prop++;
	}

	device->mdio.mmds = 0;
	device->mdio.prtad = 0;
	device->mdio.dev = netdev;
	device->mdio.mdio_read = nfb_net_mdio_read;
	device->mdio.mdio_write = nfb_net_mdio_write;
	device->mdio.mode_support = MDIO_SUPPORTS_C45;
	fdt_node = fdt_node_offset_by_phandle_ref(nfbdev->fdt, fdt_offset, "pcspma");
	fdt_comp = fdt_node_offset_by_phandle_ref(nfbdev->fdt, fdt_node, "control");
	device->nc_mdio = nc_mdio_open(nfbdev, fdt_comp);
	if (device->nc_mdio) {
		fdt_node = fdt_subnode_offset(nfbdev->fdt, fdt_node, "control-param");
		fdt_prop = fdt_getprop(nfbdev->fdt, fdt_node, "dev", &fdt_plen);
		if (fdt_plen == sizeof(*fdt_prop)) {
			device->mdio.prtad = fdt32_to_cpu(*fdt_prop);
		}
	}

	netdev->netdev_ops = &netdev_ops;
	nfb_net_set_ethtool_ops(netdev);
	SET_NETDEV_DEV(netdev, &nfbdev->pci->dev);

	nfb_net_set_dev_addr(nfbdev, netdev, index);
	nfb_net_commit_mac_address(netdev);

	nfb_net_change_mtu(netdev, netdev->mtu);

	ret = register_netdev(netdev);
	if (ret) {
		goto err_register_netdev;
	}

	nfb_set_rx_mode(netdev);
	netif_carrier_off(netdev);
	nfb_net_transceiver_off(netdev);

#ifdef CONFIG_HAS_TIMER_SETUP
	timer_setup(&device->service_timer, &nfb_net_service_timer, 0);
#else
	setup_timer(&device->service_timer, &nfb_net_service_timer, (unsigned long) device);
#endif

	INIT_WORK(&device->service_task, nfb_net_service_task);
	clear_bit(NFBNET_SERVICE_SCHED, &device->state);

	return device;

err_register_netdev:
	nfb_net_sysfs_deinit(device);
err_sysfs_init:
	nfb_net_queues_deinit(netdev);
err_queues_init:
	free_netdev(netdev);
err_alloc_etherdev:
	return NULL;
}


void nfb_net_device_destroy(struct nfb_net_device *device)
{
	cancel_work_sync(&device->service_task);
	unregister_netdev(device->netdev);

	if (device->nc_txmac)
		nc_txmac_close(device->nc_txmac);

	if (device->nc_rxmac)
		nc_rxmac_close(device->nc_rxmac);

	if (device->nc_trstat)
		nfb_comp_close(device->nc_trstat);

	if (device->nc_tri2c)
		nc_i2c_close(device->nc_tri2c);

	if (device->nc_mdio)
		nc_mdio_close(device->nc_mdio);

	nfb_net_sysfs_deinit(device);
	nfb_net_queues_deinit(device->netdev);

	free_netdev(device->netdev);
}
