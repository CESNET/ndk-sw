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
	int nfb_id;     /* Index/node number of the NFB device (e.g. for use in device path: /dev/nfbX) */
	int nfb_flags;  /* Reserved for future use */

	int ep_index;   /* ID/index of PF endpoint inside NFB card with requested pciname */
	int ep_count;   /* Total count of PF endpoints inside NFB card */
	int ep_flags;   /* Reserved for future use */

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

		info->nfb_id = nfb_get_system_id(dev);
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


#define NC_IFC_INFO_FLAG_ACTIVE  1 /* nc_ifc_info is valid and can be examined further */
/* This is realised by eth_cnt == 0 */
/*#define NC_IFC_INFO_FLAG_VIRTUAL 2*/ /* interface is not physically on card/firmware */

/* This structure represents the interface, which should be created in operating system */
struct nc_ifc_info {
	int id;         /* Interface ID */
	int subid;      /* Interface subsidiary ID for naming in style ID.SUBID */
	int ep;         /* Endpoint index in composed device (non-strict) */
	int config;
	int flags;      /* Virtual Eth channel (doesn't exists on card) */

	/* Redundant info for simpler code, can be obtained from struct nc_ifc_map_info */
	int rxq_cnt;    /* Number of Eth channels associated with this ifc */
	int txq_cnt;    /* Number of Rx queues associated with this ifc */
	int eth_cnt;    /* Number of Tx queues associated with this ifc */
};

struct nc_ifc_queue_map_info {
	int id;         /* DMA queue channel ID within card */
	int ifc;        /* Interface ID which queue belongs to */
	int node_ctrl;  /* FDT node of DMA controller */
	int ep;         /* Endpoint index in composed device (strict), should be the same as in ifc */
	int config;     /* How the queue should be configured / used */
	int flags;
};

struct nc_ifc_eth_map_info {
	int id;         /* Eth channel ID within card */
	int ifc;        /* Interface ID which Eth channel belongs to */
	int node_eth;   /* FDT node of Eth channel */
	int node_port;  /* FDT node of physical port */
	int node_rxmac; /* FDT node of RxMAC */
	int node_txmac; /* FDT node of TxMAC */

	int port;       /* Port ID/index on card, -1 for virtual ifcs */
	int channel;	/* Channel ID/index within port (first channel when Eth channel uses multiple lanes) */
	int lane;       /* Channel ID/index within card (first lane when Eth channel uses multiple lanes) */

	int config;
	int flags;
};

struct nc_ifc_map_info {
	struct nc_ifc_info              *ifc;
	struct nc_ifc_queue_map_info    *rxq;
	struct nc_ifc_queue_map_info    *txq;
	struct nc_ifc_eth_map_info      *eth;

	int ifc_cnt;
	int rxq_cnt;
	int txq_cnt;
	int eth_cnt;
};

static inline int nc_ifc_map_info_create_ordinary(struct nfb_device *nfb, struct nc_ifc_map_info* mi)
{
	int i, q, ep;
	int unassigned_queue;
	int ret;
	int ifc_cnt_real;
	int port_cnt;
	const void *fdt;
	const char *prop;
	int node, subnode, pmdnode;
	int proplen;
	int channel_cnt;

	int ifc = 0;
	int eth = 0;

	struct nc_ifc_info *info;
	struct nc_ifc_eth_map_info *eth_info;
	struct nc_composed_device_info comp_dev_info;

	fdt = nfb_get_fdt(nfb);

	ret = nc_get_composed_device_info_by_pci(nfb, NULL, &comp_dev_info);
	if (ret)
		return ret;

	ret = -ENOMEM;

	/* TODO: Add real map mechanism into firmware / DT */

	mi->ifc = NULL;
	mi->rxq = NULL;
	mi->eth = NULL;

	mi->eth_cnt = 0;

	port_cnt = 0;

	fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
		/* Alloc interface info for each Ethernet channel */
		info = realloc(mi->ifc, sizeof(*info) * (ifc + 1));
		if (info == NULL) {
			goto err_alloc;
		}
		mi->ifc = info;
		info = &mi->ifc[ifc];

		info->id = ifc++;
		info->subid = -1;
		info->flags = 0;

		info->flags |= NC_IFC_INFO_FLAG_ACTIVE;

		info->rxq_cnt = 0;
		info->txq_cnt = 0;
		info->eth_cnt = 1;

		/* Assign Ethernet channel to interface */
		eth_info = realloc(mi->eth, sizeof(*eth_info) * (eth + 1));
		if (eth_info == NULL) {
			goto err_alloc;
		}

		mi->eth = eth_info;
		mi->eth_cnt++;

		eth_info = &mi->eth[eth];

		eth_info->id = eth++;
		eth_info->ifc = info->id;
		eth_info->config = 0;
		eth_info->flags = 0;

		/* TODO */
		eth_info->port = -1;
		eth_info->channel = -1;
		eth_info->lane = -1;

		eth_info->node_eth = node;
		eth_info->node_port = fdt_node_offset_by_phandle_ref(fdt, node, "pmd");
		eth_info->node_rxmac = fdt_node_offset_by_phandle_ref(fdt, node, "rxmac");
		eth_info->node_txmac = fdt_node_offset_by_phandle_ref(fdt, node, "txmac");

		if (eth_info->node_port >= 0) {
			if (mi->eth_cnt - 1 == 0) {
				eth_info->port = 0;
				eth_info->channel = 0;
			} else {
				if (eth_info->node_port == eth_info[-1].node_port) {
					eth_info->port = eth_info[-1].port;
					eth_info->channel = eth_info[-1].channel + 1;
				} else {
					eth_info->port = eth_info[-1].port + 1;
					eth_info->channel = 0;
				}
			}
		}
	}

	if (ifc == 0) {
		info = realloc(mi->ifc, sizeof(*info) * (ifc + 1));
		if (info == NULL) {
			goto err_alloc;
		}
		mi->ifc = info;
		info = &mi->ifc[ifc];

		info->id = ifc++;
		info->subid = -1;
		info->flags = 0;

		info->flags |= NC_IFC_INFO_FLAG_ACTIVE;

		info->rxq_cnt = 0;
		info->txq_cnt = 0;
		info->eth_cnt = 0;
	}

	/* TODO: Add stream/device helper into DT. Currently we must assume
	 * each eth is equally distributed to device/streams */
	for (i = 0; i < ifc; i++) {
		mi->ifc[i].ep = mi->ifc[i].id * comp_dev_info.ep_count / ifc;
	}


	/* ****************************************************************** */
	/* Get total queue count */

	mi->rxq_cnt = ndp_get_rx_queue_count(nfb);
	mi->txq_cnt = ndp_get_tx_queue_count(nfb);

	mi->rxq = malloc(sizeof(*mi->rxq) * (mi->rxq_cnt + mi->txq_cnt));
	if (mi->rxq == NULL) {
		goto err_alloc;
	}

	mi->txq = mi->rxq + mi->rxq_cnt;

	for (q = 0; q < mi->rxq_cnt; q++) {
		/* TODO: connect DMA queue with driver phandle and read pcie prop */
		//mi->rxq[q].ep = q / (mi->rxq_cnt / comp_dev_info.ep_count);
		mi->rxq[q].ep = q * comp_dev_info.ep_count / mi->rxq_cnt;
		mi->rxq[q].id = q;
		i = q * ifc / mi->rxq_cnt;
		mi->rxq[q].ifc = i;
		mi->ifc[i].rxq_cnt++;

	}
	for (q = 0; q < mi->txq_cnt; q++) {
		/* TODO: connect DMA queue with driver phandle and read pcie prop */
		//mi->txq[q].ep = q / (mi->txq_cnt / comp_dev_info.ep_count);
		mi->txq[q].ep = q * comp_dev_info.ep_count / mi->txq_cnt;
		mi->txq[q].id = q;
		i = q * ifc / mi->txq_cnt;
		mi->txq[q].ifc = i;
		mi->ifc[i].txq_cnt++;
	}

	/* ****************************************************************** */
	/* Map DMA queues to ifc */

	#if 0
	for (ep = 0; ep < comp_dev_info.ep_count; ep++) {
		int ipe; /* Interfaces per endpoint */
		int qt;

		/* Get number of interfaces for this endpoint */
		ipe = 0;
		for (i = 0; i < ifc; i++) {
			if (mi->ifc[i].ep == ep && mi->ifc[i].flags & NC_IFC_INFO_FLAG_ACTIVE) {
				ipe++;
			}
		}

		/* for queue type: [RxQ, TxQ] */
		for (qt = 0; qt < 2; qt++) {
			int q_cnt = qt == 0 ? mi->rxq_cnt : mi->txq_cnt;
			struct nc_ifc_queue_map_info *q_map = qt == 0 ? mi->rxq : mi->txq;

			int dev_local = 0;
			int qpe = 0; /* Queues per enpoint */

			/* Get number of queues for this endpoint */
			for (q = 0; q < q_cnt; q++) {
				if (q_map[q].ep == ep) {
					qpe++;
				}
			}
			/* Map queues */
			for (q = 0; q < q_cnt; q++) {
				if (q_map[q].ep == ep) {
					int ifc_local = 0;
					int ifc_target = dev_local * ipe / qpe;

					for (i = 0; i < ifc; i++) {
						if (mi->ifc[i].ep == ep && mi->ifc[i].flags & NC_IFC_INFO_FLAG_ACTIVE) {
							if (ifc_local == ifc_target) {
								q_map[q].ifc = i;
								if (qt == 0)
									mi->ifc[i].rxq_cnt++;
								else
									mi->ifc[i].txq_cnt++;
								break;
							}
							ifc_local++;
						}
					}
					dev_local++;
				}
			}
		}
	}

	/* ****************************************************************** */
	/* Create virtual ifc for unassigned RxQ / TxQ: for each PCI endpoint */

	info = realloc(mi->ifc, sizeof(*info) * (ifc + comp_dev_info.ep_count));
	if (info == NULL) {
		goto err_alloc;
	}
	mi->ifc = info;

	for (ep = 0; ep < comp_dev_info.ep_count; ep++) {
		info = &mi->ifc[ifc];

		info->id = ifc++;
		info->subid = -1;
		info->flags = 0;
		info->ep = ep;
		info->rxq_cnt = 0;
		info->txq_cnt = 0;
		info->eth_cnt = 0;

		for (q = 0; q < mi->rxq_cnt; q++) {
			if (mi->rxq[q].ep == ep && mi->rxq[q].ifc == -1) {
				info->rxq_cnt++;
				mi->rxq[q].ifc = info->id;
			}
		}

		for (q = 0; q < mi->txq_cnt; q++) {
			if (mi->txq[q].ep == ep && mi->txq[q].ifc == -1) {
				info->txq_cnt++;
				mi->txq[q].ifc = info->id;
			}
		}

//		info->flags |= NC_IFC_INFO_FLAG_VIRTUAL
		info->flags |= (info->rxq_cnt + info->txq_cnt) ? NC_IFC_INFO_FLAG_ACTIVE : 0;
	}
	#endif

	mi->ifc_cnt = ifc;

	return 0;

err_alloc:
	if (mi->ifc)
		free(mi->ifc);
	if (mi->eth)
		free(mi->eth);
	if (mi->rxq)
		free(mi->rxq);

	return ret;
}

static inline void nc_map_info_destroy(struct nc_ifc_map_info *mi)
{
	free(mi->ifc);
	free(mi->rxq);
	free(mi->eth);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_INFO_H */
