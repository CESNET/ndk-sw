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

/* For compatibility with older versions of libfdt */
static inline int _fdt_find_max_phandle(const void *fdt, uint32_t *phandle)
{
	uint32_t max = 0;
	int offset = -1;
	uint32_t value;

	while (true) {
		offset = fdt_next_node(fdt, offset, NULL);
		if (offset < 0) {
			if (offset == -FDT_ERR_NOTFOUND)
				break;

			return offset;
		}

		value = fdt_get_phandle(fdt, offset);

		if (value > max)
			max = value;
	}

	if (phandle)
		*phandle = max;

	return 0;
}

#define _FDT_MAX_PHANDLE 0xfffffffe

static inline int _fdt_generate_phandle(const void *fdt, uint32_t *phandle)
{
	uint32_t max;
	int err;

	err = _fdt_find_max_phandle(fdt, &max);
	if (err < 0)
		return err;

	if (max == _FDT_MAX_PHANDLE)
		return -FDT_ERR_NOPHANDLES;

	if (phandle)
		*phandle = max + 1;

	return 0;
}

#endif // NFB_FDT_H
