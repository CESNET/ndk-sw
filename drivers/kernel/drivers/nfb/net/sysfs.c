/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network interface driver of the NFB platform - sysfs support
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#include <libfdt.h>

#include "../nfb.h"

#include "net.h"

#include <linux/pci.h>
#include <linux/netdevice.h>


static ssize_t nfb_net_get_discard(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", test_bit(NFBNET_DISCARD, &priv->flags));
}

static ssize_t nfb_net_set_discard(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	struct net_device *netdev = priv->netdev;

	char *end;
	long val = simple_strtoul(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	if (val) {
		set_bit(NFBNET_DISCARD, &priv->flags);
	} else {
		clear_bit(NFBNET_DISCARD, &priv->flags);
	}

	return size;
}


static ssize_t nfb_net_get_nocarrier(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", test_bit(NFBNET_NOCARRIER, &priv->flags));
}

static ssize_t nfb_net_set_nocarrier(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);

	char *end;
	long val = simple_strtoul(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (val) {
		set_bit(NFBNET_NOCARRIER, &priv->flags);
	} else {
		clear_bit(NFBNET_NOCARRIER, &priv->flags);
	}

	return size;
}


static ssize_t nfb_net_get_keepifdown(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", test_bit(NFBNET_KEEPIFDOWN, &priv->flags));
}

static ssize_t nfb_net_set_keepifdown(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);

	char *end;
	long val = simple_strtoul(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (val) {
		set_bit(NFBNET_KEEPIFDOWN, &priv->flags);
	} else {
		clear_bit(NFBNET_KEEPIFDOWN, &priv->flags);
	}

	return size;
}


static ssize_t nfb_net_get_rxqs_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", priv->rxqs_count);
}

static ssize_t nfb_net_set_rxqs_count(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	struct net_device *netdev = priv->netdev;

	char *end;
	unsigned val = simple_strtoul(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (val > priv->module->rxqc)
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	priv->rxqs_count = val;

	return size;
}


static ssize_t nfb_net_get_rxqs_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", priv->rxqs_offset);
}

static ssize_t nfb_net_set_rxqs_offset(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	struct net_device *netdev = priv->netdev;

	char *end;
	int val = simple_strtol(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	priv->rxqs_offset = ((val % priv->module->rxqc) + priv->module->rxqc) % priv->module->rxqc;

	return size;
}


static ssize_t nfb_net_get_txqs_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", priv->txqs_count);
}

static ssize_t nfb_net_set_txqs_count(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	struct net_device *netdev = priv->netdev;

	char *end;
	int val = simple_strtol(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	priv->txqs_offset = ((val % priv->module->txqc) + priv->module->txqc) % priv->module->txqc;

	return size;
}


static ssize_t nfb_net_get_txqs_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", priv->txqs_offset);
}

static ssize_t nfb_net_set_txqs_offset(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct nfb_net_device *priv = dev_get_drvdata(dev);
	struct net_device *netdev = priv->netdev;

	char *end;
	unsigned val = simple_strtoul(buf, &end, 0);

	if (end == buf)
		return -EINVAL;

	if (val >= priv->module->txqc)
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	priv->txqs_offset = val;

	return size;
}


// Attributes for sysfs -- declarations
static DEVICE_ATTR(discard, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_discard, nfb_net_set_discard);
static DEVICE_ATTR(nocarrier, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_nocarrier, nfb_net_set_nocarrier);
static DEVICE_ATTR(keepifdown, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_keepifdown, nfb_net_set_keepifdown);
static DEVICE_ATTR(rx_queues_count, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_rxqs_count, nfb_net_set_rxqs_count);
static DEVICE_ATTR(rx_queues_offset, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_rxqs_offset, nfb_net_set_rxqs_offset);
static DEVICE_ATTR(tx_queues_count, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_txqs_count, nfb_net_set_txqs_count);
static DEVICE_ATTR(tx_queues_offset, (S_IRUGO | S_IWGRP | S_IWUSR), nfb_net_get_txqs_offset, nfb_net_set_txqs_offset);


struct attribute *nfb_net_device_attrs[] = {
	&dev_attr_discard.attr,
	&dev_attr_nocarrier.attr,
	&dev_attr_keepifdown.attr,
	&dev_attr_rx_queues_count.attr,
	&dev_attr_rx_queues_offset.attr,
	&dev_attr_tx_queues_count.attr,
	&dev_attr_tx_queues_offset.attr,
	NULL,
};

struct attribute_group nfb_net_device_attr_group = {
 	.attrs = nfb_net_device_attrs,
};

const struct attribute_group *nfb_net_device_attr_groups[] = {
 	&nfb_net_device_attr_group,
 	NULL,
};


int nfb_net_sysfs_init(struct nfb_net_device *device)
{
	device_initialize(&device->dev);
	device->dev.parent = &device->module->dev;
	device->dev.groups = nfb_net_device_attr_groups;
	dev_set_name(&device->dev, "nfb%up%u", device->nfbdev->minor, device->index);
	dev_set_drvdata(&device->dev, device);

	return device_add(&device->dev);
}


void nfb_net_sysfs_deinit(struct nfb_net_device *device)
{
	device_del(&device->dev);
}
