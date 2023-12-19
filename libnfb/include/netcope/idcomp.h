/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - ID component
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_IDCOMP_H
#define NETCOPE_IDCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libfdt.h>
#include "eth.h"

enum nc_idcomp_repeater {
	IDCOMP_REPEATER_NORMAL = 0,
	IDCOMP_REPEATER_IDLE = 1,
	IDCOMP_REPEATER_UNKNOWN = 2,
	IDCOMP_REPEATER_REPEAT = 3,
};

#define IDCOMP_REG_SYSMON_BANK          0x44
#define IDCOMP_REG_REPEATER             0x70

#define IDCOMP_SYSMON_OFFSET            0x80
#define SYSMON_REG2TEMP(value) ((value) * 769 / 100 - 273150)

static inline int nc_idcomp_sysmon_get_temp(struct nfb_device *dev, int32_t *val)
{
	int nodeoffset;
	struct nfb_comp * comp;
	int32_t temp;

	nodeoffset = fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,idcomp");
	comp = nfb_comp_open(dev, nodeoffset);
	if (!comp)
		return -EINVAL;

	nfb_comp_write32(comp, IDCOMP_REG_SYSMON_BANK, 0);

	temp = nfb_comp_read32(comp, IDCOMP_SYSMON_OFFSET + 0) & 0xFFFF;

	nfb_comp_close(comp);

	*val = SYSMON_REG2TEMP(temp);

	return 0;
}

static inline void nc_idcomp_repeater_set(struct nfb_device *dev, unsigned index, enum nc_idcomp_repeater status)
{
	int node;
	int len;
	uint32_t reg;
	uint32_t val;
	const uint32_t *prop32;
	struct nfb_comp *comp;

	node = nfb_comp_find(dev, COMP_NETCOPE_ETH, index);
	if (node >= 0) {
		prop32 = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), node, "repeater-reg-index", &len);
		if (len == sizeof(*prop32))
			index = fdt32_to_cpu(*prop32);
	}

	node = fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,idcomp");
	comp = nfb_comp_open(dev, node);
	if (!comp)
		return;
	reg = nfb_comp_read32(comp, IDCOMP_REG_REPEATER);

	val = status << (index * 2);
	reg &= ~(3 << (index *2));
	reg |= val;

	nfb_comp_write32(comp, IDCOMP_REG_REPEATER, reg);
	nfb_comp_close(comp);
}

static inline enum nc_idcomp_repeater nc_idcomp_repeater_get(struct nfb_device *dev, unsigned index)
{
	int node;
	int len;
	uint32_t reg;
	const uint32_t *prop32;
	struct nfb_comp *comp;

	node = nfb_comp_find(dev, COMP_NETCOPE_ETH, index);
	if (node >= 0) {
		prop32 = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), node, "repeater-reg-index", &len);
		if (len == sizeof(*prop32))
			index = fdt32_to_cpu(*prop32);
	}

	node = fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,idcomp");
	comp = nfb_comp_open(dev, node);
	if (!comp)
		return IDCOMP_REPEATER_UNKNOWN;
	reg = nfb_comp_read32(comp, IDCOMP_REG_REPEATER);

	nfb_comp_close(comp);

	return (enum nc_idcomp_repeater) ((reg >> (index * 2)) & 3);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_IDCOMP_H */
