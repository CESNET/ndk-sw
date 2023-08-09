/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb public header file - extension module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef LIBNFB_EXTENSION_H
#define LIBNFB_EXTENSION_H

#ifndef __KERNEL__
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct nfb_device;
struct nfb_bus;
struct nfb_comp;
struct ndp_queue;

struct libnfb_ext_abi_version {
	uint32_t major;
	uint32_t minor;
};

#define libnfb_ext_abi_version_current { \
	.major = 1,\
	.minor = 0,\
}

typedef ssize_t nfb_bus_read_func(void *p, void *buf, size_t nbyte, off_t offset);
typedef ssize_t nfb_bus_write_func(void *p, const void *buf, size_t nbyte, off_t offset);

struct libnfb_bus_ext_ops {
	nfb_bus_read_func *read;
	nfb_bus_write_func *write;
};

struct libnfb_ext_ops {
	int (*open)(const char *devname, int oflag, void **priv, void **fdt);
	void (*close)(void *priv);
	int (*bus_open_mi)(void *dev_priv, int bus_node, int comp_node, void ** bus_priv, struct libnfb_bus_ext_ops* ops);
	void (*bus_close_mi)(void *bus_priv);
	int (*comp_lock)(const struct nfb_comp *comp, uint32_t features);
	void (*comp_unlock)(const struct nfb_comp *comp, uint32_t features);

	int (*ndp_queue_open)(struct nfb_device *dev, void *dev_priv, unsigned index, int dir, int flags, struct ndp_queue ** pq);
	int (*ndp_queue_close)(struct ndp_queue *q);
};

typedef int libnfb_ext_get_ops_t(const char *devname, struct libnfb_ext_ops *ops);

/* NDP extensions */

struct ndp_packet;

typedef unsigned (*ndp_rx_burst_get_t)(void *priv, struct ndp_packet *packets, unsigned count);
typedef int (*ndp_rx_burst_put_t)(void *priv);

typedef unsigned (*ndp_tx_burst_get_t)(void *priv, struct ndp_packet *packets, unsigned count);
typedef int (*ndp_tx_burst_put_t)(void *priv);
typedef int (*ndp_tx_burst_flush_t)(void *priv);

struct ndp_queue_ops {
	/* Fast path */
	union {
		struct {
			ndp_rx_burst_get_t get;
			ndp_rx_burst_put_t put;
		} rx;
		struct {
			ndp_tx_burst_get_t get;
			ndp_tx_burst_put_t put;
			ndp_tx_burst_flush_t flush;
		} tx;
	} burst;

	/* Control path */
	struct {
		int (*start)(void *priv);
		int (*stop)(void *priv);
	} control;
};

struct ndp_queue * ndp_queue_create(struct nfb_device *dev, int numa, int type, int index);
void ndp_queue_destroy(struct ndp_queue* q);

void* ndp_queue_get_priv(struct ndp_queue *q);
void ndp_queue_set_priv(struct ndp_queue *q, void *priv);

struct ndp_queue_ops* ndp_queue_get_ops(struct ndp_queue *q);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_EXTENSION_H */
