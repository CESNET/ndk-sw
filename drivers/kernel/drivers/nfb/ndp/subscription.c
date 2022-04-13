/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP driver of the NFB platform - subscription module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include "ndp.h"

size_t ndp_subscription_rx_data_available(struct ndp_subscription *sub)
{
	size_t ret = 0;
	struct ndp_channel *channel;
	unsigned long hwptr;

	channel = sub->channel;

		return 0;
	if (channel->id.type != NDP_CHANNEL_TYPE_RX)
		return ret;

	hwptr = channel->ops->get_hwptr(channel);
	ret = (hwptr - sub->swptr) & (channel->ring.size - 1);

	return ret;
}

int ndp_subscription_sync(struct ndp_subscription* sub,
		struct ndp_subscription_sync *sync)
{
	int ret = 0;

	if (sub->status != NDP_SUB_STATUS_RUNNING) {
		return -EBADF;
	}

	ndp_channel_sync(sub, sync);

	return ret;
}

int ndp_subscription_start(struct ndp_subscription *sub,
		struct ndp_subscription_sync *sync)
{
	int ret;
	if (sub->status != NDP_SUB_STATUS_SUBSCRIBED)
		return -EBADF;

	ret = ndp_channel_start(sub);
	if (ret)
		return ret;

	if (sub->channel->id.type == NDP_CHANNEL_TYPE_RX) {
		sync->hwptr = sub->hwptr;
		sync->swptr = sub->swptr;
	} else {

	}

	sub->status = NDP_SUB_STATUS_RUNNING;

	return 0;
}

void ndp_subscription_stop(struct ndp_subscription *sub)
{
	if (sub->status != NDP_SUB_STATUS_RUNNING) {
		//dev_warn("Trying to stop not started subscription.");
		return;
	}
	ndp_channel_stop(sub);
	sub->status = NDP_SUB_STATUS_SUBSCRIBED;
}

struct ndp_subscription *ndp_subscription_create(
		struct ndp_subscriber *subscriber,
		struct ndp_channel_request *channel_req)
{
	int ret;
	struct ndp_subscription *sub;
	struct ndp *ndp = subscriber->ndp;

	struct ndp_channel *channel;
	struct ndp_channel_id id;

	mutex_lock(&ndp->lock);
	id.index = channel_req->index;
	id.type = (channel_req->type & 1) ? NDP_CHANNEL_TYPE_TX : NDP_CHANNEL_TYPE_RX;

	/* Find the channel specified with index and type */
	list_for_each_entry(channel, &ndp->list_channels, list_ndp) {
		if (memcmp(&id, &channel->id, sizeof(id)) == 0) {
			break;
		}
	}

	/* Channel not exists */
	if (&channel->list_ndp == &ndp->list_channels) {
		ret = -ENODEV;
		goto err_not_found;
	}

	if (channel->ring.size == 0) {
		ret = -EBADFD;
		goto err_noring;
	}

	sub = kzalloc_node(sizeof(struct ndp_subscription), GFP_KERNEL, dev_to_node(channel->ring.dev));
	if (sub == NULL) {
		ret = -ENOMEM;
		goto err_kmalloc;
	}

	sub->subscriber = subscriber;
	sub->status = NDP_SUB_STATUS_SUBSCRIBED;
	sub->channel = channel;
	INIT_LIST_HEAD(&sub->list_item);
	INIT_LIST_HEAD(&sub->ndp_subscriber_list_item);

	ret = ndp_channel_subscribe(sub, &channel_req->flags);
	if (ret) {
		goto err_channel_subscribe;
	}

	channel_req->id = sub;

	list_add(&sub->ndp_subscriber_list_item, &subscriber->list_head_subscriptions);

	mutex_unlock(&ndp->lock);
	return sub;

err_channel_subscribe:
	kfree(sub);
err_kmalloc:
err_noring:
err_not_found:
	mutex_unlock(&ndp->lock);
	return ERR_PTR(ret);
}

void ndp_subscription_destroy(struct ndp_subscription *sub)
{
	struct ndp_subscriber *subscriber = sub->subscriber;
	struct ndp *ndp = subscriber->ndp;

	if (sub->status == NDP_SUB_STATUS_RUNNING) {
		ndp_subscription_stop(sub);
	}

	ndp_channel_unsubscribe(sub);

	mutex_lock(&ndp->lock);
	list_del(&sub->ndp_subscriber_list_item);
	mutex_unlock(&ndp->lock);
	kfree(sub);
}
