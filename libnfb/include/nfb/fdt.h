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

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


/*!
 * \brief Generate a dynamic header name from devicetree format
 * \param[in] fdt        Firmware devicetree
 * \param[in] offset     Header node offset
 * \return
 *   - Unique pointer (must be freed) to the generated string on success
 *   - NULL on error (errno is set)
 */
static inline char *nfb_fdt_rxhdr_hash(const void *fdt, int offset)
{
	int proplen = 0;
	int names_offset = 0;
	const int hash_size = 16 + 1;

	uint32_t header_id = fdt_getprop_u32(fdt, offset, "header_id", &proplen);
	if (proplen != (int)sizeof(uint32_t)) {
		errno = EBADFD;
		return NULL;
	}

	const char *names = fdt_getprop(fdt, offset, "item-name", &proplen);
	if (!names || proplen <= 0) {
		errno = EBADFD;
		return NULL;
	}

	size_t serial_cap = 128;
	char *serialized = malloc(serial_cap);
	if (!serialized) {
		errno = ENOMEM;
		return NULL;
	}
	char *serial_out = serialized;

	int n = snprintf(serialized, serial_cap, "%"PRIu32";", header_id);
	if (n < 0 || (size_t)n >= serial_cap) {
		errno = EOVERFLOW;
		free(serialized);
		return NULL;
	}

	while (names_offset < proplen) {
		const char *name = names + names_offset;
		struct nfb_fdt_packed_item item = nfb_fdt_packed_item_by_name(fdt, offset, name);
		if (item.name == NULL) {
			errno = EBADFD;
			free(serialized);
			return NULL;
		}

		size_t out_offset = (size_t)(serial_out - serialized);
		size_t remaining_cap = (size_t)serial_cap - out_offset;
		n = snprintf(serial_out, remaining_cap, "%s:%"PRIi16":%"PRIi16";", item.name,
			     item.width, item.offset);
		if (n < 0) {
			errno = EOVERFLOW;
			free(serialized);
			return NULL;
		}

		if ((size_t)n >= remaining_cap) {
			if (serial_cap > 1024) {
				// something is very wrong
				errno = EOVERFLOW;
				free(serialized);
				return NULL;
			}
			serial_cap *= 2;
			char *new_serialized = realloc(serialized, serial_cap);
			if (!new_serialized) {
				errno = ENOMEM;
				free(serialized);
				return NULL;
			}
			serialized = new_serialized;
			serial_out = serialized + out_offset;
			continue;
		}

		names_offset += strlen(name) + 1;
		serial_out += n;
	}

	// FNV-1a 64-bit
	uint64_t hash_val = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < (size_t)(serial_out - serialized); i++) {
		hash_val ^= serialized[i];
		hash_val *= 0x100000001b3ULL;
	}

	char *hash = malloc(hash_size);
	if (!hash) {
		errno = ENOMEM;
		free(serialized);
		return NULL;
	}

	static const char hex[] = "0123456789abcdef";
	for (unsigned int i = 0; i < (unsigned int)sizeof(hash_val); i++) {
		unsigned char byte = (unsigned char)(hash_val >> (i * 8));
		hash[i * 2] = hex[byte >> 4];
		hash[i * 2 + 1] = hex[byte & 0xf];
	}
	hash[sizeof(hash_val) * 2] = '\0';

	free(serialized);

	return hash;
}

/*!
 * \brief Generate a dynamic header name from devicetree format
 * \param[in] fdt     Firmware devicetree
 * \param[in] id      Header id in devicetree
 * \return
 *   - Unique pointer (must be freed) to the generated string on success
 *   - NULL on error (errno is set)
 */
static inline char *nfb_fdt_rxhdr_hash_by_id(const void *fdt, int id)
{
	int node = ndp_header_fdt_node_offset(fdt, 0, id);
	if (node <= 0)
		return NULL;

	return nfb_fdt_rxhdr_hash(fdt, node);
}
#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_FDT_H */
