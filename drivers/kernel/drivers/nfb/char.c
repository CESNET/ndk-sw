/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Char driver module of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/idr.h>
#include <linux/uaccess.h>
#include <linux/rculist.h>
#include <linux/fs.h>

#include <libfdt.h>

#include "nfb.h"
#include "ndp/ndp.h"

#include "boot/boot.h"


/* Global variables */
static int nfb_major;
static struct class *nfb_class;
static struct ida nfb_minor;

extern struct nfb_device *nfb_devices[NFB_CARD_COUNT_MAX];
extern struct nfb_driver_ops nfb_registered_drivers[NFB_DRIVERS_MAX];

struct spinlock open_lock;

/* Attributes for sysfs - get functions */
static ssize_t nfb_char_get_serial(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_device *nfb = dev_get_drvdata(dev);
	if (nfb->serial_str) {
		return scnprintf(buf, PAGE_SIZE, "%s\n", nfb->serial_str);
	}
	return scnprintf(buf, PAGE_SIZE, "%lld\n", nfb->serial);
}
static ssize_t nfb_char_get_cardname(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_device *nfb = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s\n", nfb->pci_name);
}
static ssize_t nfb_char_get_pcislot(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_device *nfb = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s\n", pci_name(nfb->pci));
}

static ssize_t nfb_boot_get_load_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfb_device *nfb = dev_get_drvdata(dev);
	struct nfb_boot *nfb_boot = nfb_get_priv_for_attach_fn(nfb, nfb_boot_attach);

	if (nfb_boot == NULL)
		return scnprintf(buf, PAGE_SIZE, "\n");

	return nfb_boot_load_get_status(nfb_boot, buf);
}

/* Attributes for sysfs - declarations */
DEVICE_ATTR(serial,   S_IRUGO, nfb_char_get_serial,   NULL);
DEVICE_ATTR(cardname, S_IRUGO, nfb_char_get_cardname, NULL);
DEVICE_ATTR(pcislot,  S_IRUGO, nfb_char_get_pcislot,  NULL);
DEVICE_ATTR(boot_load_status, S_IRUGO, nfb_boot_get_load_status,   NULL);

struct attribute *nfb_char_attrs[] = {
	&dev_attr_serial.attr,
	&dev_attr_cardname.attr,
	&dev_attr_pcislot.attr,
	&dev_attr_boot_load_status.attr,
	NULL,
};

struct attribute_group nfb_char_attr_group = {
	.attrs = nfb_char_attrs,
};

const struct attribute_group *nfb_char_attr_groups[] = {
	&nfb_char_attr_group,
	NULL,
};

int nfb_char_set_lr_callback(struct nfb_device *nfb, nfb_char_callback_t cb,
		void *argp)
{
	if (nfb->char_lr_cb)
		return -EEXIST;

	spin_lock(&open_lock);
	nfb->status = NFB_DEVICE_STATUS_RELEASE;
	nfb->char_lr_cb = cb;
	nfb->char_lr_data = argp;
	spin_unlock(&open_lock);

	return 0;
}

/**
 * nfb_char_mmap() - mmap directly card space or other nfb structures
 * @file: file pointer in user application
 * @vma: pointer to describing vm_area_struct
 *
 * Description
 *
 * Return: 0 when all goes OK.
 */
int nfb_char_register_mmap(struct nfb_device* nfb, size_t size, size_t *offset, int (*mmap)(struct vm_area_struct * vma, unsigned long offset, unsigned long size, void*priv), void*priv)
{
	int ret = 0;
	struct nfb_char_mmap_mod *item;
	struct list_head *pos;

	if (size == 0)
		return -EINVAL;

	*offset = 0;

	mutex_lock(&nfb->list_lock);

	/* Set offset to first sufficient free space */
	list_for_each(pos, &nfb->list_mmap) {
		item = list_entry(pos, struct nfb_char_mmap_mod, list);
		if ((*offset + size) <= item->offset)
			break;
		*offset = item->offset + item->size;
	}

	/* Create new list item and initialize */
	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (item == NULL) {
		ret = -ENOMEM;
		mutex_unlock(&nfb->list_lock);
		goto err_kmalloc;
	}
	item->mmap = mmap;
	item->offset = *offset;
	item->size = size;
	item->priv = priv;

	/* Add item between pos->prev and pos */
	list_add_tail(&item->list, pos);

	mutex_unlock(&nfb->list_lock);
	return ret;

	/* Error handling */
err_kmalloc:
	return ret;
}

int nfb_char_unregister_mmap(struct nfb_device *nfb, size_t offset)
{
	int ret = 0;
	struct nfb_char_mmap_mod *item;

	mutex_lock(&nfb->list_lock);

	/* Find the address in list */
	list_for_each_entry(item, &nfb->list_mmap, list) {
		if (item->offset == offset) {
			list_del(&item->list);
			mutex_unlock(&nfb->list_lock);
			kfree(item);
			return ret;
		}
	}

	mutex_unlock(&nfb->list_lock);
	return -ENODEV;
}

/**
 * nfb_char_open - init structures for newly opened chardev file decriptor
 * @inode: inode of chardev
 * @file: file pointer in user application
 */
static int nfb_char_open(struct inode *inode, struct file *file)
{
	int i;
	int size;
	int ret;

	struct nfb_device *nfb;
	struct nfb_app *app;
	void *app_priv;

	unsigned int minor;

	minor = MINOR(inode->i_rdev);
	if (minor >= NFB_CARD_COUNT_MAX)
		return -ENODEV;

	/* Get pointer to nfb struct */
	spin_lock(&open_lock);
	barrier();
	nfb = nfb_devices[minor];
	if (nfb == NULL || nfb->status != NFB_DEVICE_STATUS_OK) {
		spin_unlock(&open_lock);
		return -ENODEV;
	}
	spin_unlock(&open_lock);

	/* Check for O_APPEND flag */
	if (atomic_inc_return(&nfb->openers) > 1 && (file->f_flags & O_APPEND)) {
		atomic_dec(&nfb->openers);
		ret = -EBUSY;
		goto err_append;
	}

	/* Allocate main struct for application */
	app = kmalloc(sizeof(*app), GFP_KERNEL);
	if (app == NULL) {
		ret = -ENOMEM;
		goto err_kmalloc;
	}

	app->nfb = nfb;

	/* Copy FDT tree - needed for concurrent read/modification */
	size = fdt_totalsize(nfb->fdt);
	app->fdt = kmalloc(size, GFP_KERNEL);
	if (app->fdt == NULL) {
		ret = -ENOMEM;
		goto err_kmalloc_fdt;
	}
	memcpy(app->fdt, nfb->fdt, size);

	file->private_data = app;

	nfb_lock_open(nfb, app);

	/* Call open for all child drivers */
	for (i = 0; i < NFB_DRIVERS_MAX; i++) {
		if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK && nfb_registered_drivers[i].open) {
			ret = nfb_registered_drivers[i].open(nfb->list_drivers[i].priv, &app_priv, file);
			if (ret)
				goto err_open_child;
			app->driver_private[i] = app_priv;
		}
	}

	return 0;

err_open_child:
	for (--i; i >= 0; i--) {
		if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK && nfb_registered_drivers[i].release) {
			nfb_registered_drivers[i].release(nfb->list_drivers[i].priv, app->driver_private[i], file);
		}
	}

	nfb_lock_release(nfb, app);

	kfree(app->fdt);
err_kmalloc_fdt:
	kfree(app);
err_kmalloc:
err_append:
	return ret;
}

/**
 * nfb_char_release - free structures for closing descriptor
 * @inode: inode of chardev
 * @file: file pointer in user application
 */
static int nfb_char_release(struct inode *inode, struct file *file)
{
	int i;
	struct nfb_app *app = file->private_data;
	struct nfb_device *nfb = app->nfb;

	for (i = NFB_DRIVERS_MAX - 1; i >= 0; i--) {
		if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK && nfb_registered_drivers[i].release) {
			nfb_registered_drivers[i].release(nfb->list_drivers[i].priv, app->driver_private[i], file);
		}
	}

	nfb_lock_release(nfb, app);

	kfree(app->fdt);
	kfree(app);

	if (atomic_dec_return(&nfb->openers) == 0 && nfb->char_lr_cb) {
		nfb->char_lr_cb(nfb->char_lr_data);
	}
	return 0;
}

static loff_t nfb_char_llseek(struct file *file, loff_t off, int whence)
{
	struct nfb_app *app = file->private_data;
	loff_t newpos;
	int size = fdt_totalsize(app->fdt);

	switch (whence) {
	case SEEK_SET:
		newpos = off;
		break;
	case SEEK_CUR:
		newpos = file->f_pos + off;
		break;
	case SEEK_END:
		newpos = size + off;
		break;
	default:
		return -EINVAL;
	}
	if (newpos < 0 || newpos > size) {
		return -EINVAL;
	}
	file->f_pos = newpos;
	return newpos;
}

static ssize_t nfb_char_read(struct file *file, char __user *buffer, size_t length, loff_t *offset)
{
	struct nfb_app *app = file->private_data;
	int ret = 0;
	// TODO: Replace totalsize with something smaller :)
	int size = fdt_totalsize(app->fdt);

	if(*offset >= size)
		return 0;
	if(*offset + length > size)
		length = size - *offset;

	ret = copy_to_user(buffer, app->fdt + *offset, length);
	if(ret)
		return -EFAULT;

	*offset += length;
	return length;
}

/**
 * nfb_char_mmap - mmap directly card space or other nfb structures
 * @file: file pointer in user application
 * @vma: pointer to describing vm_area_struct
 */
static int nfb_char_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = -EINVAL;
	struct nfb_app *app = file->private_data;
	struct nfb_device *nfb = app->nfb;
	struct nfb_char_mmap_mod *item;

	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	vma->vm_private_data = app;

	mutex_lock(&nfb->list_lock);

	/* Select mmap item by offset */
	list_for_each_entry(item, &nfb->list_mmap, list) {
		if (item->offset <= offset && item->size >= size &&
				item->offset + item->size >= offset + size) {
			ret = item->mmap(vma, offset, size, item->priv);
			break;
		}
	}

	mutex_unlock(&nfb->list_lock);
	return ret;
}

/**
 * nfb_char_ioctl - ioctl function for chardev file
 * @file: file pointer in user application
 * @cmd: type of IOC command
 * @arg: argument for IOC command
 */
static long nfb_char_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int i;
	int ret = -ENXIO;
	struct nfb_app *app = file->private_data;
	struct nfb_device *nfb = app->nfb;

	for (i = 0; i < NFB_DRIVERS_MAX; i++) {
		if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK && nfb_registered_drivers[i].ioctl && nfb_registered_drivers[i].ioc_type == _IOC_TYPE(cmd)) {
			ret = nfb_registered_drivers[i].ioctl(nfb->list_drivers[i].priv, app->driver_private[i], file, cmd, arg);
			return ret;
		}
	}

	switch(_IOC_TYPE(cmd)) {
	case NFB_LOCK_IOC:
		ret = nfb_lock_ioctl(nfb, app, cmd, arg);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * nfb_char_poll - poll function for chardev file
 * @file: file pointer in user application
 * @wait: struct poll_table_struct
 */
static unsigned int nfb_char_poll(struct file *file, struct poll_table_struct *wait)
{
	struct nfb_app *app = file->private_data;
	struct nfb_device *nfb = app->nfb;

	if (nfb->list_drivers[NFB_DRIVER_NDP].status == NFB_DRIVER_STATUS_OK) {
		return ndp_char_poll(nfb->list_drivers[NFB_DRIVER_NDP].priv, app->driver_private[NFB_DRIVER_NDP], file, wait);
	}
	return 0;
}

static struct file_operations nfb_fops = {
	.owner          = THIS_MODULE,
	.open           = nfb_char_open,
	.llseek         = nfb_char_llseek,
	.release        = nfb_char_release,
	.unlocked_ioctl = nfb_char_ioctl,
	.mmap           = nfb_char_mmap,
	.read           = nfb_char_read,
	.poll           = nfb_char_poll,
//	.write          = nfb_char_write,
};

/**
 * nfb_char_create - alloc char device
 * @nfb: newly created nfb struct
 */
int nfb_char_create(struct nfb_device *nfb)
{
	int ret = -ENOMEM;

	nfb->minor = ret = ida_simple_get(&nfb_minor, 0, NFB_CARD_COUNT_MAX, GFP_KERNEL);
	if (nfb->minor < 0) {
		printk("NFB driver: unable to allocate new minor: %d\n", ret);
		goto err_ida_simple_get;
	}
	return 0;

	/* Error handling */
//	ida_simple_remove(&nfb_minor, nfb->minor);
err_ida_simple_get:
	return ret;
}

/**
 * nfb_char_probe - create new character device for probed card
 * @nfb: newly created nfb struct
 */
int nfb_char_probe(struct nfb_device *nfb)
{
	int ret = -ENOMEM;

	nfb->dev = device_create_with_groups(nfb_class, &nfb->pci->dev, MKDEV(nfb_major, nfb->minor),
			nfb, nfb_char_attr_groups, "nfb%d", nfb->minor);
	if (nfb->dev == NULL) {
		goto err_device_create;
	}
	return 0;

	/* Error handling */
//	device_destroy(nfb_class, MKDEV(nfb_major, nfb->minor));
err_device_create:
	return ret;
}

/**
 * nfb_char_remove - remove character device for card
 * @nfb: nfb struct to be removed
 */
void nfb_char_remove(struct nfb_device* nfb)
{
	device_destroy(nfb_class, MKDEV(nfb_major, nfb->minor));
	ida_simple_remove(&nfb_minor, nfb->minor);
}

/*
 * nfb_char_init - init chardev part of nfb kernel module
 */
int nfb_char_init(void)
{
	int ret;

	spin_lock_init(&open_lock);
	ida_init(&nfb_minor);
	nfb_major = register_chrdev(0, "nfb", &nfb_fops);
	if (nfb_major < 0) {
		ret = nfb_major;
		goto err_register_chrdev;
	}

#ifdef CONFIG_CLASS_CREATE_HAVE_ONE_PARAMETER
	nfb_class = class_create("nfb");
#else
	nfb_class = class_create(THIS_MODULE, "nfb");
#endif
	if (IS_ERR(nfb_class)) {
		ret = PTR_ERR(nfb_class);
		printk(KERN_ERR "nfb: class_create failed: %d\n", ret);
		goto err_class_create;
	}
	return 0;

	/* Error handling */
//	class_destroy(nfb_class);
err_class_create:
	unregister_chrdev(nfb_major, "nfb");
err_register_chrdev:
	return ret;
}

/*
 * nfb_char_exit - release chardev part of nfb kernel module
 */
void nfb_char_exit(void)
{
	class_destroy(nfb_class);
	unregister_chrdev(nfb_major, "nfb");
}
