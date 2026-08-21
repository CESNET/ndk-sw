/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - PCIe endpoint enumeration from firmware FDT
 *
 * Copyright (C) 2026 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_PCI_EP_H
#define NETCOPE_PCI_EP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfdt.h>
#include <nfb/fdt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max endpoint index bit we track (PCI0 .. PCI31). */
#define NFB_PCI_EP_MAX 32

/* Bitmask of endpoints implied by MI bus resources ("PCIx,BARy"). */
static inline unsigned nfb_pci_ep_mask_by_mi_bus_nodes(const void *fdt)
{
	int node;
	int pci_index, bar;
	int proplen;
	unsigned mask = 0;
	const void *prop;

	if (fdt == NULL)
		return 0;

	fdt_for_each_compatible_node(fdt, node, "netcope,bus,mi") {
		prop = fdt_getprop(fdt, node, "resource", &proplen);
		if (prop == NULL || proplen <= 0 || ((const char *)prop)[proplen - 1] != 0)
			continue;
		if (sscanf(prop, "PCI%d,BAR%d", &pci_index, &bar) != 2)
			continue;
		if (pci_index < 0 || pci_index >= NFB_PCI_EP_MAX)
			continue;
		mask |= 1u << pci_index;
	}

	return mask;
}

/* Bitmask of /system/device/endpointN nodes present in the FDT. */
static inline unsigned nfb_pci_ep_mask_by_system_endpoints(const void *fdt)
{
	int node, subnode;
	unsigned mask = 0;
	const char *name;
	char *end;
	unsigned long idx;

	if (fdt == NULL)
		return 0;

	node = fdt_path_offset(fdt, "/system/device/");
	if (node < 0)
		return 0;

	fdt_for_each_subnode(subnode, fdt, node) {
		name = fdt_get_name(fdt, subnode, NULL);
		if (name == NULL || strncmp(name, "endpoint", 8) != 0)
			continue;
		idx = strtoul(name + 8, &end, 10);
		if (end == name + 8 || *end != '\0' || idx >= NFB_PCI_EP_MAX)
			continue;
		mask |= 1u << idx;
	}

	return mask;
}

/* Number of endpoints implied by MI bus resources ("PCIx,BARy"). */
static inline int nfb_pci_ep_count_by_mi_bus_nodes(const void *fdt)
{
	unsigned mask = nfb_pci_ep_mask_by_mi_bus_nodes(fdt);
	int count = 0;

	while (mask) {
		count += mask & 1u;
		mask >>= 1;
	}
	return count;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_PCI_EP_H */
