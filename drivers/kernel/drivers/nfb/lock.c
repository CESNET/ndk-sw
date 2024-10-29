/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * MI bus driver module of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <libfdt.h>

#include "nfb.h"

/**
 * nfb_lock_probe - Initialize NFB device lock subsystem
 * @nfb: NFB device
 */
int nfb_lock_probe(struct nfb_device *nfb)
{
	INIT_LIST_HEAD(&nfb->lock_list);
	mutex_init(&nfb->lock_mutex);

	return 0;
}

/**
 * nfb_lock_remove - Deinitialize NFB device lock subsystem
 * @nfb: NFB device
 */
int nfb_lock_remove(struct nfb_device *nfb)
{
	struct nfb_lock_item *item, *temp;

	/* Delete all items */
	list_for_each_entry_safe(item, temp, &nfb->lock_list, list) {
		list_del(&item->list);
		kfree(item);
	}
	return 0;
}

/**
 * nfb_lock_open - Initialize locks for application
 * @nfb: NFB device
 * @app: NFB application
 */
int nfb_lock_open(struct nfb_device *nfb, struct nfb_app *app)
{
	return 0;
}

/**
 * nfb_lock_release - Deinitialize application locks
 * @nfb: NFB device
 * @app: NFB application
 *
 * Unlock all locks held by the @app application
 */
void nfb_lock_release(struct nfb_device *nfb, struct nfb_app *app)
{
	struct nfb_lock_item *item, *temp;

	mutex_lock(&nfb->lock_mutex);

	/* Delete items of current app */
	list_for_each_entry_safe(item, temp, &nfb->lock_list, list) {
		if (item->app == app) {
			list_del(&item->list);
			kfree(item);
		}
	}
	mutex_unlock(&nfb->lock_mutex);
}

/**
 * nfb_lock_try_lock - Try to lock a specific component with specific feature set
 * @nfb: NFB device
 * @app: NFB application
 * @lock: Lock information (component + features)
 */
int nfb_lock_try_lock(struct nfb_device *nfb, struct nfb_app *app, struct nfb_lock lock)
{
	int ret = 0;
	int len;
	struct nfb_lock_item *item, *temp;

	mutex_lock(&nfb->lock_mutex);

	temp = NULL;

	/* Check features through all aplications */
	list_for_each_entry(item, &nfb->lock_list, list) {
		if (strcmp(item->path, lock.path) == 0) {
			if ((item->features & lock.features) != 0) {
				/* Some of requested features are already locked */
				mutex_unlock(&nfb->lock_mutex);
				return -EBUSY;
			}

			/* This component is already locked by current application */
			if (item->app == app) {
				temp = item;
			}
		}
	}

	item = temp;

	if (item == NULL) {
		len = strlen(lock.path) + 1;
		item = kmalloc(sizeof(*item) + len, GFP_KERNEL);
		if (item == NULL) {
			mutex_unlock(&nfb->lock_mutex);
			return -ENOMEM;
		}

		INIT_LIST_HEAD(&item->list);
		item->features = 0;
		item->app = app;
		item->path = (char *) (item + 1);
		strncpy(item->path, lock.path, len);

		list_add(&item->list, &nfb->lock_list);
	}

	item->features |= lock.features;

	mutex_unlock(&nfb->lock_mutex);
	return ret;
}


/**
 * nfb_lock_unlock - Unlock specific features of specific component
 * @nfb: NFB device
 * @app: NFB application
 * @lock: Lock information (component + features)
 *
 * An application can only unlock a component it has locked before.
 * However, it is possible to unlock any feature set.
 */
int nfb_lock_unlock(struct nfb_device *nfb, struct nfb_app *app, struct nfb_lock lock)
{
	int ret = 0;
	struct nfb_lock_item *item, *temp;

	temp = NULL;

	mutex_lock(&nfb->lock_mutex);

	list_for_each_entry(item, &nfb->lock_list, list) {
		if (item->app == app && strcmp(item->path, lock.path) == 0) {
			temp = item;
			break;
		}
	}

	item = temp;

	if (item == NULL) {
		mutex_unlock(&nfb->lock_mutex);
		return -ENODEV;
	}

	item->features &= ~lock.features;

	if (item->features == 0) {
		list_del(&item->list);
		kfree(item);
	}

	mutex_unlock(&nfb->lock_mutex);
	return ret;
}

/**
 * nfb_lock_ioctl - NFB lock subsystem IOCTL handler
 * @nfb: NFB device
 * @app: NFB application
 * @cmd: IOCTL command
 * @arg: IOCTL data
 */
long nfb_lock_ioctl(struct nfb_device *nfb, struct nfb_app *app, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct nfb_lock lock;
	char path[MAX_FDT_PATH_LENGTH + 1];
	int ret;

	path[MAX_FDT_PATH_LENGTH] = '\0';

	if (cmd == NFB_LOCK_IOC_TRY_LOCK || cmd == NFB_LOCK_IOC_UNLOCK) {
		if (copy_from_user(&lock, argp, sizeof(lock)))
			return -EFAULT;
		ret = strncpy_from_user(path, lock.path, MAX_FDT_PATH_LENGTH);
		if (ret == MAX_FDT_PATH_LENGTH || ret <= 0)
			return -EINVAL;
		lock.path = path;
	}

	switch (cmd) {
	case NFB_LOCK_IOC_TRY_LOCK:
		return nfb_lock_try_lock(nfb, app, lock);
	case NFB_LOCK_IOC_UNLOCK:
		return nfb_lock_unlock(nfb, app, lock);
	default:
		return -ENOTTY;
	}
}
