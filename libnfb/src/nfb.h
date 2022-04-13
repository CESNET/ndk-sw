/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - base module private header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_H
#define NFB_H

#include <stdint.h>

#include <nfb/nfb.h>

struct ndp_queue;

/*!
 * \brief Structure for the NFB device
 */
struct nfb_device {
	int fd;                         /*!< NFB chardev file descriptor */
	void *fdt;                      /*!< NFB device Device Tree description */

	int queue_count;                /*!< Number of opened NDP queues */
	struct ndp_queue **queues;      /*!< Opened NDP queues pointers for poll function */
};


struct nfb_bus;

typedef ssize_t nfb_bus_read_func(void *p, void *buf, size_t nbyte, off_t offset);
typedef ssize_t nfb_bus_write_func(void *p, const void *buf, size_t nbyte, off_t offset);

/*!
 * \brief Structure for the NFB bus
 */
struct nfb_bus {
	const struct nfb_device *dev;   /*!< Bus's device */
	void *priv;                     /*!< Bus's private data */
	int state;                      /*!< Bus's state e.g. in case of error */
	int type;

	nfb_bus_read_func *read;
	nfb_bus_write_func *write;
};


/*!
 * \brief Structure for NFB device component
 */
struct nfb_comp {
	struct nfb_bus bus;             /*!< Component's bus */
	const struct nfb_device *dev;   /*!< Component's device */
	char *path;                     /*!< Component's path in the Device Tree */
	off_t base;                     /*!< Component's offset in the bus address space */
	size_t size;                    /*!< Component's size in the bus address space */
};


struct nfb_device *nfb_open_ext(const char *devname, int oflags);

int nfb_bus_open_mi(struct nfb_bus *bus, int node_offset);
int nfb_bus_open(struct nfb_comp *comp, int fdt_offset);

#define NFB_BUS_TYPE_MI 1


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

#endif /* NFB_H */
