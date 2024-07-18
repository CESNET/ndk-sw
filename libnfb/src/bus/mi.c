/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - memory interface module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <libfdt.h>

#include <emmintrin.h>
#include <immintrin.h>

#include "../nfb.h"

struct nfb_bus_mi_priv
{
	size_t mmap_size;
	off_t mmap_offset;
	void * space;
	bool is_wc_mapped;
};

/*
 * INFO1: Some CPU/machines hangs on high frequency bus access with size < 32bits
 * INFO2: Also Valgrind does ugly accesses with classic memcpy function
 * Here is a workaround for these issues
 */

#define _NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr) \
do {							\
	if (*nbyte == (size)) {			\
		if (wc_instr)				\
			*wc_used = true;		\
		return true;				\
	}						\
	*(src) = (const mtype *) *(src) + 1; 		\
	*(dst) = (mtype *) *(dst) + 1; 			\
	*nbyte -= (size);				\
} while (0)

#define _NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, size, src, dst, mtype, load, store, wc_used, wc_instr) \
do {							\
	mtype tmp;					\
	while (*nbyte >= (size)) {			\
		tmp = load(*(src));			\
		store(*(dst), tmp);			\
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr);	\
	}						\
} while (0)

#define _NFB_BUS_MI_MEMCPY_CYCLE_ST(nbyte, size, src, dst, mtype, stream, wc_used, wc_instr) \
do {							\
	while (*nbyte >= (size)) {			\
		stream(*(dst), *(const mtype *) *(src));	\
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr);	\
	}						\
} while (0)

#define _NFB_BUS_MI_MEMCPY_CYCLE_AS(nbyte, size, src, dst, mtype, wc_used, wc_instr) \
do {							\
	while (*nbyte >= (size)) {			\
		*(mtype *) *(dst) = *(const mtype *) *(src);	\
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, size, src, dst, mtype, wc_used, wc_instr);	\
	}						\
} while (0)


#define __NFB_BUS_MI_MEMCOPY_TEMPLATE(impl) \
static inline ssize_t _nfb_bus_mi_memcopy_rd_##impl(void *bus_priv, void *buf, size_t nbyte, off_t offset) \
{ \
	ssize_t ret; \
	bool wc_used = false; \
	struct nfb_bus_mi_priv *bus = bus_priv; \
	ret = nfb_bus_mi_memcopy_##impl(buf, (uint8_t*) bus->space + offset, nbyte, offset, &wc_used); \
	return ret; \
} \
\
static inline ssize_t _nfb_bus_mi_memcopy_wr_##impl(void *bus_priv, const void *buf, size_t nbyte, off_t offset) \
{ \
	ssize_t ret; \
	bool wc_used = false; \
	struct nfb_bus_mi_priv *bus = bus_priv; \
	ret = nfb_bus_mi_memcopy_##impl((uint8_t*) bus->space + offset, buf, nbyte, offset, &wc_used); \
\
	if (bus->is_wc_mapped || wc_used) \
		_mm_mfence(); \
	return ret; \
}


static inline bool nfb_bus_mi_memcopy_simple(void *dst, const void *src, size_t nbyte, size_t offset, bool *wc_used)
{
	(void) offset;
	(void) wc_used;

	if (nbyte == 4) {
		*(uint32_t *) dst = *(const uint32_t *) src;
		return true;
	} else if (nbyte == 8) {
		*(uint64_t *) dst = *(const uint64_t *) src;
		return true;
	}
	return false;
}

static inline bool nfb_bus_mi_memcopy_prelude(void **dst, const void **src, size_t *nbyte, size_t *offset, bool *wc_used)
{
	/* The offset variable is just for alignment check. The dst variable is alredy offseted. */
	/* Align access on 4/8B boundary first */
	if (*offset & 0x03) {
		if (*offset & 0x01 && *nbyte >= 1) {
			*(uint8_t *) *dst = *(const uint8_t *) *src;
			_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 1, src, dst, uint8_t, wc_used, false);
			*offset += 1;
		}
		if (*offset & 0x02 && *nbyte >= 2) {
			*(uint16_t *) *dst = *(const uint16_t *) *src;
			_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 2, src, dst, uint16_t, wc_used, false);
			*offset += 2;
		}
	}

	if (*offset & 0x04 && *nbyte >= 4) {
		*(uint32_t *) *dst = *(const uint32_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 4, src, dst, uint32_t, wc_used, false);
		*offset += 4;
	}
	return false;
}

static inline bool nfb_bus_mi_memcopy_interlude_avx_sse2(void **dst, const void **src, size_t *nbyte, size_t *offset, bool *wc_used)
{
	(void) offset;

	bool src256a = ((uintptr_t) *src & 0x1F) ? false : true;
	bool dst256a = ((uintptr_t) *dst & 0x1F) ? false : true;
	bool src128a = ((uintptr_t) *src & 0x0F) ? false : true;
	bool dst128a = ((uintptr_t) *dst & 0x0F) ? false : true;

	/* The _mm_stream* instructions are using the non-temporal hint.
	 * The non-temporal hint is implemented by using a write combining (WC) memory type protocol.
	 * The WC protocol uses a weakly-ordered memory consistency model, fencing operation should be used. */

	if (src256a && dst256a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_ST(nbyte, 32, src, dst, __m256i, _mm256_stream_si256, wc_used, true);
	} else if (src256a && !dst256a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 32, src, dst, __m256i, _mm256_stream_load_si256, _mm256_storeu_si256, wc_used, false);
	} else if (!src256a && dst256a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 32, src, dst, __m256i, _mm256_loadu_si256, _mm256_store_si256, wc_used, false);
	} else {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 32, src, dst, __m256i, _mm256_loadu_si256, _mm256_storeu_si256, wc_used, false);
	}

	if (src128a && dst128a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_ST(nbyte, 16, src, dst, __m128i, _mm_stream_si128, wc_used, true);
	} else if (src128a && !dst128a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 16, src, dst, __m128i, _mm_load_si128, _mm_storeu_si128, wc_used, false);
	} else if (!src128a && dst128a) {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 16, src, dst, __m128i, _mm_loadu_si128, _mm_stream_si128, wc_used, false);
	} else {
		_NFB_BUS_MI_MEMCPY_CYCLE_LS(nbyte, 16, src, dst, __m128i, _mm_loadu_si128, _mm_storeu_si128, wc_used, false);
	}

	return false;
}

static inline bool nfb_bus_mi_memcopy_postlude(void **dst, const void **src, size_t *nbyte, size_t *offset, bool *wc_used)
{
	(void) offset;

	_NFB_BUS_MI_MEMCPY_CYCLE_AS(nbyte, 8, src, dst, uint64_t, wc_used, false);

	/* Access the remaining bytes */
	if (*nbyte >= 4) {
		*(uint32_t *) *dst = *(const uint32_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 4, src, dst, uint32_t, wc_used, false);
	}
	if (*nbyte >= 2) {
		*(uint16_t *) *dst = *(const uint16_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 2, src, dst, uint16_t, wc_used, false);
	}
	if (*nbyte >= 1) {
		*(uint8_t *) *dst = *(const uint8_t *) *src;
		_NFB_BUS_MI_MEMCPY_CYCLE_ITER(nbyte, 1, src, dst, uint8_t, wc_used, false);
	}

	return false;
}

static inline ssize_t nfb_bus_mi_memcopy_avx2_sse2(void *dst, const void *src, size_t nbyte, size_t offset, bool *wc_used)
{
	ssize_t ret = nbyte;
	if (nfb_bus_mi_memcopy_simple(dst, src, nbyte, offset, wc_used))
		return ret;

	if (nfb_bus_mi_memcopy_prelude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;
	if (nfb_bus_mi_memcopy_interlude_avx_sse2(&dst, &src, &nbyte, &offset, wc_used))
		return ret;
	if (nfb_bus_mi_memcopy_postlude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;

	return ret;
}

static inline ssize_t nfb_bus_mi_memcopy_noopt(void *dst, const void *src, size_t nbyte, size_t offset, bool *wc_used)
{
	ssize_t ret = nbyte;
	if (nfb_bus_mi_memcopy_simple(dst, src, nbyte, offset, wc_used))
		return ret;

	if (nfb_bus_mi_memcopy_prelude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;
	if (nfb_bus_mi_memcopy_postlude(&dst, &src, &nbyte, &offset, wc_used))
		return ret;

	return ret;
}

__NFB_BUS_MI_MEMCOPY_TEMPLATE(avx2_sse2)
__NFB_BUS_MI_MEMCOPY_TEMPLATE(noopt)

ssize_t nfb_bus_mi_read(void *bus_priv, void *buf, size_t nbyte, off_t offset)
{
	return _nfb_bus_mi_memcopy_rd_avx2_sse2(bus_priv, buf, nbyte, offset);
}

ssize_t nfb_bus_mi_write(void *bus_priv, const void *buf, size_t nbyte, off_t offset)
{
	return _nfb_bus_mi_memcopy_wr_avx2_sse2(bus_priv, buf, nbyte, offset);
}

#define DRIVER_MI_PATH "/drivers/mi/"

int nfb_bus_open_mi(void *dev_priv, int bus_node, int comp_node, void **bus_priv, struct libnfb_bus_ext_ops* ops)
{
	const void *fdt;
	int fdt_offset;
	int proplen;
	char path[sizeof(DRIVER_MI_PATH) + 16];

	const void *prop;
	const fdt64_t *prop64;

	struct nfb_base_priv *dev = dev_priv;
	struct nfb_bus_mi_priv * bus;

	(void) comp_node;

	fdt = dev->fdt;

	prop = fdt_getprop(fdt, bus_node, "resource", NULL);
	if (prop == NULL)
		return -EINVAL;

	strcpy(path, DRIVER_MI_PATH);
	strcpy(path + sizeof(DRIVER_MI_PATH) - 1, prop);

	bus = malloc(sizeof(*bus));
	if (bus == NULL) {
		goto err_priv_alloc;
	}

	prop = fdt_getprop(fdt, bus_node, "map-as-wc", &proplen);
	bus->is_wc_mapped = prop && proplen == 0;

	/* Find MI driver node in FDT */
	fdt_offset = fdt_path_offset(fdt, path);
	if (fdt_offset < 0) {
		errno = ENODEV;
		goto err_fdt_path;
	}

	/* Get mmap size */
	prop64 = fdt_getprop(fdt, fdt_offset, "mmap_size", &proplen);
	if (proplen != sizeof(*prop64)) {
		errno = EBADFD;
		goto err_fdt_getprop;
	}
	bus->mmap_size = fdt64_to_cpu(*prop64);

	/* Get mmap offset */
	prop64 = fdt_getprop(fdt, fdt_offset, "mmap_base", &proplen);
	if (proplen != sizeof(*prop64)) {
		errno = EBADFD;
		goto err_fdt_getprop;
	}
	bus->mmap_offset = fdt64_to_cpu(*prop64);

	/* Map the memory for MI address space */
	bus->space = mmap(NULL, bus->mmap_size,
		PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, dev->fd, bus->mmap_offset);
	if (bus->space == MAP_FAILED) {
		goto err_mmap;
	}

	*bus_priv = bus;

	ops->read = _nfb_bus_mi_memcopy_rd_noopt;
	ops->write = _nfb_bus_mi_memcopy_wr_noopt;

	if (
			__builtin_cpu_supports("avx") &&
			__builtin_cpu_supports("avx2") &&
			__builtin_cpu_supports("sse2") &&
			1 ) {
		ops->read = nfb_bus_mi_read;
		ops->write = nfb_bus_mi_write;
	}

	return 0;

	//munmap((void*)dev->mmap_comp, dev->mmap_comp_size);
err_mmap:
err_fdt_getprop:
err_fdt_path:
	free(bus);
err_priv_alloc:
	return errno;
}

void nfb_bus_close_mi(void *bus_priv)
{
	struct nfb_bus_mi_priv * bus =  bus_priv;

	munmap((void*)bus->space, bus->mmap_size);
	free(bus);
}
