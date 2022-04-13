/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libfdt extension of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_FDT_H
#define NFB_FDT_H
#include <libfdt.h>

/* define for loop iterate throught compatible property of Device Tree */
#define fdt_for_each_compatible_node(fdt, node, compatible) \
for ( node = fdt_node_offset_by_compatible(fdt, -1, compatible); \
	node >= 0; \
	node = fdt_node_offset_by_compatible(fdt, node, compatible) )

static inline int fdt_node_offset_by_phandle_ref(const void *fdt, int fdt_offset,
                const char *propname)
{
        int proplen;
        uint32_t phandle;
        const fdt32_t *fdt_prop;

        fdt_prop = fdt_getprop(fdt, fdt_offset, propname, &proplen);
        if (proplen == sizeof(*fdt_prop)) {
                phandle = fdt32_to_cpu(*fdt_prop);
                return fdt_node_offset_by_phandle(fdt, phandle);
        }
        return -FDT_ERR_NOTFOUND;
}

#define __nfb_fdt_getprop(bits) \
static inline int fdt_getprop##bits(const void *fdt, int fdt_offset, const char *name, void *prop) \
{ \
	const fdt##bits##_t *p; \
	int proplen; \
	p = fdt_getprop(fdt, fdt_offset, name, &proplen); \
	if (proplen != sizeof(*p)) \
		return -1; \
	if (prop) \
		*((uint##bits##_t*)prop) = fdt##bits##_to_cpu(*p); \
	return 0; \
}

__nfb_fdt_getprop(64)
__nfb_fdt_getprop(32)

#undef __nfb_fdt_getprop

#endif // NFB_FDT_H
