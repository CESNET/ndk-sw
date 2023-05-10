/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * data transmission module - base implementation using kernel NDP buffers
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#ifdef __KERNEL__
#else
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

inline int _ndp_queue_sync(struct nc_ndp_queue *q, struct ndp_subscription_sync *sync)
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

inline int _ndp_queue_start(struct nc_ndp_queue *q)
{
#ifdef __KERNEL__
	return ndp_subscription_start(ndp_subscription_by_id(q->subscriber, q->sync.id), &q->sync);
#else
	if (ioctl(q->fd, NDP_IOC_START, &q->sync))
		return errno;
	return 0;
#endif
}

inline int _ndp_queue_stop(struct nc_ndp_queue *q)
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

int ndp_base_queue_open(struct nfb_device *dev, void *dev_priv, unsigned index, int dir, ndp_open_flags_t flags, struct ndp_queue ** pq)
{
	int ret;
	int fdt_offset;
	int32_t numa;
	struct nc_ndp_queue *q_nc;
	struct ndp_queue *q;
#ifdef __KERNEL__
	struct ndp * ndp;
#endif
	//struct nfb_base_priv *dev_base = dev_priv;

	(void) dev_priv;

	fdt_offset = nc_nfb_fdt_queue_offset(nfb_get_fdt(dev), index, dir);
	if (fdt_getprop32(nfb_get_fdt(dev), fdt_offset, "numa", &numa)) {
		numa = -1;
	}

	q = ndp_queue_create(dev, numa, dir, index);
	if (q == NULL) {
		ret = -ENOMEM;
		goto err_nalloc;
	}

	q_nc = nfb_nalloc(numa, sizeof(struct nc_ndp_queue));
	if (q_nc == NULL) {
		ret = -ENOMEM;
		goto err_nc_ndp_queue_alloc;
	}

	q_nc->q = q;
#ifdef __KERNEL__
	ndp = (struct ndp *) dev->list_drivers[NFB_DRIVER_NDP].priv;
	q_nc->subscriber = ndp_subscriber_create(ndp);
	if (!q_nc->subscriber) {
		ret = -ENOMEM;
		goto err_subscriber_create;
	}
#else
	q_nc->fd = dev->fd;
#endif

	ndp_queue_set_priv(q, q_nc);
	if ((ret = nc_ndp_queue_open_init_ext(nfb_get_fdt(dev), q_nc, index, dir, flags))) {
		goto err_open;
	}

	*pq = q;
	return 0;

err_open:
#ifdef __KERNEL__
	ndp_subscriber_destroy(q_nc->subscriber);
err_subscriber_create:
#endif
	nfb_nfree(numa, q_nc, sizeof(struct nc_ndp_queue));
err_nc_ndp_queue_alloc:
	ndp_queue_destroy(q);
err_nalloc:
	return ret;
}

void ndp_base_queue_close(void *priv)
{
	struct nc_ndp_queue *q = priv;
	struct ndp_queue *ndp_q = q->q;

	nc_ndp_queue_close(q);
#ifdef __KERNEL__
	ndp_subscriber_destroy(q->subscriber);
#endif

	nfb_nfree(ndp_queue_get_numa_node(ndp_q), q, sizeof(struct nc_ndp_queue));
	ndp_queue_destroy(ndp_q);
}
