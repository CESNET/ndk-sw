/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - subscriber module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>

#include "ndp.h"

static size_t ndp_subscriber_new_data(struct ndp_subscriber *subscriber)
{
	size_t ret = 0, max;
	struct ndp_subscription *sub, *max_sub;

	if (list_empty(&subscriber->list_head_subscriptions)) {
		return -1;
	}

	max = 0;
	max_sub = NULL;
	list_for_each_entry(sub, &subscriber->list_head_subscriptions, ndp_subscriber_list_item) {
		ret = ndp_subscription_rx_data_available(sub);
		if (ret > max) {
			max = ret;
			max_sub = sub;
			return ret;
		}
	}

	return ret;
}

static enum hrtimer_restart ndp_subscriber_poll_timer(struct hrtimer *timer)
{
	int ret;
	struct ndp_subscriber *subscriber = container_of(timer, struct ndp_subscriber, poll_timer);

	ret = ndp_subscriber_new_data(subscriber);
	if (ret > 0) {
		set_bit(NDP_WAKE_RX, &subscriber->wake_reason);
		wake_up_interruptible(&subscriber->poll_wait);
		return HRTIMER_NORESTART;
	} else if (ret < 0) {
		return HRTIMER_NORESTART;
	}

	hrtimer_forward(timer, hrtimer_get_expires(timer), ns_to_ktime(200 * 1000));
	return HRTIMER_RESTART;
}

/**
 * ndp_open - userspace application opens the device
 * @subscriber: ndp structure which application opens
 *
 * Allocate, initialize and attach new application structure to ndp instance.
 */
struct ndp_subscriber *ndp_subscriber_create(struct ndp *ndp)
{
	struct ndp_subscriber *subscriber;

	subscriber = kzalloc(sizeof(*subscriber), GFP_KERNEL);
	if (subscriber == NULL) {
		goto err_alloc_app_struct;
	}

	subscriber->ndp = ndp;

	INIT_LIST_HEAD(&subscriber->list_head);
	INIT_LIST_HEAD(&subscriber->list_head_subscriptions);
	init_waitqueue_head(&subscriber->poll_wait);
	hrtimer_init(&subscriber->poll_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	subscriber->poll_timer.function = ndp_subscriber_poll_timer;
	clear_bit(NDP_WAKE_RX, &subscriber->wake_reason);

	mutex_lock(&ndp->lock);
	list_add_tail(&subscriber->list_head, &ndp->list_subscribers);
	mutex_unlock(&ndp->lock);
	return subscriber;

err_alloc_app_struct:
	return NULL;
}

/**
 * ndp_close - userspace application closes the device
 * @subscriber: structure of closing application
 *
 * Close all subscriptions and free application structure.
 */
void ndp_subscriber_destroy(struct ndp_subscriber *subscriber)
{
	struct ndp_subscription *sub, *tmp;
	struct ndp *ndp;

	ndp = subscriber->ndp;

	hrtimer_cancel(&subscriber->poll_timer);
	list_for_each_entry_safe(sub, tmp, &subscriber->list_head_subscriptions, ndp_subscriber_list_item) {
		ndp_subscription_destroy(sub);
	}

	mutex_lock(&ndp->lock);
	list_del(&subscriber->list_head);
	mutex_unlock(&ndp->lock);

	kfree(subscriber);
}

struct ndp_subscription *ndp_subscription_by_id(struct ndp_subscriber *subscriber,
		void *id)
{
	struct ndp_subscription *sub;

	/* Find the channel specified with ID */
	list_for_each_entry(sub, &subscriber->list_head_subscriptions, ndp_subscriber_list_item) {
		if (sub == (struct ndp_subscription*) id)
			return sub;
	}
	return NULL;
}

int ndp_subscriber_poll(struct ndp_subscriber *subscriber, struct file *filp, struct poll_table_struct *wait)
{
	int ret;
	ktime_t to;

	ret = test_bit(NDP_WAKE_RX, &subscriber->wake_reason) ? (POLLIN | POLLRDNORM) : 0;
	if (ret) {
		hrtimer_cancel(&subscriber->poll_timer);
		clear_bit(NDP_WAKE_RX, &subscriber->wake_reason);
		return ret;
	}

	poll_wait(filp, &subscriber->poll_wait, wait);

	to = ktime_get();
	to = ktime_add_ns(to, 200 * 1000);
	hrtimer_start(&subscriber->poll_timer, to, HRTIMER_MODE_ABS);
	return ret;
}
