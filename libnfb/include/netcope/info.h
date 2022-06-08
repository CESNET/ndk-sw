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

static inline const char *nc_info_get_fw_prop_string(struct nfb_device *dev, const char *propname, int *len)
{
        int proplen;
        const char *prop;
	const void *fdt = nfb_get_fdt(dev);
        int node = fdt_path_offset(fdt, "/firmware/");

        prop = (const char *) fdt_getprop(fdt, node, propname, &proplen);
	if (len)
		*len = prop ? proplen : 0;
	return prop;
}

static inline const char *nc_info_get_fw_project_name(struct nfb_device *dev, int *len)
{
	return nc_info_get_fw_prop_string(dev, "project-name", len);
}

static inline const char *nc_info_get_fw_project_version(struct nfb_device *dev, int *len)
{
	return nc_info_get_fw_prop_string(dev, "project-version", len);
}

struct nc_composed_device_info {
	int nfb_index; /* Index/node number of the NFB device (e.g. for use in device path: /dev/nfbX) */
	int nfb_flags; /* Reserved for future use */

	int ep_index; /* ID/index of PF endpoint inside NFB card with requested pciname */
	int ep_count; /* Total count of PF endpoints inside NFB card */
	int ep_flags; /* Reserved for future use */

	uint64_t eps_active; /* TODO: 64b bitmask of active PF endpoints (probed by the NFB driver) */
};

/*!
 * \brief Get composed device informations about the NFB
 * \param[in] dev      NFB device handle (can be NULL)
 * \param[in] pciname  PCI slot string in format "0000:00:00.0" (can be NULL)
 * \param[out] info    Pointer to a structure to be filled
 *
 * Function fills items in the nc_composed_device_info structure by given PCI slot or the nfb_device handle.
 *
 * At least valid dev or pciname should be supplied.
 * If the pciname is NULL, info->ep_index will be invalid (-1).
 */
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
		info->ep_index = -1;
		info->ep_flags = 0;
		info->ep_count = 0;
		info->eps_active = 0;
		fdt = nfb_get_fdt(dev);
		node = fdt_path_offset(fdt, "/system/device/");
		fdt_for_each_subnode(subnode, fdt, node) {
			prop = fdt_getprop(fdt, subnode, "pci-slot", NULL);
			if (device_found == 0) {
				info->ep_index++;
				if (pciname && prop && strcmp(prop, pciname) == 0) {
					device_found = 1;
				}
			}
			info->ep_count++;
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
