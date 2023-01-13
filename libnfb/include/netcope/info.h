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

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_INFO_H */
