/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - info module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Tomas Hak <xhakto01@stud.fit.vutbr.cz>
 */

#include <sys/ioctl.h>

#include <linux/nfb/boot.h>

#include "nfb.h"

int nfb_sensor_get(struct nfb_device *dev, struct nfb_boot_ioc_sensor *s)
{
	int ret;

	ret = ioctl(dev->fd, NFB_BOOT_IOC_SENSOR_READ, s);
	if (ret == -1)
		return -errno;

	return ret;
}
