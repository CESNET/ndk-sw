/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Core driver module of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/kref.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/idr.h>
#include <linux/xz.h>

#include "nfb.h"
#include "mi/mi.h"
#include "ndp/ndp.h"
#include "qdr/qdr.h"
#include "net/net.h"
#include "boot/boot.h"
#include "ndp_netdev/core.h"
#include "hwmon/nfb_hwmon.h"

MODULE_VERSION(PACKAGE_VERSION);
MODULE_AUTHOR("CESNET; Martin Spinler <spinler@cesnet.cz>");
MODULE_DESCRIPTION("NFB platform Linux driver");
MODULE_LICENSE("GPL");

struct nfb_device *nfb_devices[NFB_CARD_COUNT_MAX] = {NULL};
struct nfb_driver_ops nfb_registered_drivers[NFB_DRIVERS_MAX] = {};
struct mutex nfb_driver_register_mutex;

void *nfb_get_priv_for_attach_fn(struct nfb_device *nfb, nfb_driver_ops_attach_t attach)
{
	int i;
        for (i = 0; i < NFB_DRIVERS_MAX; i++) {
                if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK && nfb_registered_drivers[i].attach == attach) {
                        return nfb->list_drivers[i].priv;
		}
	}
	return ERR_PTR(-ENODEV);
}

/*
 * nfb_attach_drivers - attach embedded drivers to NFB device
 * @nfb: NFB device structure
 */
void nfb_attach_driver(struct nfb_device* nfb, int i)
{
	int ret;

	if (nfb_registered_drivers[i].attach && nfb->list_drivers[i].status == NFB_DRIVER_STATUS_NONE) {
		ret = nfb_registered_drivers[i].attach(nfb, &nfb->list_drivers[i].priv);
		nfb->list_drivers[i].status = ret == 0 ? NFB_DRIVER_STATUS_OK : NFB_DRIVER_STATUS_ERROR;
	}
}

void nfb_detach_driver(struct nfb_device *nfb, int i)
{
	if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK) {
		nfb_registered_drivers[i].detach(nfb, nfb->list_drivers[i].priv);
	}
	nfb->list_drivers[i].status = NFB_DRIVER_STATUS_NONE;
}

void nfb_attach_drivers_early(struct nfb_device* nfb)
{
	int i;
	mutex_lock(&nfb_driver_register_mutex);
	for (i = 0; i < NFB_DRIVERS_EARLY; i++) {
		nfb_attach_driver(nfb, i);
	}
	mutex_unlock(&nfb_driver_register_mutex);
}

void nfb_attach_drivers(struct nfb_device* nfb)
{
	int i;
	mutex_lock(&nfb_driver_register_mutex);
	for (i = NFB_DRIVERS_EARLY; i < NFB_DRIVERS_MAX; i++) {
		nfb_attach_driver(nfb, i);
	}
	mutex_unlock(&nfb_driver_register_mutex);
}

/*
 * nfb_detach_drivers - detach embedded drivers from NFB device
 * @nfb: NFB device structure
 */
void nfb_detach_drivers(struct nfb_device* nfb)
{
	int i;

	mutex_lock(&nfb_driver_register_mutex);
	for (i = NFB_DRIVERS_MAX - 1; i >= 0; i--) {
		nfb_detach_driver(nfb, i);
	}
	mutex_unlock(&nfb_driver_register_mutex);
}

/*
 * nfb_create - alloc and init NFB structure
 * return: NFB device structure
 */
struct nfb_device *nfb_create(void)
{
	int ret;
	struct nfb_device *nfb;

	/* Allocate and initialize nfb structure */
	nfb = kzalloc(sizeof(*nfb), GFP_KERNEL);
	if (nfb == NULL) {
		ret = -ENOMEM;
		goto err_alloc_nfb;
	}

	/* Init callback & driver lists */
	rwlock_init(&nfb->fdt_lock);
	mutex_init(&nfb->list_lock);
	INIT_LIST_HEAD(&nfb->list_mmap);
	INIT_LIST_HEAD(&nfb->pci_devices);
	INIT_LIST_HEAD(&nfb->buses);

	nfb->status = NFB_DEVICE_STATUS_INIT;

	ret = nfb_char_create(nfb);
	if (ret)
		goto err_char_create;

	return nfb;

err_char_create:
	kfree(nfb);

err_alloc_nfb:
	return ERR_PTR(ret);
}

/*
 * nfb_destroy - clean and free NFB structure
 */
void nfb_destroy(struct nfb_device *nfb)
{
	kfree(nfb);
}

/*
 * nfb_probe - activate and publish NFB structure for drivers and userspace
 * @nfb: NFB device structure
 */
int nfb_probe(struct nfb_device *nfb)
{
	int ret;
	int offset;

	/* Init drivers node in FDT */
	offset = fdt_next_node(nfb->fdt, -1, NULL);
	offset = fdt_add_subnode(nfb->fdt, offset, "drivers");
	fdt_setprop_u32(nfb->fdt, offset, "version", 0x00020000);

	nfb_lock_probe(nfb);

	nfb_attach_drivers_early(nfb);

	/* All low-level initializations complete,
	   now we can initialize and populate char devices */
	ret = nfb_char_probe(nfb);
	if (ret) {
		goto err_nfb_char_probe;
	}
	nfb_devices[nfb->minor] = nfb;

	/* Attach registered drivers to card */
	nfb_attach_drivers(nfb);

	/* Enable user applications to open device */
	nfb->status = NFB_DEVICE_STATUS_OK;

	return 0;

//	nfb_char_remove(nfb);
err_nfb_char_probe:
	return ret;
}

/*
 * nfb_remove - remove and unpublish NFB structure from drivers and userspace
 * @nfb: NFB device structure
 */
void nfb_remove(struct nfb_device *nfb)
{
	nfb_detach_drivers(nfb);
	nfb_devices[nfb->minor] = NULL;

	/* Remove the char device */
	nfb_char_remove(nfb);

	nfb_lock_remove(nfb);
}

static struct nfb_driver_ops nfb_driver_ops_zero = {};

int nfb_driver_register(struct nfb_driver_ops ops)
{
	int i;
	int index = -1;
	if (ops.attach == NULL || ops.detach == NULL) {
		return -1;
	}

	mutex_lock(&nfb_driver_register_mutex);
	for (i = 0; i < NFB_DRIVERS_MAX; i++) {
		if (nfb_registered_drivers[i].attach == NULL) {
			index = i;
			break;
		}
	}
	if (index == -1) {
		mutex_unlock(&nfb_driver_register_mutex);
		return -1;
	}

	nfb_registered_drivers[index] = ops;
	for (i = 0; i < NFB_CARD_COUNT_MAX; i++) {
		if (nfb_devices[i])
			nfb_attach_driver(nfb_devices[i], index);
	}
	mutex_unlock(&nfb_driver_register_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(nfb_driver_register);

void nfb_driver_unregister(struct nfb_driver_ops ops)
{
	int i;
	int index = -1;
	if (ops.attach == NULL || ops.detach == NULL) {
		return;
	}

	mutex_lock(&nfb_driver_register_mutex);
	for (i = 0; i < NFB_DRIVERS_MAX; i++) {
		if (nfb_registered_drivers[i].attach == ops.attach) {
			index = i;
			break;
		}
	}

	if (index == -1) {
		mutex_unlock(&nfb_driver_register_mutex);
		return;
	}

	for (i = 0; i < NFB_CARD_COUNT_MAX; i++) {
		if (nfb_devices[i])
			nfb_detach_driver(nfb_devices[i], index);
	}
	mutex_unlock(&nfb_driver_register_mutex);

	nfb_registered_drivers[index] = nfb_driver_ops_zero;
}
EXPORT_SYMBOL_GPL(nfb_driver_unregister);

static struct nfb_driver_ops embedded_driver_ops[] = {
	/* INFO: Synchronize position of NDP driver with NFB_DRIVIER_NDP value! */
	{
		.attach = nfb_mi_attach,
		.detach = nfb_mi_detach,
	},
	{
		.attach = nfb_boot_attach,
		.detach = nfb_boot_detach,
		.ioctl = nfb_boot_ioctl,
		.ioc_type = NFB_BOOT_IOC,
	},
	{
		.attach = nfb_ndp_attach,
		.detach = nfb_ndp_detach,
		.open = ndp_char_open,
		.release = ndp_char_release,
		.ioctl = ndp_char_ioctl,
		.ioc_type = _IOC_TYPE(NDP_IOC_SUBSCRIBE),
	},
	{
		.attach = nfb_qdr_attach,
		.detach = nfb_qdr_detach,
	},
	{
		.attach = nfb_net_attach,
		.detach = nfb_net_detach,
	},
	{
		.attach = nfb_ndp_netdev_attach,
		.detach = nfb_ndp_netdev_detach,
	},
	{
		.attach = nfb_hwmon_attach,
		.detach = nfb_hwmon_detach,
	},
#ifdef CONFIG_NFB_ENABLE_PMCI
	{
		.attach = nfb_fpga_image_load_attach,
		.detach = nfb_fpga_image_load_detach,
		.ioc_type = FPGA_IMAGE_LOAD_MAGIC,
		.ioctl = nfb_fpga_image_load_ioctl,
		.open = nfb_fpga_image_load_open,
		.release = nfb_fpga_image_load_release,
	},
#endif
};

/*
 * nfb_init - init NFB kernel module
 */
static int nfb_init(void)
{
	int ret;
	int i;

	ret = nfb_boot_init();
	if (ret)
		return ret;

	mutex_init(&nfb_driver_register_mutex);

	for (i = 0; i < ARRAY_SIZE(embedded_driver_ops); i++)
		nfb_driver_register(embedded_driver_ops[i]);

	ret = nfb_char_init();
	if (ret)
		goto err_nfb_char_init;

	ret = nfb_pci_init();
	if (ret < 0)
		goto err_register_driver;

	return 0;

	/* Error handling */
//	pci_unregister_driver(&nfb_driver);
err_register_driver:
	nfb_char_exit();
err_nfb_char_init:
	return ret;
}

/*
 * nfb_exit - release nfb kernel module
 */
static void nfb_exit(void)
{
	nfb_pci_exit();
	nfb_char_exit();

	nfb_boot_exit();
}

module_init(nfb_init);
module_exit(nfb_exit);
