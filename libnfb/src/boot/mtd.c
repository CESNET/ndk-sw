/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - MTD API functions
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <sys/ioctl.h>
#include <linux/nfb/boot.h>

#include "../nfb.h"

ssize_t nfb_mtd_get_size(struct nfb_device *dev, int index)
{
	struct nfb_boot_ioc_mtd_info ioc_mtd;

	ioc_mtd.mtd = index;
	if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_INFO, &ioc_mtd) == -1) {
		return -1;
	}

	return ioc_mtd.size;
}

ssize_t nfb_mtd_get_erasesize(struct nfb_device *dev, int index)
{
	struct nfb_boot_ioc_mtd_info ioc_mtd;

	ioc_mtd.mtd = index;
	if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_INFO, &ioc_mtd) == -1) {
		return -1;
	}

	return ioc_mtd.erasesize;
}

int nfb_mtd_read(struct nfb_device *dev, int index, size_t addr, void *data, size_t size)
{
	struct nfb_boot_ioc_mtd ioc_mtd;

	ioc_mtd.mtd = index;
	ioc_mtd.addr = addr;
	ioc_mtd.data = data;
	ioc_mtd.size = size;

	if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_READ, &ioc_mtd) == -1) {
		return -1;
	}
	return 0;
}

int nfb_mtd_write(struct nfb_device *dev, int index, size_t addr, void *data, size_t size)
{
	struct nfb_boot_ioc_mtd ioc_mtd;

	ioc_mtd.mtd = index;
	ioc_mtd.addr = addr;
	ioc_mtd.data = data;
	ioc_mtd.size = size;

	if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_WRITE, &ioc_mtd) == -1) {
		return -1;
	}
	return 0;
}

int nfb_mtd_erase(struct nfb_device *dev, int index, size_t addr, size_t size)
{
	struct nfb_boot_ioc_mtd ioc_mtd;

	ioc_mtd.mtd = index;
	ioc_mtd.addr = addr;
	ioc_mtd.size = size;

	if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_ERASE, &ioc_mtd) == -1) {
		return -1;
	}
	return 0;
}
