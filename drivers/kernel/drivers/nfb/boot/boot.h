/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Boot driver module header of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_BOOT_H
#define NFB_BOOT_H

#include <linux/pci.h>

#include <linux/spi/spi.h>

#include <linux/nfb/boot.h>

#include "../../spi-nor/spi-nor.h"

#include "../nfb.h"

struct nfb_boot {
	struct nfb_comp *comp;
	struct spi_device *spi;
	struct nfb_device *nfb;

	int num_image;

	int num_flash;
	struct map_info *map;
	struct spi_nor *nor;
	struct mtd_info **mtd;
	int flags;
	int controller_type;

	int mtd_bit;
	unsigned long mtd_size;
	int fb_active_flash;
};

struct mtd_info *cfi_probe(struct map_info *map);

int nfb_boot_mtd_init(struct nfb_boot *nfb_boot);
void nfb_boot_mtd_destroy(struct nfb_boot *nfb_boot);
int nfb_boot_mtd_read(struct nfb_boot *nfb_boot, int mtd, int addr, int size, void *data);

int nfb_boot_ioctl_mtd_read(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd);
int nfb_boot_ioctl_mtd_write(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd);

int nfb_boot_ioctl_mtd_erase(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd);
int nfb_boot_ioctl_mtd_info(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd_info __user *_ioc_mtd_info);

int nfb_mtd_read(struct nfb_device *dev, int index, size_t addr, void *data, size_t size);

int nfb_boot_attach(struct nfb_device* nfb, void **priv);
void nfb_boot_detach(struct nfb_device* nfb, void *priv);
/*int ndp_char_open(void *priv, void **app_priv, struct file *file);
void ndp_char_release(void *priv, void *app_priv, struct file *file);*/
long nfb_boot_ioctl(void *priv, void * app_priv, struct file *file, unsigned int cmd, unsigned long arg);
int nfb_boot_ioctl_error_disable(struct nfb_boot *nfb_boot);

int nfb_boot_reload(void *arg);

#define NFB_BOOT_FLAG_FB_SELECT_FLASH 1
#define NFB_BOOT_FLAG_FLASH_SET_ASYNC 2

#endif /* NFB_BOOT_H */
