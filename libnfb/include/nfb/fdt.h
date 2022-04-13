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

#include <libfdt.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_FDT_H */
