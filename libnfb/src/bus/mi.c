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

#include <libfdt.h>

#include "../nfb.h"

struct nfb_bus_mi_priv
{
	size_t mmap_size;
	off_t mmap_offset;
	void * space;
};

/*
 * INFO1: Some CPU/machines hangs on high frequency bus access with size < 32bits
 * INFO2: Also Valgrind does ugly accesses with classic memcpy function
 * Here is a workaround for these issues
 */

static inline void nfb_bus_mi_memcopy(void *dst, const void *src, size_t nbyte, size_t offset)
{
	if (nbyte == 4) {
		*(uint32_t *) dst = *(const uint32_t *) src;
		return;
	} else if (nbyte == 8) {
		*(uint64_t *) dst = *(const uint64_t *) src;
		return;
	}

	/* Align access on 4/8B boundary first */
	if (offset & 0x03) {
		if (offset & 0x01 && nbyte >= 1) {
			*(uint8_t *) dst = *(const uint8_t *) src;
			if (nbyte == 1)
				return;
			src = ((const uint8_t *) src + 1);
			dst = ((uint8_t *) dst + 1);
			nbyte -= 1;
			offset += 1;
		}
		if (offset & 0x02 && nbyte >= 2) {
			*(uint16_t *) dst = *(const uint16_t *) src;
			if (nbyte == 2)
				return;
			src = ((const uint16_t *) src + 1);
			dst = ((uint16_t *) dst + 1);
			nbyte -= 2;
			offset += 2;
		}
	}
	#ifdef MI_ACCESS_ALIGN32
	if (offset & 0x04 && nbyte >= 4) {
		*(uint32_t *) dst = *(const uint32_t *) src;
		if (nbyte == 4)
			return;
		src = ((const uint32_t *) src + 1);
		dst = ((uint32_t *) dst + 1);
		nbyte -= 4;
		offset += 4;
	}
	#endif

	/* Loop with 64b accesses */
	while (nbyte >= 8) {
		*(uint64_t *) dst = *(const uint64_t *) src;
		if (nbyte == 8)
			return;
		src = ((const uint64_t *) src + 1);
		dst = ((uint64_t *) dst + 1);
		nbyte -= 8;
	}

	/* Access the remaining bytes */
	if (nbyte >= 4) {
		*(uint32_t *) dst = *(const uint32_t *) src;
		if (nbyte == 4)
			return;
		src = ((const uint32_t *) src + 1);
		dst = ((uint32_t *) dst + 1);
		nbyte -= 4;
	}
	if (nbyte >= 2) {
		*(uint16_t *) dst = *(const uint16_t *) src;
		if (nbyte == 2)
			return;
		src = ((const uint16_t *) src + 1);
		dst = ((uint16_t *) dst + 1);
		nbyte -= 2;
	}
	if (nbyte >= 1) {
		*(uint8_t *) dst = *(const uint8_t *) src;
		if (nbyte == 1)
			return;
		src = ((const uint8_t *) src + 1);
		dst = ((uint8_t *) dst + 1);
		nbyte -= 1;
	}
}

ssize_t nfb_bus_mi_read(void *bus_priv, void *buf, size_t nbyte, off_t offset)
{
	struct nfb_bus_mi_priv *bus = bus_priv;
	nfb_bus_mi_memcopy(buf, (uint8_t*) bus->space + offset, nbyte, offset);
	return nbyte;
}

ssize_t nfb_bus_mi_write(void *bus_priv, const void *buf, size_t nbyte, off_t offset)
{
	struct nfb_bus_mi_priv *bus = bus_priv;
	nfb_bus_mi_memcopy((uint8_t*) bus->space + offset, buf, nbyte, offset);
	return nbyte;
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

	/* Find MI driver node in FDT */
	fdt_offset = fdt_path_offset(fdt, path);
	if (fdt_offset < 0) {
		/* Compatibility for old driver DT layout */
		fdt_offset = fdt_path_offset(fdt, DRIVER_MI_PATH);

		if (fdt_offset < 0) {
			errno = ENODEV;
			goto err_fdt_path;
		}
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
	ops->read = nfb_bus_mi_read;
	ops->write = nfb_bus_mi_write;

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
