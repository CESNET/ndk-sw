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
/* UltraScale+ SYSMONE4 transfer function with the internal reference (UG580), in mdeg C */
#define SYSMON_USP_REG2TEMP(value) ((int32_t) ((int64_t) (value) * 509314 / 65536 - 280231))

/* The ID component's DT node does not name the FPGA family, so the card decides: these carry an UltraScale+ device. */
static inline int nc_idcomp_sysmon_is_usp(const void *fdt)
{
	static const char *const usp_cards[] = {
		"ALVEO_U200", "ALVEO_U250", "ALVEO_U55C", "VCU118", "FB2CGHH", "FB2CGG3", "FB4CGG3", "NT200A02",
	};
	const char *card;
	unsigned i;
	int node;

	node = fdt_path_offset(fdt, "/firmware");
	card = (const char *) fdt_getprop(fdt, node, "card-name", NULL);
	if (card == NULL)
		return 0;

	for (i = 0; i < sizeof(usp_cards) / sizeof(usp_cards[0]); i++) {
		if (!strcmp(card, usp_cards[i]))
			return 1;
	}
	return 0;
}

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

	*val = nc_idcomp_sysmon_is_usp(nfb_get_fdt(dev)) ? SYSMON_USP_REG2TEMP(temp) : SYSMON_REG2TEMP(temp);

	return 0;
}

static inline int nc_idcomp_repeater_set(struct nfb_device *dev, unsigned index, enum nc_idcomp_repeater status)
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
		return -ENODEV;
	reg = nfb_comp_read32(comp, IDCOMP_REG_REPEATER);

	val = status << (index * 2);
	reg &= ~(3 << (index *2));
	reg |= val;

	nfb_comp_write32(comp, IDCOMP_REG_REPEATER, reg);
	nfb_comp_close(comp);
	return 0;
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
