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

#ifdef CONFIG_HAVE_MAVX2
#include <emmintrin.h>
#include <immintrin.h>
#endif

#include "../nfb.h"


#ifndef CONFIG_HAVE_MAVX2
#define _mm_mfence() do {} while(0)
#endif


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
#include <netcope/mi.h>


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
#ifdef CONFIG_HAVE_MAVX2
			__builtin_cpu_supports("avx") &&
			__builtin_cpu_supports("avx2") &&
			__builtin_cpu_supports("sse2") &&
#endif
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
