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


#define NFB_IFC_FLAG_ACTIVE  1
#define NFB_IFC_FLAG_VIRTUAL 2

struct nc_ifc_map_info {
	int node;	/* FDT eth node */
	int port_node;	/* FDT port node */
	int index;      /* Eth channel index within card */
/*	int id;       *//* Eth channel ID within card */
	int port;       /* Parent port index, -1 for virtual ifcs */
	int channel;    /* First channel index within port */
	int dev;        /* Composed device index */
	int flags;      /* Virtual Eth channel (doesn't exists on card) */
	int rxmac_node;
	int txmac_node;

	int rxmac_cnt;  /* Number of RX MAC associated with this ifc */
	int txmac_cnt;  /* Number of TX MAC associated with this ifc */
};

struct nc_queue_map_info {
#if 0
	int index;      /* DMA queue channel index within card */
#endif
	int eth;        /* Eth channel index */
	int dev;        /* Composed device index, should be the same as in eth */
//	int flags;
};

struct nc_mac_map_info {
	int node;
	int ifc;
};

struct nc_ifc_queue_map_info {
	struct nc_ifc_map_info   *ifc_map;
	struct nc_queue_map_info *rxq_map;
	struct nc_queue_map_info *txq_map;
	struct nc_mac_map_info   *rxm_map;
	struct nc_mac_map_info   *txm_map;

	int ifc_cnt;
	int rxq_cnt;
	int txq_cnt;
	int rxm_cnt;
	int txm_cnt;
};

static inline int nc_create_ordinary_map_info(struct nfb_device *dev, struct nc_ifc_queue_map_info* info)
{
	int i, q, s, d;
	int unassigned_queue;
	int ret;
	int ifc_cnt_real;
	int port_cnt;
	const void *fdt;
	const char *prop;
	int node, subnode, pmdnode;
        int proplen;
	int channel_cnt;

	struct nc_ifc_map_info *imap;
	int icnt;

	struct nc_composed_device_info comp_dev_info;

	fdt = nfb_get_fdt(dev);

	ret = nc_get_composed_device_info_by_pci(dev, NULL, &comp_dev_info);
	if (ret)
		return ret;

	info->ifc_map = NULL;
	info->rxq_map = NULL;
	info->rxm_map = NULL;


	/* TODO: Add real map mechanism into firmware / DT */

	/* ****************************************************************** */
	/* Get number of all PMD on card and its possible channels */

	icnt = 0;
	imap = NULL;
	port_cnt = 0;

	fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
		prop = fdt_getprop(fdt, node, "type", &proplen);
		if (prop) {
			/* TODO */
			if (strcmp(prop, "QSFP") == 0) {
				channel_cnt = 4;
			} else if (strcmp(prop, "QSFP-DD") == 0) {
				channel_cnt = 8;
			} else {
				free(info->ifc_map);
				return -ENOSYS;
			}

			imap = realloc(info->ifc_map, sizeof(*imap) * (icnt + channel_cnt));
			if (imap == NULL) {
				free(info->ifc_map);
				return -ENOMEM;
			}

			info->ifc_map = imap;
			for (i = 0; i < channel_cnt; i++) {
				imap[icnt].port_node = node;
				//imap[icnt].index = icnt;
				imap[icnt].index = -1;
				imap[icnt].port = port_cnt;
				imap[icnt].channel = i;
				imap[icnt].flags = 0;
//				imap[icnt].dev = ???;
				imap[icnt].rxmac_node = -1;
				imap[icnt].txmac_node = -1;

				imap[icnt].rxmac_cnt = 0;
				imap[icnt].txmac_cnt = 0;

				icnt++;
			}
		} else {
			free(info->ifc_map);
			return -ENODATA;
		}
		port_cnt++;
	}

	/* TODO: Add stream/device helper into DT. Currently we must assume
	 * each transciever is equally distributed to device/streams */
	for (i = 0; i < icnt; i++) {
		imap[i].dev  = imap[i].port * comp_dev_info.dev_count / port_cnt;
	}

	/* ****************************************************************** */
	/* Get number of all Rx/Tx MACs */

	i = 0;
	ifc_cnt_real = 0;
	info->rxm_cnt = 0;
	info->txm_cnt = 0;
	fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
#if 0
		/* TODO: Eth node doesn't specify PMA lines (though does specify PMD lines)
		 * Suppose that Eth nodes are in right order */
		node = fdt_subnode_offset(fdt, node, "pma-params");
		prop = fdt_getprop(fdt, node, "lines", &proplen);
#else
		/* TODO: PMD lines are bound with PMA rather than Eth channel */
		subnode = fdt_subnode_offset(fdt, node, "pmd-params");
		prop = fdt_getprop(fdt, subnode, "lines", &proplen);
#endif
		subnode = fdt_node_offset_by_phandle_ref(fdt, node, "pmd");
		if (prop == NULL || subnode < 0)
			continue;

		 /* Should not happend, only for disabled eth channels */
		while (i < icnt && imap[i].port_node != subnode)
			i++;
		if (i >= icnt)
			break;

		imap[i].node = node;
		imap[i].index = ifc_cnt_real++;
		imap[i].flags |= NFB_IFC_FLAG_ACTIVE;

		if (fdt_node_offset_by_phandle_ref(fdt, node, "rxmac") >= 0)
			info->rxm_cnt++;
		if (fdt_node_offset_by_phandle_ref(fdt, node, "txmac") >= 0)
			info->txm_cnt++;

		/* Advance imap index for lines count */
		i += proplen / sizeof(fdt32_t);
	}

	info->ifc_cnt = ifc_cnt_real;

	info->rxm_map = malloc(sizeof(*info->rxm_map) *
			(info->rxm_cnt + info->txm_cnt));

	if (info->rxm_map == NULL) {
		free(info->ifc_map);
		return -ENOMEM;
	}

	info->txm_map = info->rxm_map + info->rxm_cnt;

	/* ****************************************************************** */
	/* Assign MACs to ports */

	info->rxm_cnt = 0;
	info->txm_cnt = 0;
	fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
		pmdnode = fdt_node_offset_by_phandle_ref(fdt, node, "pmd");

		subnode = fdt_node_offset_by_phandle_ref(fdt, node, "rxmac");
		if (subnode >= 0) {
			for (i = 0; i < icnt; i++) {
				/* FIXME */
				if (imap[i].port_node == pmdnode && imap[i].rxmac_node == -1) {
					info->rxm_map[info->rxm_cnt].ifc = imap[i].index;
					info->rxm_map[info->rxm_cnt].ifc = i;
					imap[i].rxmac_node = subnode;
					imap[i].rxmac_cnt++;
					break;
				}
			}
			info->rxm_map[info->rxm_cnt++].node = subnode;
		}

		subnode = fdt_node_offset_by_phandle_ref(fdt, node, "txmac");
		if (subnode >= 0) {
			for (i = 0; i < icnt; i++) {
				/* FIXME */
				if (imap[i].port_node == pmdnode && imap[i].txmac_node == -1) {
					info->txm_map[info->txm_cnt].ifc = imap[i].index;
					info->txm_map[info->txm_cnt].ifc = i;
					imap[i].txmac_node = subnode;
					imap[i].txmac_cnt++;
					break;
				}
			}
			info->txm_map[info->txm_cnt++].node = subnode;
		}
	}

	/* ****************************************************************** */
	/* Get total queue count */

	info->rxq_cnt = ndp_get_rx_queue_count(dev);
	info->txq_cnt = ndp_get_tx_queue_count(dev);

	info->rxq_map = malloc(sizeof(*info->rxq_map) *
			(info->rxq_cnt + info->txq_cnt));

	if (info->rxq_map == NULL) {
		free(info->rxm_map);
		free(info->ifc_map);
		return -ENOMEM;
	}

	info->txq_map = info->rxq_map + info->rxq_cnt;

	for (q = 0; q < info->rxq_cnt; q++) {
		/* TODO: connect dma queue with driver phandle and read pcie prop */
		//info->rxq_map[q].dev = q / (info->rxq_cnt / comp_dev_info.dev_count);
		info->rxq_map[q].dev = q * comp_dev_info.dev_count / info->rxq_cnt;
		info->rxq_map[q].eth = -1;
	}
	for (q = 0; q < info->txq_cnt; q++) {
		/* TODO: connect dma queue with driver phandle and read pcie prop */
		//info->txq_map[q].dev = q / (info->txq_cnt / comp_dev_info.dev_count);
		info->txq_map[q].dev = q * comp_dev_info.dev_count / info->txq_cnt;
		info->txq_map[q].eth = -1;
	}

		/* TODO FIXME: Map DMAs to ETH */

	/* ****************************************************************** */
	/* Map DMA queues to ifc */

	for (d = 0; d < comp_dev_info.dev_count; d++) {
		int ipd;
		int qpd;
		ipd = 0;
		qpd = 0;
		for (q = 0; q < icnt; q++) {
			if (imap[q].dev == d && imap[q].flags & NFB_IFC_FLAG_ACTIVE) {
				ipd++;
			}
		}
		for (q = 0; q < info->rxq_cnt; q++) {
			if (info->rxq_map[q].dev == d) {
				qpd++;
			}
		}
		ret = 0;
		for (q = 0; q < info->rxq_cnt; q++) {
			if (info->rxq_map[q].dev == d) {
				int imap_index = ret * ipd / qpd/* + d * ipd*/;
				s = 0;
				for (i = 0; i < icnt; i++) {
					if (imap[i].dev == d && imap[i].flags & NFB_IFC_FLAG_ACTIVE) {
						if (s == imap_index)  {
							info->rxq_map[q].eth = i;
							break;
						}
						s++;
					}
				}
				ret++;
			}
		}

		qpd = 0;
		for (q = 0; q < info->txq_cnt; q++) {
			if (info->txq_map[q].dev == d) {
				qpd++;
			}
		}
		ret = 0;
		for (q = 0; q < info->txq_cnt; q++) {
			if (info->txq_map[q].dev == d) {
				s = 0;
				int imap_index = ret * ipd / qpd/* + d * ipd*/;
				for (i = 0; i < icnt; i++) {
					if (imap[i].dev == d && imap[i].flags & NFB_IFC_FLAG_ACTIVE) {
						if (s == imap_index)  {
							info->txq_map[q].eth = i;
							break;
						}
						s++;
					}
				}
				ret++;
			}
		}

	}

	/* ****************************************************************** */
	/* Create virtual ifc for unassigned rxq / txq */

	imap = realloc(info->ifc_map, sizeof(*imap) * (icnt + comp_dev_info.dev_count));
	if (imap == NULL) {
		free(info->rxq_map);
		free(info->ifc_map);
		return -ENOMEM;
	}
	info->ifc_map = imap;

	for (d = 0; d < comp_dev_info.dev_count; d++) {
		unassigned_queue = 0;
		for (q = 0; q < info->rxq_cnt; q++) {
			if (info->rxq_map[q].dev == d && info->rxq_map[q].eth == -1) {
				unassigned_queue = 1;
				info->rxq_map[q].eth = icnt;
			}
		}
		for (q = 0; q < info->txq_cnt; q++) {
			if (info->txq_map[q].dev == d && info->txq_map[q].eth == -1) {
				unassigned_queue = 1;
				info->txq_map[q].eth = icnt;
			}
		}
		imap[icnt].port_node = -1;
		imap[icnt].channel = -1;
		imap[icnt].index = icnt;
		imap[icnt].port = port_cnt;
		imap[icnt].flags = NFB_IFC_FLAG_VIRTUAL | (unassigned_queue ? NFB_IFC_FLAG_ACTIVE : 0);
		imap[icnt].dev = d;

		imap[icnt].rxmac_cnt = 0;
		imap[icnt].txmac_cnt = 0;
		icnt++;
	}

	info->ifc_cnt = icnt;

	return 0;

}

static inline void nc_destroy_map_info(struct nc_ifc_queue_map_info *info)
{
	free(info->ifc_map);
	free(info->rxq_map);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_INFO_H */
