/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - transmission channel module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <libfdt.h>

#include "ndp.h"
#include "../nfb.h"

extern unsigned long ndp_ring_size;
extern unsigned long ndp_ring_block_size;

ssize_t ndp_channel_get_discard(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ndp_channel *channel = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", channel->ops->get_flags(channel) & NDP_CHANNEL_FLAG_DISCARD ? 1 : 0);
}

ssize_t ndp_channel_set_discard(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	char *end;
	long val = simple_strtoul(buf, &end, 0);
	struct ndp_channel *channel = dev_get_drvdata(dev);
	uint64_t flags;

	if (end == buf)
		return -EINVAL;

	flags = channel->ops->get_flags(channel);
	flags = val ? (flags | NDP_CHANNEL_FLAG_DISCARD) : (flags & ~NDP_CHANNEL_FLAG_DISCARD);
	if (flags != channel->ops->set_flags(channel, flags))
		return -EPERM;

	return size;
}

void ndp_channel_init(struct ndp_channel *channel, struct ndp_channel_id id)
{
	channel->id = id;
	channel->flags = 0;
	channel->subscriptions_count = 0;
	channel->start_count = 0;
	channel->locked_sub = NULL;

	spin_lock_init(&channel->lock);
	mutex_init(&channel->mutex);
	INIT_LIST_HEAD(&channel->list_ndp);
	INIT_LIST_HEAD(&channel->list_subscriptions);

	device_initialize(&channel->dev);
	dev_set_name(&channel->dev, channel->id.type == NDP_CHANNEL_TYPE_TX ? "tx%d" :"rx%d", channel->id.index);
	dev_set_drvdata(&channel->dev, channel);

	ndp_channel_ring_req_block_update_by_size(channel, ndp_ring_size);
}

int ndp_channel_add(struct ndp_channel *channel, struct ndp *ndp, uint32_t phandle)
{
	int ret;
	int node_offset;

	channel->dev.parent = &ndp->dev;
	channel->ndp = ndp;

	node_offset = fdt_path_offset(ndp->nfb->fdt, channel->id.type == NDP_CHANNEL_TYPE_TX ?
				"/drivers/ndp/tx_queues" : "/drivers/ndp/rx_queues");
	node_offset = fdt_add_subnode(ndp->nfb->fdt, node_offset, dev_name(&channel->dev));
	fdt_setprop_u32(ndp->nfb->fdt, node_offset, "ctrl", phandle);

	ndp_channel_ring_create(channel, channel->ring.dev, channel->req_block_count, channel->req_block_size);

	ret = device_add(&channel->dev);
	if (ret)
		goto err_device_add;

	mutex_lock(&ndp->lock);
	list_add_tail(&channel->list_ndp, &ndp->list_channels);
	mutex_unlock(&ndp->lock);

	return ret;

err_device_add:
	/* FIXME: delete FDT node */
	return ret;
}

void ndp_channel_del(struct ndp_channel *channel)
{
	struct ndp *ndp = channel->ndp;

	mutex_lock(&ndp->lock);
	list_del_init(&channel->list_ndp);
	mutex_unlock(&ndp->lock);

	channel->ops->detach_ring(channel);

	ndp_channel_ring_destroy(channel);
	device_del(&channel->dev);
	put_device(&channel->dev);
}

int ndp_channel_subscribe(struct ndp_subscription *sub, uint32_t *flags)
{
	int ret = 0;
	struct ndp_channel *channel = sub->channel;
	uint32_t mask;
	uint64_t req_flags = *flags;

	mutex_lock(&channel->mutex);
	if (channel->subscriptions_count++ == 0) {
		/* Common flags that are handled by channel */
		mask = NDP_CHANNEL_FLAG_EXCLUSIVE;
		*flags = channel->ops->set_flags(channel, req_flags & ~mask);
		if (*flags != (req_flags & ~mask)) {
			ret = -EPERM;
		} else {
			channel->flags = req_flags & mask;
		}
	} else {
		mask = channel->ops->get_flags(channel);
		if ((*flags | channel->flags) & NDP_CHANNEL_FLAG_EXCLUSIVE)
			ret = -EPERM;
		if (*flags ^ (channel->flags | mask))
			ret = -EPERM;
	}

	if (ret)
		channel->subscriptions_count--;
	mutex_unlock(&channel->mutex);
	return ret;
}

void ndp_channel_unsubscribe(struct ndp_subscription *sub)
{
	struct ndp_channel *channel = sub->channel;

	mutex_lock(&channel->mutex);
	channel->subscriptions_count--;
	mutex_unlock(&channel->mutex);
}

int ndp_channel_start(struct ndp_subscription *sub)
{
	int ret;
	struct ndp_channel *channel = sub->channel;

	mutex_lock(&channel->mutex);

	/* already started? */
	if (channel->start_count++ == 0) {
		ret = channel->ops->start(channel, &channel->hwptr);
		if (ret)
			goto err_start;

		channel->swptr = channel->hwptr;
	}

	spin_lock(&channel->lock);
	sub->swptr = sub->hwptr = channel->hwptr;
	list_add_tail(&sub->list_item, &channel->list_subscriptions);
	spin_unlock(&channel->lock);

	mutex_unlock(&channel->mutex);
	return 0;

err_start:
	channel->start_count--;
	mutex_unlock(&channel->mutex);
	return ret;
}

int ndp_channel_stop(struct ndp_subscription *sub, int force)
{
	int ret = 0;
	struct ndp_channel *channel = sub->channel;

	mutex_lock(&channel->mutex);

	if (channel->locked_sub == sub)
		channel->locked_sub = NULL;

	/* stop now (i.e. the last one)? */
	if (--channel->start_count == 0) {
		ret = channel->ops->stop(channel, force);
		if (ret == -EAGAIN) {
			++channel->start_count;
			goto err_again;
		} else {
			ret = 0;
		}
	}

	spin_lock(&channel->lock);
	list_del_init(&sub->list_item);
	spin_unlock(&channel->lock);

err_again:
	mutex_unlock(&channel->mutex);
	return ret;
}

inline void ndp_channel_rxsync(struct ndp_subscription *sub, struct ndp_subscription_sync *sync)
{
	struct ndp_subscription *list_sub;
	unsigned long swptr, sub_swptr;
	size_t sub_lock, max_lock;
	struct ndp_channel *channel = sub->channel;

	sub->swptr = sync->swptr;

	spin_lock(&channel->lock);
	rmb();

	max_lock = 0;
	swptr = sub->swptr;

	/* Find the farthest swptr */
	list_for_each_entry(list_sub, &channel->list_subscriptions, list_item) {
		sub_swptr = list_sub->swptr;
		sub_lock = (channel->hwptr - sub_swptr) & channel->ptrmask;
		if (sub_lock > max_lock) {
			max_lock = sub_lock;
			swptr = sub_swptr;
		}
	}

	/* Update swptr only when changed */
	if (swptr != channel->swptr) {
		channel->swptr = swptr;
		channel->ops->set_swptr(channel, swptr);
	}

	/* Update hwptr */
	channel->hwptr = channel->ops->get_hwptr(channel);
	sub->hwptr = channel->hwptr;

	wmb();
	spin_unlock(&channel->lock);

	sync->hwptr = sub->hwptr;
}

inline void ndp_channel_txsync(struct ndp_subscription *sub, struct ndp_subscription_sync *sync)
{
	size_t len, chlen;

	struct ndp_channel *channel = sub->channel;

	sub->swptr = sync->swptr;
	sub->hwptr = sync->hwptr;

	spin_lock(&channel->lock);

	rmb();

	if (channel->locked_sub == sub) {
		/* This subscriber have lock */

		if (sub->hwptr != channel->swptr) {
			/* Subscriber puts some data */
			channel->swptr = sub->hwptr;
			channel->ops->set_swptr(channel, channel->swptr);
		}

		channel->hwptr = channel->ops->get_hwptr(channel);
		if (channel->ops->get_free_space != NULL)
			sync->size = channel->ops->get_free_space(channel);
		chlen = (channel->hwptr - channel->swptr - 1) & channel->ptrmask;

		/* Subscriber tries to lock LENGTH bytes */
		len = (sub->swptr - sub->hwptr) & channel->ptrmask;
		len = min(len, chlen);
		if (!len) {
			channel->locked_sub = NULL;
		}

		sub->hwptr = channel->swptr;
		sub->swptr = (channel->swptr + len) & channel->ptrmask;
	} else if (channel->locked_sub == NULL) {
		/* There is no locked subscriber */

		channel->hwptr = channel->ops->get_hwptr(channel);
		if (channel->ops->get_free_space != NULL)
			sync->size = channel->ops->get_free_space(channel);
		chlen = (channel->hwptr - channel->swptr - 1) & channel->ptrmask;

		/* Subscriber tries to lock LENGTH bytes */
		len = (sub->swptr - sub->hwptr) & channel->ptrmask;
		len = min(len, chlen);
		if (len) {
			channel->locked_sub = sub;
		}

		sub->hwptr = channel->swptr;
		sub->swptr = (channel->swptr + len) & channel->ptrmask;
	} else {
		/* Other subscribers have lock */
		sub->hwptr = channel->swptr;
		sub->swptr = channel->swptr;
	}

	spin_unlock(&channel->lock);
	sync->hwptr = sub->hwptr;
	sync->swptr = sub->swptr;
}

void ndp_channel_sync(struct ndp_subscription *sub, struct ndp_subscription_sync *sync)
{
	struct ndp_channel *channel = sub->channel;

	if (channel->id.type == NDP_CHANNEL_TYPE_RX) {
		ndp_channel_rxsync(sub, sync);
	} else {
		ndp_channel_txsync(sub, sync);
	}
}
