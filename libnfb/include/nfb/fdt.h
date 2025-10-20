/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb public header file - FDT module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef LIBNFB_FDT_H
#define LIBNFB_FDT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libfdt.h>

/* define for loop iterate throught compatible property of Device Tree */
#define fdt_for_each_compatible_node(fdt, node, compatible) \
for ( node = fdt_node_offset_by_compatible(fdt, -1, compatible); \
	node >= 0; \
	node = fdt_node_offset_by_compatible(fdt, node, compatible) )

static inline int fdt_node_offset_by_phandle_ref(const void *fdt, int nodeoffset,
                const char *propname)
{
        int proplen;
        uint32_t phandle;
        const fdt32_t *fdt_prop;

        fdt_prop = (const fdt32_t*) fdt_getprop(fdt, nodeoffset, propname, &proplen);
        if (proplen == sizeof(*fdt_prop)) {
                phandle = fdt32_to_cpu(*fdt_prop);
                return fdt_node_offset_by_phandle(fdt, phandle);
        }
        return -FDT_ERR_NOTFOUND;
}

static inline uint32_t fdt_getprop_u32(const void *fdt, int nodeoffset, const char *name, int *lenp)
{
        const fdt32_t *fdt_prop;
	int proplen;

        fdt_prop = (const fdt32_t*) fdt_getprop(fdt, nodeoffset, name, &proplen);
	if (lenp)
		*lenp = proplen;
        if (proplen == sizeof(*fdt_prop)) {
                return fdt32_to_cpu(*fdt_prop);
        }
        return 0;
}

static inline uint64_t fdt_getprop_u64(const void *fdt, int nodeoffset, const char *name, int *lenp)
{
        const fdt64_t *fdt_prop;
	int proplen;

        fdt_prop = (const fdt64_t*) fdt_getprop(fdt, nodeoffset, name, &proplen);
	if (lenp)
		*lenp = proplen;
        if (proplen == sizeof(*fdt_prop)) {
                return fdt64_to_cpu(*fdt_prop);
        }
        return 0;
}

static inline int ndp_header_fdt_node_offset(const void *fdt, int dir, int id)
{
	int ret;
	int node;
	int proplen;
	const char * compatible = dir == 0 ? "cesnet,ofm,ndp-header-rx" : "cesnet,ofm,ndp-header-tx";
	fdt_for_each_compatible_node(fdt, node, compatible) {
		ret = fdt_getprop_u32(fdt, node, "header_id", &proplen);
		if (proplen == sizeof(uint32_t) && ret == id)
			return node;
	}
	return -1;
}

struct nfb_fdt_packed_item {
	const char *name;
	int16_t width;
	int16_t offset;
};

static inline struct nfb_fdt_packed_item nfb_fdt_packed_item_by_name(const void *fdt, int fdt_offset, const char *name)
{
	struct nfb_fdt_packed_item ret, err;
	int stroff = 0;
	int cnt, index = -1;
	int proplen;
	const char *fdt_prop;

	const fdt16_t *fdt_prop16;

	err.name = NULL;
	err.width = -1;
	err.offset = -1;

	cnt = 0;
	fdt_prop = (const char*) fdt_getprop(fdt, fdt_offset, "item-name", &proplen);
	while (stroff < proplen) {
		if (!strcmp(name, fdt_prop + stroff)) {
			index = cnt;
			ret.name = fdt_prop + stroff;
		}
		stroff += strlen(fdt_prop + stroff) + 1;
		cnt++;
	}

	if (index == -1)
		return err;

	fdt_prop16 = (const fdt16_t*) fdt_getprop(fdt, fdt_offset, "item-offset", &proplen);
	if (proplen != cnt * (signed)sizeof(uint16_t))
		return err;

	ret.offset = fdt16_to_cpu(fdt_prop16[index]);

	fdt_prop16 = (const fdt16_t*) fdt_getprop(fdt, fdt_offset, "item-width", &proplen);
	if (proplen != cnt * (signed)sizeof(uint16_t))
		return err;

	ret.width = fdt16_to_cpu(fdt_prop16[index]);
	return ret;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_FDT_H */
