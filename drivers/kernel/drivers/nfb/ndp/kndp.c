/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP driver of the NFB platform - sync module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/unistd.h>
#include <linux/err.h>
#include <linux/errno.h>

#include "../nfb.h"
#include "kndp.h"

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#include <netcope/ndp.h>
#include <netcope/ndp_priv.h>

inline int _ndp_queue_sync(struct ndp_queue *q, struct ndp_subscription_sync *sync)
{
#ifdef __KERNEL__
	if (q->sub == NULL)
		return -ENOENT;
	return ndp_subscription_sync(q->sub, sync);
#else
	if (ioctl(q->fd, NDP_IOC_SYNC, sync))
		return errno;
	return 0;
#endif
}

inline int _ndp_queue_start(struct ndp_queue *q)
{
#ifdef __KERNEL__
	return ndp_subscription_start(ndp_subscription_by_id(q->subscriber, q->sync.id), &q->sync);
#else
	if (ioctl(q->fd, NDP_IOC_START, &q->sync))
		return errno;
	return 0;
#endif
}

inline int _ndp_queue_stop(struct ndp_queue *q)
{
#ifdef __KERNEL__
	ndp_subscription_stop(ndp_subscription_by_id(q->subscriber, q->sync.id), 1);
#else
	if (ioctl(q->fd, NDP_IOC_STOP, &q->sync))
		return errno;
#endif
	return 0;
}
#ifndef __KERNEL__
static void *nalloc(int numa_node, size_t size)
{
	if (numa_node == -1)
		return (malloc(size));

	return (numa_alloc_onnode(size, numa_node));
}

static void nfree(int numa_node, void *ptr, size_t size)
{
	if (numa_node == -1)
		free(ptr);
	else
		numa_free(ptr, size);
}
#endif

int nfb_queue_add(struct ndp_queue *q)
{
#ifdef __KERNEL__
//	(void*) q;
#else
	struct ndp_queue **queues;
	struct nfb_device *dev;

	dev = q->dev;
	queues = realloc(dev->queues, sizeof(*dev->queues) * (dev->queue_count + 1));
	if (queues == NULL)
		return ENOMEM;

	dev->queues = queues;
	dev->queues[dev->queue_count] = q;
	dev->queue_count++;
#endif
	return 0;
}

void nfb_queue_remove(struct ndp_queue *q)
{
#ifndef __KERNEL__
	struct ndp_queue **queues;

	queues = q->dev->queues;
	while (*queues != q)
		queues++;
	*queues = NULL;

	munmap(q->buffer, q->size * 2);
	nfree(q->numa, q, sizeof(*q));
#endif
}

int ndp_queue_open_init(struct nfb_device *dev, struct ndp_queue *q, unsigned index, int type)
{
	return nc_ndp_queue_open_init(dev, q, index, type);
}


void ndp_close_queue(struct ndp_queue *q)
{
	if (q->status == NDP_QUEUE_RUNNING)
		ndp_queue_stop(q);
#ifdef __KERNEL__
	if (q->sub) {
		/* FIXME: force stop ctrl */
		ndp_subscription_destroy(q->sub);
		q->sub = NULL;
	}
#endif
	nfb_queue_remove(q);
}

void ndp_close_rx_queue(struct ndp_queue *q)
{
	ndp_close_queue(q);
}

void ndp_close_tx_queue(struct ndp_queue *q)
{
	ndp_close_queue(q);
}

int ndp_queue_get_numa_node(const struct ndp_queue *q)
{
#ifdef __KERNEL__
	return dev_to_node(q->sub->channel->ring.dev);
#else
	return q->numa;
#endif
}

static inline int fdt_get_subnode_count(const void *fdt, const char *path)
{
	int count = 0;
	int node;
	int fdt_offset;

	fdt_offset = fdt_path_offset(fdt, path);
	fdt_for_each_subnode(node, fdt, fdt_offset) {
		count++;
	}
	return count;
}
#ifndef __KERNEL__
int ndp_get_rx_queue_count(const struct nfb_device *dev)
{
	return fdt_get_subnode_count(dev->fdt, "/drivers/ndp/rx_queues");
}

int ndp_get_tx_queue_count(const struct nfb_device *dev)
{
	return fdt_get_subnode_count(dev->fdt, "/drivers/ndp/tx_queues");
}

int ndp_get_rx_queue_available_count(const struct nfb_device *dev)
{
	int i;
	int count = 0;
	int total = ndp_get_rx_queue_count(dev);

	for (i = 0; i < total; i++) {
		if (ndp_rx_queue_is_available(dev, i))
			count++;
	}
	return count;
}

int ndp_get_tx_queue_available_count(const struct nfb_device *dev)
{
	int i;
	int count = 0;
	int total = ndp_get_tx_queue_count(dev);

	for (i = 0; i < total; i++) {
		if (ndp_tx_queue_is_available(dev, i))
			count++;
	}
	return count;
}
#endif

/*!
 * \brief Getter for tx buffer size
 * \param[in] queue  NDP TX queue
 */
long long unsigned ndp_queue_size(struct ndp_queue *q)
{
	return q->size;
}

int ndp_queue_start(struct ndp_queue *q)
{
	return nc_ndp_queue_start(q);
}

int ndp_queue_stop(struct ndp_queue *q)
{
	return nc_ndp_queue_stop(q);
}

unsigned ndp_rx_burst_get(struct ndp_queue *q, struct ndp_packet *packets, unsigned count)
{
	return nc_ndp_rx_burst_get(q, packets, count);
}

void ndp_rx_burst_put(struct ndp_queue *q)
{
	nc_ndp_rx_burst_put(q);
}

unsigned ndp_tx_burst_get(ndp_tx_queue_t *q, struct ndp_packet *packets, unsigned count)
{
	return nc_ndp_tx_burst_get(q, packets, count);
}

void ndp_tx_burst_put(struct ndp_queue *q)
{
	nc_ndp_tx_burst_put(q);
}

void ndp_tx_burst_flush(struct ndp_queue *q)
{
	nc_ndp_tx_burst_flush(q);
}
