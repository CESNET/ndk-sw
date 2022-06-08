/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - Informational functions
 *
 * Copyright (C) 2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_INFO_H
#define NETCOPE_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __KERNEL__
#include <dirent.h>
#include <stdio.h>
#endif

#include <libfdt.h>
#include <nfb/nfb.h>

#include <netcope/eth.h>

#define NFB_PATH_MAXLEN 64
#define NFB_BASE_DEV_PATH "/dev/nfb/"

struct nc_composed_device_info {
	int dev_index;
	int dev_count;
	int node_nr;
};

/* At least valid dev or pciname must be supplied.
 * If the pciname is NULL, info->dev_index will be invalid (-1). */
static inline int nc_get_composed_device_info_by_pci(struct nfb_device *dev,
		const char *pciname, struct nc_composed_device_info * info)
{
	char path[NFB_PATH_MAXLEN];
	int ret;
	int node, subnode;
	const void *fdt;
	const void *prop;
	int device_found;
#ifndef __KERNEL__
	DIR * d;
	struct dirent *dir;
#endif
	if (info == NULL)
		return -EINVAL;

	if (dev) {
		device_found = 0;

		info->node_nr = nfb_get_system_id(dev);
		info->dev_index = -1;
		info->dev_count = 0;
		fdt = nfb_get_fdt(dev);
		node = fdt_path_offset(fdt, "/system/device/");
		fdt_for_each_subnode(subnode, fdt, node) {
			prop = fdt_getprop(fdt, subnode, "pci-slot", NULL);
			if (device_found == 0) {
				info->dev_index++;
				if (pciname && prop && strcmp(prop, pciname) == 0) {
					device_found = 1;
				}
			}
			info->dev_count++;
		}

		return (pciname && device_found == 0) ? -ENODEV : 0;
#ifndef __KERNEL__
	} else if (pciname) {
		d = opendir(NFB_BASE_DEV_PATH "by-pci-slot/");
		if (d == NULL)
			return -ENODEV;

		while ((dir = readdir(d)) != NULL) {
			ret = snprintf(path, NFB_PATH_MAXLEN, NFB_BASE_DEV_PATH "by-pci-slot/%s", dir->d_name);
			if (ret <= 0 || ret == NFB_PATH_MAXLEN) {
				ret = -ENXIO;
				break;
			}

			ret = -ENODEV;
			dev = nfb_open(path);
			if (dev) {
				ret = nc_get_composed_device_info_by_pci(dev, pciname, info);
				if (ret == 0) {
					nfb_close(dev);
					break;
				}
				nfb_close(dev);
			}
		}
		closedir(d);
		return ret;
#endif
	}
	return -EINVAL;
}


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_INFO_H */
