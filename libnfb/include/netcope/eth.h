/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - Ethernet nodes management
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_ETH_H
#define NETCOPE_ETH_H

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_NETCOPE_ETH "netcope,eth"

static inline int nc_eth_get_rxmac_node(const void *fdt, int nodeoffset)
{
	return fdt_node_offset_by_phandle_ref(fdt, nodeoffset, "rxmac");
}

static inline int nc_eth_get_txmac_node(const void *fdt, int nodeoffset)
{
	return fdt_node_offset_by_phandle_ref(fdt, nodeoffset, "txmac");
}

static inline int nc_eth_get_pcspma_control_node(const void *fdt, int nodeoffset, int *node_control_param)
{
	int node_pcspma = -1;
	int node_ctrl = -1;

	int proplen;
	const fdt32_t *prop32;

	prop32 = (const fdt32_t*) fdt_getprop(fdt, nodeoffset, "pcspma", &proplen);
	if (proplen == sizeof(*prop32))
		node_pcspma = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop32));

	prop32 = (const fdt32_t*) fdt_getprop(fdt, node_pcspma, "control", &proplen);
	if (proplen == sizeof(*prop32))
		node_ctrl = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop32));

	if (node_control_param) {
		*node_control_param = fdt_subnode_offset(fdt, node_pcspma, "control-param");
	}
	return node_ctrl;
}

static inline int nc_eth_get_count(struct nfb_device *dev)
{
	int node;
	int i = 0;
	fdt_for_each_compatible_node(nfb_get_fdt(dev), node, "netcope,eth") {
		i++;
	}
	return i;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_ETH_H */
