/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - char module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "ndp.h"

/**
 * ndp_char_open - userspace application opens the device
 * @priv: ndp structure which application opens
 * @app_priv: private data for userspace application in NDP module
 *
 * Allocate, initialize and attach new application structure to ndp instance.
 */
int ndp_char_open(void *priv, void **app_priv, struct file *file)
{
	int ret = -ENOMEM;
	struct ndp *ndp = (struct ndp *) priv;
	struct ndp_subscriber *subscriber;

	subscriber = ndp_subscriber_create(ndp);
	if (subscriber == NULL)
		goto err_alloc_subscriber_struct;

	*app_priv = subscriber;
	return 0;

err_alloc_subscriber_struct:
	return ret;
}

/**
 * ndp_close - userspace application closes the device
 * @priv: ndp structure which application closes
 * @app_priv: private data for userspace application in NDP module
 *
 * Close all subscriptions and free application structure.
 */
void ndp_char_release(void *priv, void *app_priv, struct file *file)
{
	struct ndp_subscriber *subscriber = app_priv;

	/* FIXME: force stop ctrl */
	ndp_subscriber_destroy(subscriber);
}

/**
 * ndp_ioctl
 */
long ndp_char_ioctl(void *priv, void *app_priv, struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	struct ndp_subscriber *subscriber = app_priv;
	struct ndp_subscription *sub;
	struct ndp_subscription_sync sync;

	switch (cmd) {
	case NDP_IOC_SYNC: {
		if (copy_from_user(&sync, argp, sizeof(sync)))
			return -EFAULT;

		sub = ndp_subscription_by_id(subscriber, sync.id);
		if (sub == NULL)
			return -EBADF;

		ret = ndp_subscription_sync(sub, &sync);

		if (copy_to_user(argp, &sync, sizeof(sync)))
			return -EFAULT;
		break;
	}
	case NDP_IOC_SUBSCRIBE: {
		struct ndp_channel_request req;
		if (copy_from_user(&req, argp, sizeof(req)))
			return -EFAULT;

		/* Create new subscription */
		sub = ndp_subscription_create(subscriber, &req);
		if (IS_ERR(sub)) {
			ret = copy_to_user(argp, &req, sizeof(req));
			ret = PTR_ERR(sub);
			return ret;
		}

		if (copy_to_user(argp, &req, sizeof(req))) {
			ndp_subscription_destroy(sub);
			return -EFAULT;
		}

		break;
	}
	case NDP_IOC_START: {
		if (copy_from_user(&sync, argp, sizeof(sync)))
			return -EFAULT;

		sub = ndp_subscription_by_id(subscriber, sync.id);
		if (sub == NULL)
			return -EBADF;

		ret = ndp_subscription_start(sub, &sync);

		if (copy_to_user(argp, &sync, sizeof(sync)))
			return -EFAULT;

		break;
	}
	case NDP_IOC_STOP: {
		if (copy_from_user(&sync, argp, sizeof(sync)))
			return -EFAULT;

		sub = ndp_subscription_by_id(subscriber, sync.id);
		if (sub == NULL)
			return -EBADF;

		ret = ndp_subscription_stop(sub, 0);
		break;
	}
	default:
		return -ENXIO;
	}

	return ret;
}

int ndp_char_poll(void *priv, void *app_priv, struct file *filp, struct poll_table_struct *wait)
{
	//struct ndp *ndp = priv;
	struct ndp_subscriber *subscriber = app_priv;

	return ndp_subscriber_poll(subscriber, filp, wait);
}
