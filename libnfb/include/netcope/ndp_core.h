/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - data transmission module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#ifndef __KERNEL__
#define __ALIGN_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define __ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a) - 1)
#define ALIGN(x, a)		__ALIGN((x), (a))

#if defined(_LITTLE_ENDIAN) || (defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && (__BYTE_ORDER == __LITTLE_ENDIAN))
  #define le16_to_cpu(X) ((uint16_t)X)
  #define cpu_to_le16(X) ((uint16_t)X)
#elif defined(_BIG_ENDIAN) || (defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) && (__BYTE_ORDER == __BIG_ENDIAN))
  #define le16_to_cpu(X) ((uint16_t)((((uint16_t)(X) & 0xff00) >> 8) | \
		                      (((uint16_t)(X) & 0x00ff) << 8)))
  #define cpu_to_le16(X) le16_to_cpu(X)
#else
  #error "Endian not specified !"
#endif

static inline unsigned min(unsigned a, unsigned b)
{
	return a < b ? a : b;
}

#endif

#include <netcope/ndp.h>

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
	int ret;
	do {
		errno = 0;
		ret = ioctl(q->fd, NDP_IOC_STOP, &q->sync);
	} while (ret == -1 && errno == EAGAIN);

	if (ret)
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

#ifdef __KERNEL__
int ndp_queue_open_init(struct nfb_device *dev, struct ndp_queue *q, unsigned index, int type)
{
	return nc_ndp_queue_open_init(dev, q, index, type);
}
#else
static struct ndp_queue *ndp_open_queue(struct nfb_device *dev, unsigned index, int dir, ndp_open_flags_t flags)
{
	int ret;
	int fdt_offset;
	int32_t numa;
	struct ndp_queue *q;

	fdt_offset = nfb_fdt_queue_offset(dev, index, dir);
	if (fdt_getprop32(nfb_get_fdt(dev), fdt_offset, "numa", &numa)) {
		numa = -1;
	}

	/* Create queue structure */
	q = nalloc(numa, sizeof(*q));
	if (q == NULL) {
		errno = ENOMEM;
		goto err_nalloc;
	}

	q->numa = numa;
	if ((ret = nc_ndp_queue_open_init_ext(dev, q, index, dir, flags))) {
		errno = ret;
		goto err_ndp_queue_open_init;
	}

	return q;

err_ndp_queue_open_init:
	nfree(q->numa, q, sizeof(*q));
err_nalloc:
	return NULL;
}

struct ndp_queue *ndp_open_rx_queue_ext(struct nfb_device *dev, unsigned index, ndp_open_flags_t flags)
{
	return ndp_open_queue(dev, index, NDP_CHANNEL_TYPE_RX, flags);
}

struct ndp_queue *ndp_open_rx_queue(struct nfb_device *dev, unsigned index)
{
	return ndp_open_rx_queue_ext(dev, index, 0);
}

struct ndp_queue *ndp_open_tx_queue_ext(struct nfb_device *dev, unsigned index, ndp_open_flags_t flags)
{
	return ndp_open_queue(dev, index, NDP_CHANNEL_TYPE_TX, flags);
}

struct ndp_queue *ndp_open_tx_queue(struct nfb_device *dev, unsigned index)
{
	return ndp_open_tx_queue_ext(dev, index, 0);
}
#endif

void ndp_close_queue(struct ndp_queue *q)
{
	if (q->status == NDP_QUEUE_RUNNING)
		ndp_queue_stop(q);
#ifdef __KERNEL__
	if (q->sub) {
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

int ndp_queue_is_available(const struct nfb_device *dev, unsigned index, int dir)
{
	int fdt_offset;
	size_t prop;
	char path[64];

	const char *dir_str = dir ? "tx" : "rx";
	const void *fdt = nfb_get_fdt(dev);

	snprintf(path, 64, "/drivers/ndp/%s_queues/%s%u", dir_str, dir_str, index);

	/* Parse base values from FDT */
	fdt_offset = fdt_path_offset(fdt, path);
	if (fdt_offset < 0)
		return 0;

	/* Get mmap size */
	if (fdt_getprop64(fdt, fdt_offset, "mmap_size", &prop))
		return 0;
	return prop == 0 ? 0 : 1;
}

int ndp_rx_queue_is_available(const struct nfb_device *dev, unsigned index)
{
	return ndp_queue_is_available(dev, index, NDP_CHANNEL_TYPE_RX);
}

int ndp_tx_queue_is_available(const struct nfb_device *dev, unsigned index)
{
	return ndp_queue_is_available(dev, index, NDP_CHANNEL_TYPE_TX);
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

#ifndef __KERNEL__

#define NDP_TX_BURST_COPY_ATTEMPTS 1000

unsigned ndp_tx_burst_copy(ndp_tx_queue_t *queue, struct ndp_packet *packets, unsigned count)
{
	struct ndp_packet *our_packets;
	unsigned our_burst_count;
	unsigned packets_sent = 0;
	unsigned attempts = 0;

	our_packets = malloc(sizeof(our_packets[0]) * count);
	if (!our_packets) {
		return 0; /* -ENOMEM */
	}

	for (unsigned i = 0; i < count; i++) {
		our_packets[i].header_length = 0;
		our_packets[i].data_length = packets[i].data_length;
	}

	while (packets_sent < count && attempts < NDP_TX_BURST_COPY_ATTEMPTS) {
		our_burst_count = ndp_tx_burst_get(queue, our_packets + packets_sent, count - packets_sent);

		for (unsigned i = 0; i < our_burst_count; i++) {
			memcpy(our_packets[packets_sent + i].data, packets[packets_sent + i].data,
					our_packets[packets_sent + i].data_length);
		}

		ndp_tx_burst_put(queue);

		packets_sent += our_burst_count;
		attempts++;
	}

	free(our_packets);

	return packets_sent;
}

int ndp_rx_poll(struct nfb_device *dev, int timeout, struct ndp_queue **q)
{
	int i;
	int ret;
	struct pollfd pfd;
	size_t size, max_size = 0;

	for (i = 0; i < dev->queue_count; i++) {
		if (dev->queues[i] == NULL)
			continue;
		if (dev->queues[i]->version == 2) {
			/* TODO */
		} else if (dev->queues[i]->version == 1) {
			nc_ndp_v1_rx_unlock(dev->queues[i]);
		}
	}

	pfd.fd = dev->fd;
	pfd.events = POLLIN;

	ret = poll(&pfd, 1, timeout);
	if (ret <= 0) {
		if (ret < 0) {
			ret = -errno;
		}
		goto err;
	}
	if (pfd.revents & POLLERR) {
		ret = -1;
		goto err;
	}

	ret = 0;
	if (q) {
		/* Find queue with biggest amount of data */
		for (i = 0; i < dev->queue_count; i++) {
			if (dev->queues[i] == NULL)
				continue;
			size = 0;
			if (dev->queues[i]->version == 2) {
				/* TODO */
			} else if (dev->queues[i]->version == 1) {
				nc_ndp_v1_rx_lock(dev->queues[i]);
				size = dev->queues[i]->u.v1.bytes;
			}
			if (size)
				ret++;
			if (size > max_size) {
				*q = dev->queues[i];
				max_size = size;
			}
		}
	}

	return ret;
err:
	return ret;
}
#endif
