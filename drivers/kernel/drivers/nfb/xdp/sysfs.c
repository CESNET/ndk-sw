/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - sysfs
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include "sysfs.h"
#include "driver.h"
#include "channel.h"

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/parser.h>

// ---------------- SYSFS file for dynamic adding and removing of XDP netdevs ------------

static ssize_t cmd_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
static ssize_t cmd_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	enum {
		Opt_cmd,
		Opt_id,
		Opt_qidxs,
		Opt_err,
	};
    const match_table_t tokens = {
		{Opt_cmd, "cmd=%s"},
		{Opt_id, "id=%d"},
		{Opt_qidxs, "qidxs=%s"},
		{Opt_err, NULL},
	};
	struct nfb_xdp *module = dev_get_drvdata(dev);

	char *options_iter, *options;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int ret;
	int i = 0;

	int index = -1;
	char *cmd = NULL;
	char *queue_string, *queue_string_iter = NULL;
	char *q;
	unsigned queue_index_iter;
	unsigned queue_count = 0;
	unsigned *queue_indexes = NULL;
	

	if (buf[size - 1] != 0)
		return -EINVAL;

	options_iter = options = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	while ((p = strsep(&options_iter, ",")) != NULL) {
		int token;

		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_id:
			ret = match_int(&args[0], &index);
			if (ret)
				goto err_parser;
			break;
		case Opt_cmd:
			cmd = match_strdup(&args[0]);
			break;
		case Opt_qidxs:
			queue_string = queue_string_iter = match_strdup(&args[0]);
			if (!queue_string) {
				ret = -ENOMEM;
				goto err_parser;
			}
			break;
		default:
			continue;
		}
	}

	if (cmd == NULL || index == -1) {
		ret = -EINVAL;
		goto err_parser;
	}
	
	if (strcmp(cmd, "add") == 0 && index != -1) {
		if (queue_string) {
			for (i = 0; (q = strsep(&queue_string_iter , ":")) != NULL; i++) {
				// If the ':' is ending, break
				if (*q == '\0')
					break;

				// Validate queue string
				if(sscanf(q, "%u", &queue_index_iter) == 1) {
					if (!(queue_indexes = krealloc(queue_indexes, sizeof(*queue_indexes) * ++queue_count, GFP_KERNEL))) {
						ret = -ENOMEM;
						goto err_parser;
					}
					queue_indexes[i] = queue_index_iter;
				} else {
					ret = -EINVAL;
					goto err_parser;
				}
			}
		} else { // If no queues were provided, use all queues
			queue_count = module->channelc;
			if (!(queue_indexes = kmalloc(sizeof(*queue_indexes) * queue_count, GFP_KERNEL))) {
				ret = -ENOMEM;
				goto err_parser;
			}
			for (i = 0; i < queue_count; i++) {
				queue_indexes[i] = i;
			}
		}
		ret = create_ethdev(module, index, queue_indexes, queue_count);
	} else if (strcmp(cmd, "del") == 0) {
		ret = destroy_ethdev(module, index);
	} else {
		ret = -ENXIO;
	}

err_parser:
	kfree(queue_indexes);
	kfree(cmd);
	kfree(queue_string);
	kfree(options);
	return ret ? ret : size;
}
static DEVICE_ATTR_RW(cmd);

// -------------------- SYSFS files for the MODULE - top level information ---------------

static ssize_t channel_total_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_xdp *module = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", module->channelc);
}
static DEVICE_ATTR_RO(channel_total);

static ssize_t eth_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_xdp *module = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", module->ethc);
}
static DEVICE_ATTR_RO(eth_count);

struct attribute *nfb_module_attrs[] = {
	&dev_attr_cmd.attr,
	&dev_attr_channel_total.attr,
	&dev_attr_eth_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(nfb_module);

void nfb_xdp_sysfs_init_module_attributes(struct nfb_xdp *module)
{
	struct device *dev = &module->dev;
	dev->groups = nfb_module_groups;
}

void nfb_xdp_sysfs_deinit_module(struct nfb_ethdev *ethdev)
{
	return;
}

// --------------------------- SYSFS files for each channel -------------------------------- 
struct channel_sysfs_drvdata {
	struct nfb_xdp *module;
	unsigned channel_index;
};

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_sysfs_drvdata *drvdata = dev_get_drvdata(dev);
	struct nfb_xdp *module = drvdata->module;
	unsigned channel_index = drvdata->channel_index;
	struct nfb_ethdev *ethdev, *tmp;

	bool open = false;
	int i;
	int ret;

	mutex_lock(&module->list_mutex);
	{
		list_for_each_entry_safe(ethdev, tmp, &module->list_devices, list) {
			for(i = 0; i < ethdev->channel_count; i++) {
				if (ethdev->channels[i].nfb_index == channel_index) {
					open = true;
					break;
				}
			}
		}
		ret = sysfs_emit(buf, "%d\n", open);
	}
	mutex_unlock(&module->list_mutex);
	return ret;
}
static DEVICE_ATTR_RO(status);

static ssize_t ifname_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_sysfs_drvdata *drvdata = dev_get_drvdata(dev);
	struct nfb_xdp *module = drvdata->module;
	unsigned channel_index = drvdata->channel_index;
	struct nfb_ethdev *ethdev, *tmp;

	bool open = false;
	int i;
	int ret;

	mutex_lock(&module->list_mutex);
	{
		list_for_each_entry_safe(ethdev, tmp, &module->list_devices, list) {
			for(i = 0; i < ethdev->channel_count; i++) {
				if (ethdev->channels[i].nfb_index == channel_index) {
					open = true;
					ret = sysfs_emit(buf, "%s\n", netdev_name(ethdev->netdev));
					break;
				}
			}
		}

		if (!open) {
			ret = sysfs_emit(buf, "%s\n", "NOT_OPEN");
		}
	}
	mutex_unlock(&module->list_mutex);
	return ret;
}
static DEVICE_ATTR_RO(ifname);

static ssize_t index_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_sysfs_drvdata *drvdata = dev_get_drvdata(dev);
	struct nfb_xdp *module = drvdata->module;
	unsigned channel_index = drvdata->channel_index;
	struct nfb_ethdev *ethdev, *tmp;

	bool open = false;
	int i;
	int ret;

	mutex_lock(&module->list_mutex);
	{
		list_for_each_entry_safe(ethdev, tmp, &module->list_devices, list) {
			for(i = 0; i < ethdev->channel_count; i++) {
				if (ethdev->channels[i].nfb_index == channel_index) {
					open = true;
					ret = sysfs_emit(buf, "%d\n", ethdev->channels[i].index);
					break;
				}
			}
		}
		if (!open) {
			ret = sysfs_emit(buf, "%d\n", -1);
		}
	}
	mutex_unlock(&module->list_mutex);
	return ret;
}
static DEVICE_ATTR_RO(index);

struct attribute *nfb_channel_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_ifname.attr,
	&dev_attr_index.attr,
	NULL,
};
ATTRIBUTE_GROUPS(nfb_channel);

int nfb_xdp_sysfs_init_channels(struct nfb_xdp *module)
{
	struct device *dev;
	struct channel_sysfs_drvdata *drvdata;

	int ret;
	int ch_idx;

	if (!(module->channel_sysfsdevs = kzalloc(sizeof(*module->channel_sysfsdevs) * module->channelc, GFP_KERNEL))) {
		return -ENOMEM;
	}

	for (ch_idx = 0; ch_idx < module->channelc; ch_idx++) {
		dev = &module->channel_sysfsdevs[ch_idx];
		if (!(drvdata = kzalloc(sizeof(*module->channel_sysfsdevs) * module->channelc, GFP_KERNEL))) {
			ret = -ENOMEM;
			goto err;
		}
		drvdata->module = module;
		drvdata->channel_index = ch_idx;

		device_initialize(dev); 
		dev->parent = &module->dev;
		dev->groups = nfb_channel_groups;
		dev_set_name(dev, "channel%d", ch_idx);
		dev_set_drvdata(dev, drvdata);
		ret = device_add(dev);
		if (ret) {
			put_device(dev);
			goto err;
		}
	}
	
	return ret;

err:
	for (; ch_idx >= 0; --ch_idx) {
		drvdata = dev_get_drvdata(&module->channel_sysfsdevs[ch_idx]);
		put_device(&module->channel_sysfsdevs[ch_idx]);
		kfree(drvdata);
	}
	kfree(module->channel_sysfsdevs);
	return ret;
}

void nfb_xdp_sysfs_deinit_channels(struct nfb_xdp *module) {
	struct channel_sysfs_drvdata *drvdata;
	int ch_idx;

	for (ch_idx = 0; ch_idx < module->channelc; ch_idx++) {
		drvdata = dev_get_drvdata(&module->channel_sysfsdevs[ch_idx]);
		device_del(&module->channel_sysfsdevs[ch_idx]);
		kfree(drvdata);
	}
	kfree(module->channel_sysfsdevs);
}