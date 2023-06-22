/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP backend network interface driver of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Dominik Tran <xtrand00@stud.fit.vutbr.cz>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>

#include <libfdt.h>
#include "../nfb.h"
#include "core.h"

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/skbuff.h>
#include <linux/sched/task.h>


static bool ndp_netdev_enable = 0;
static bool ndp_netdev_carrier = 0;


/**
 * nfb_ndp_netdev_rx_thread - thread function for receiving data, works in a poll mode
 * @data: pointer to net_device structure
 */
static int nfb_ndp_netdev_rx_thread(void *data)
{
	unsigned cnt;
	struct ndp_packet packet;
	struct ndp_queue *q;

	struct net_device *dev = data;
	struct nfb_ndp_netdev *ethdev;

	struct sk_buff *skb;

	ethdev = netdev_priv(dev);
	q = &ethdev->rx_q;

	while (!kthread_should_stop()) {
		/* read new data */
		cnt = ndp_rx_burst_get(q, &packet, 1);

		/* no new data, sleep and then try again */
		if (cnt == 0) {
			usleep_range(995, 1005);
			continue;
		}

		skb = __netdev_alloc_skb(dev, packet.data_length + NET_IP_ALIGN, GFP_KERNEL);
		if (!skb) {
			ethdev->ndev_stats.rx_errors++;
			ndp_rx_burst_put(q);
			continue;
		}

		skb_reserve(skb, NET_IP_ALIGN);
		memcpy(skb->data, packet.data, packet.data_length);

		skb_put(skb, packet.data_length);
		skb->protocol = eth_type_trans(skb, dev);

		/* send packet to the kernel network stack */
#ifdef CONFIG_HAVE_NETIF_RX_NI
		if (netif_rx_ni(skb) != NET_RX_DROP) {
#else
		if (netif_rx(skb) != NET_RX_DROP) {
#endif
			ethdev->ndev_stats.rx_packets++;
			ethdev->ndev_stats.rx_bytes += packet.data_length;
		} else {
			ethdev->ndev_stats.rx_dropped++;
		}

		/* packet is processed, unlock ring buffer */
		ndp_rx_burst_put(q);
	}

	return 0;
}

/**
 * nfb_ndp_netdev_sub_dma - subscribes to DMA channel and prepares ndp_queue sructure
 * @type: type of DMA channel - for reading (RX) or writing (TX)
 */
static int nfb_ndp_netdev_sub_dma(struct net_device *ndev, int type)
{
	int ret;

	struct ndp_queue *q;
#if 0
	struct ndp_channel * channel;
#endif

	struct nfb_ndp_netdev *ethdev;
	ethdev = netdev_priv(ndev);

	q = (type == NDP_CHANNEL_TYPE_TX ? &ethdev->tx_q : &ethdev->rx_q);

	q->subscriber = (struct ndp_subscriber *) ethdev->suber;
	ret = ndp_queue_open_init(ethdev->suber->ndp->nfb, q, ethdev->index, type);
	if (ret) {
		printk(KERN_ERR "%s: failed to init queue\n", __func__);
		goto err_queue_init;
	}

	/* start subscription */
	ret = ndp_queue_start(q);
	if (ret) {
		printk(KERN_ERR "%s: failed to start queue\n", __func__);
		goto err_sub_present;
	}

	/* enable discarding */
#if 0
	channel = q->sub->channel;
	channel->ops->set_flags(channel, channel->ops->get_flags(channel) | NDP_CHANNEL_FLAG_DISCARD);
#endif
	return 0;

err_sub_present:
	ndp_close_queue(q);
err_queue_init:
	memset(q, 0, sizeof(*q));
	return ret;
}

/**
 * nfb_ndp_netdev_unsub_dma - unsubscribes from DMA channel and clears ndp_queue sructure
 * @type: type of DMA channel - for reading (RX) or writing (TX)
 */
static void nfb_ndp_netdev_unsub_dma(struct nfb_ndp_netdev *ethdev, int type)
{
	struct ndp_queue *q;

	q = (type == NDP_CHANNEL_TYPE_TX ? &ethdev->tx_q : &ethdev->rx_q);

	if (q->subscriber != NULL) {
		ndp_close_queue(q);
		memset(q, 0, sizeof(*q));
	}
}

/**
 * nfb_ndp_netdev_open - subscribes to DMA channels and creates thread for reading
 *
 * This function is called when network interface is opened
 */
static int nfb_ndp_netdev_open(struct net_device *ndev)
{
	int ret = 0;
	struct ndp *ndp;

	struct nfb_ndp_netdev *ethdev;
	ethdev = netdev_priv(ndev);

	/* create new subscriber */
	ndp = (struct ndp *) ethdev->nfb->list_drivers[NFB_DRIVER_NDP].priv;
	ethdev->suber = ndp_subscriber_create(ndp);

	if (!ethdev->suber) {
		printk(KERN_ERR "%s: %s - failed to create subscriber\n", __func__, ndev->name);
		ret = -ENOMEM;
		goto err_no_suber;
	}

	/* subscribe to channels and prepare ndp_queue structures */
	ret = nfb_ndp_netdev_sub_dma(ndev, NDP_CHANNEL_TYPE_TX);
	if (ret) {
		goto err_no_tx;
	}

	ret = nfb_ndp_netdev_sub_dma(ndev, NDP_CHANNEL_TYPE_RX);
	if (ret) {
		goto err_no_rx;
	}

	/* create kernel thread for reading */
	ethdev->rx_task = kthread_create(nfb_ndp_netdev_rx_thread, ndev, "nfb_rx/%u", ethdev->index);
	if (IS_ERR(ethdev->rx_task)) {
		printk(KERN_ERR "%s: %s - failed to create thread\n", __func__, ndev->name);
		ret = -ENOMEM;
		goto err_no_task;
	}
	wake_up_process(ethdev->rx_task);

	return 0;

	put_task_struct(ethdev->rx_task);
err_no_task:
	nfb_ndp_netdev_unsub_dma(ethdev, NDP_CHANNEL_TYPE_RX);
err_no_rx:
	nfb_ndp_netdev_unsub_dma(ethdev, NDP_CHANNEL_TYPE_TX);
err_no_tx:
	ndp_subscriber_destroy(ethdev->suber);
	ethdev->suber = NULL;
err_no_suber:
	return ret;
}

/**
 * nfb_ndp_netdev_close - removes subscriptions and closes reading thread
 *
 * This function is called when network interface is closed
 */
static int nfb_ndp_netdev_close(struct net_device *ndev)
{
	struct nfb_ndp_netdev *ethdev;
	ethdev = netdev_priv(ndev);

	kthread_stop(ethdev->rx_task);
	nfb_ndp_netdev_unsub_dma(ethdev, NDP_CHANNEL_TYPE_RX);
	nfb_ndp_netdev_unsub_dma(ethdev, NDP_CHANNEL_TYPE_TX);

	ndp_subscriber_destroy(ethdev->suber);
	ethdev->suber = NULL;

	return 0;
}

/**
 * nfb_ndp_netdev_xmit_dma - transmit packet
 * @skb: structure containing packet data
 */
netdev_tx_t nfb_ndp_netdev_xmit_dma(struct sk_buff *skb, struct net_device *dev)
{
	int ret;
	unsigned cnt;
	struct ndp_packet packet;
	struct ndp_queue *q;

	struct nfb_ndp_netdev *ethdev;
	ethdev = netdev_priv(dev);
	q = &ethdev->tx_q;

	/* no NDP specific packet metadata */
	packet.header_length = 0;

	/* packet must have certain minimal size to be transmited by card, if it's smaller it will be padded with zeros */
	packet.data_length = max_t(unsigned int, skb->len, ETH_ZLEN);

	ret = skb_linearize(skb);

	if (ret) {
		if (net_ratelimit())
			printk(KERN_ERR "%s: can't linearize sk_buff: %d\n", __func__, ret);

		ethdev->ndev_stats.tx_errors++;
		goto free;
	}

	/* find free space for packet in ring buffer */
	cnt = ndp_tx_burst_get(q, &packet, 1);
	if (cnt != 1) {
		ethdev->ndev_stats.tx_errors++;
		goto free;
	}

	/* padding unused space with zeroes */
	if (skb->len < ETH_ZLEN)
		memset(packet.data, 0, packet.data_length);

	memcpy(packet.data, skb->data, skb->len);
	/* flush function will send packet immediately */
	ndp_tx_burst_flush(q);

	ethdev->ndev_stats.tx_packets++;
	ethdev->ndev_stats.tx_bytes += packet.data_length;

free:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static struct net_device_stats *nfb_ndp_netdev_get_stats(struct net_device *dev)
{
	struct nfb_ndp_netdev *ethdev;
	ethdev = netdev_priv(dev);

	return &ethdev->ndev_stats;
}

/*
 * commenting this out, since it is unused
static int nfb_ndp_netdev_change_carrier(struct net_device *dev, bool new_carrier)
{
	if (new_carrier)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);

	return 0;
}
*/

static const struct net_device_ops ndp_netdev_ops = {
	.ndo_open = nfb_ndp_netdev_open,
	.ndo_stop = nfb_ndp_netdev_close,
	.ndo_start_xmit = nfb_ndp_netdev_xmit_dma,
	.ndo_get_stats = nfb_ndp_netdev_get_stats,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = eth_mac_addr,
	//.ndo_change_carrier = nfb_ndp_netdev_change_carrier,
};



static ssize_t nfb_ndp_netdev_get_carrier(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_ndp_netdev *ethdev = dev_get_drvdata(dev);
	struct net_device *ndev = ethdev->ndev;

	return scnprintf(buf, PAGE_SIZE, "%d\n", netif_carrier_ok(ndev) ? 1 : 0);
}

static ssize_t nfb_ndp_netdev_set_carrier(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	char *end;
	long val = simple_strtoul(buf, &end, 0);
	struct nfb_ndp_netdev *ethdev = dev_get_drvdata(dev);
	struct net_device *ndev = ethdev->ndev;

	if (end == buf)
		return -EINVAL;

	if (val)
		netif_carrier_on(ndev);
	else
		netif_carrier_off(ndev);

	return size;
}

/* Attributes for sysfs - declarations */
DEVICE_ATTR(carrier, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_ndp_netdev_get_carrier, nfb_ndp_netdev_set_carrier);

struct attribute *nfb_ndp_netdev_attrs[] = {
	&dev_attr_carrier.attr,
	NULL,
};

struct attribute_group nfb_ndp_netdev_attr_group = {
	.attrs = nfb_ndp_netdev_attrs,
};

const struct attribute_group *nfb_ndp_netdev_attr_groups[] = {
	&nfb_ndp_netdev_attr_group,
	NULL,
};

/**
 * nfb_ndp_netdev_create - creates and registers new network device in the system
 * @index: index of network interface
 */
struct nfb_ndp_netdev *nfb_ndp_netdev_create(struct nfb_mod_ndp_netdev *eth, int index)
{
	struct nfb_device *nfb = eth->nfb;
	struct nfb_ndp_netdev *ethdev;
	struct net_device *ndev;
	int ret;

	ndev = alloc_etherdev(sizeof(*ethdev));
	if (!ndev) {
		printk(KERN_ERR "%s: failed to alloc etherdev %d\n", __func__, index);
		goto err_alloc;
	}

	ethdev = netdev_priv(ndev);
	ethdev->ndev = ndev;
	ethdev->nfb = nfb;
	ethdev->eth = eth;
	ethdev->index = index;
	memset(&ethdev->device, 0, sizeof(struct device));

	device_initialize(&ethdev->device);
	ethdev->device.parent = &eth->dev;
	ethdev->device.groups = nfb_ndp_netdev_attr_groups;
	dev_set_name(&ethdev->device, "nfb%ud%u", nfb->minor, index);
	dev_set_drvdata(&ethdev->device, ethdev);
	ret = device_add(&ethdev->device);
	if (ret)
		goto err_device_add;

	/* supported operations */
	ndev->netdev_ops = &ndp_netdev_ops;
	SET_NETDEV_DEV(ndev, &nfb->pci->dev);

	snprintf(ndev->name, IFNAMSIZ-1, "nfb%ud%u", nfb->minor, index);
	nfb_net_set_dev_addr(nfb, ndev, index);

	ret = register_netdev(ndev);
	if (ret) {
		printk(KERN_ERR "%s: failed to register netdev %d\n", __func__, index);
		goto err_register_netdev;
	}

	netif_carrier_off(ndev);

	/* create device with carrier state up */
	if (ndp_netdev_carrier)
		netif_carrier_on(ndev);

	return ethdev;

	unregister_netdev(ethdev->ndev);
err_register_netdev:
	device_del(&ethdev->device);
err_device_add:
	free_netdev(ndev);
err_alloc:
	return NULL;
}

/**
 * nfb_ndp_netdev_destroy - remove network device from the system
 */
void nfb_ndp_netdev_destroy(struct nfb_ndp_netdev *ethdev)
{
	unregister_netdev(ethdev->ndev);
	device_del(&ethdev->device);
	free_netdev(ethdev->ndev);
}

/**
 * nfb_ndp_netdev_attach - initializes this submodule
 *
 * This function is called when this submodule is being attached to the main module
 */
int nfb_ndp_netdev_attach(struct nfb_device *nfb, void **priv)
{
	int ret = 0;
	int index;
	struct nfb_mod_ndp_netdev * eth;
	struct nfb_ndp_netdev *ethdev;
	int rx;
	int tx;

	int proplen;
	const fdt64_t *proprx, *proptx;

	if (!ndp_netdev_enable) {
		*priv = NULL;
		return 0;
	}

	eth = kzalloc(sizeof(*eth), GFP_KERNEL);
	if (!eth) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	INIT_LIST_HEAD(&eth->list_ethdev);
	eth->nfb = nfb;
	*priv = eth;

	device_initialize(&eth->dev);
	eth->dev.parent = eth->nfb->dev;
	dev_set_name(&eth->dev, "ndp_netdev");
	dev_set_drvdata(&eth->dev, eth);
	ret = device_add(&eth->dev);
	if (ret)
		goto err_device_add;

	/* create new network device for each RX/TX DMA pair */
	index = 0;
	rx = fdt_path_offset(nfb->fdt, "/drivers/ndp/rx_queues");
	tx = fdt_path_offset(nfb->fdt, "/drivers/ndp/tx_queues");
	rx = fdt_first_subnode(nfb->fdt, rx),
	tx = fdt_first_subnode(nfb->fdt, tx);
	while (rx >= 0 && tx >= 0) {
		/* Check if queue is available (both rx + tx)*/
		proprx = fdt_getprop(nfb->fdt, rx, "mmap_size", &proplen);
		proptx = fdt_getprop(nfb->fdt, tx, "mmap_size", &proplen);
		if (proprx && proptx && fdt64_to_cpu(*proprx) && fdt64_to_cpu(*proptx)) {
			ethdev = nfb_ndp_netdev_create(eth, index);
			if (ethdev)
				list_add_tail(&ethdev->list_item, &eth->list_ethdev);
			index++;
		}
		rx = fdt_next_subnode(nfb->fdt, rx);
		tx = fdt_next_subnode(nfb->fdt, tx);
	}

	dev_info(&nfb->pci->dev, "ndp_netdev: Attached successfully (%d NDP based ETH interfaces)\n", index);

	return ret;

	device_del(&eth->dev);
err_device_add:
	kfree(eth);
err_alloc:
	return ret;
}

/**
 * nfb_ndp_netdev_detach - deinitializes this submodule
 *
 * This function is called when this submodule is being deattached from the main module
 */
void nfb_ndp_netdev_detach(struct nfb_device *nfb, void *priv)
{
	struct nfb_mod_ndp_netdev* eth = (struct nfb_mod_ndp_netdev*) priv;
	struct nfb_ndp_netdev *ethdev, *tmp;

	if (eth == NULL)
		return;

	list_for_each_entry_safe(ethdev, tmp, &eth->list_ethdev, list_item) {
		list_del(&ethdev->list_item);
		nfb_ndp_netdev_destroy(ethdev);
	}

	device_del(&eth->dev);

	kfree(eth);
}

module_param(ndp_netdev_enable, bool, S_IRUGO);
MODULE_PARM_DESC(ndp_netdev_enable, "Create netdevices for each NDP rx-tx pair [no]");

module_param(ndp_netdev_carrier, bool, S_IRUGO);
MODULE_PARM_DESC(ndp_netdev_carrier, "Create netdevices with carrier state set to up [no]");
