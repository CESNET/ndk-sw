/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - common functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <nfb/ndp.h>
#include "ndp_priv.h"

#ifndef _NC_NDP_H_
#define _NC_NDP_H_

void ndp_close_queue(struct ndp_queue *q);

int _ndp_queue_sync(struct nc_ndp_queue *q, struct ndp_subscription_sync *sync);
int _ndp_queue_start(struct nc_ndp_queue *q);
int _ndp_queue_stop(struct nc_ndp_queue *q);

static int nc_ndp_queue_start(void *priv);
static int nc_ndp_queue_stop(void *priv);

#include "dma_ctrl_ndp.h"
#include "ndp_rx.h"
#include "ndp_tx.h"

static inline int nc_nfb_fdt_queue_offset(const void *fdt, unsigned index, int type)
{
	const char * dir_str = type ? "tx" : "rx";
	char path[64];

	index &= 0x0FFFFFFF;

	snprintf(path, 64, "/drivers/ndp/%s_queues/%s%u", dir_str, dir_str, index);

	return fdt_path_offset(fdt, path);
}

static inline int nc_ndp_v1_open_queue(struct nc_ndp_queue *q)
{
	struct ndp_queue_ops *ops = ndp_queue_get_ops(q->q);

	if (q->frame_size_min == 0)
		q->frame_size_min = 57;
	if (q->frame_size_max == 0)
		q->frame_size_max = 0x3FFF;

	q->u.v1.bytes = 0;
	q->u.v1.total = 0;
	q->u.v1.swptr = 0;

	q->u.v1.data = q->buffer;

	if (q->channel.type == NDP_CHANNEL_TYPE_RX) {
		ops->burst.rx.get = nc_ndp_v1_rx_burst_get;
		ops->burst.rx.put = nc_ndp_v1_rx_burst_put;
	} else {
		ops->burst.tx.get = nc_ndp_v1_tx_burst_get;
		ops->burst.tx.put = nc_ndp_v1_tx_burst_put;
		ops->burst.tx.flush = nc_ndp_v1_tx_burst_flush;
	}

	return 0;
}

static inline int nc_ndp_v2_open_queue(struct nc_ndp_queue *q, const void *fdt, int fdt_offset)
{
	int ret = 0;
#ifndef __KERNEL__
	int prot;
	size_t hdr_mmap_size = 0;
	size_t off_mmap_size = 0;
	off_t hdr_mmap_offset = 0;
	off_t off_mmap_offset = 0;
#endif
	struct ndp_queue_ops *ops = ndp_queue_get_ops(q->q);

	/* 4096 is the default value from older version of driver. Never version have buffer_size prop in DT */
	uint32_t buffer_size = 4096;

	if (q->frame_size_min == 0)
		q->frame_size_min = 60;
	if (q->frame_size_max == 0)
		q->frame_size_max = 0x3FFF;

	fdt_getprop32(fdt, fdt_offset, "buffer_size", &buffer_size);
	if (buffer_size < q->frame_size_max)
		q->frame_size_max = buffer_size;

	q->u.v2.rhp = 0;
	q->u.v2.pkts_available = 0;

#ifdef __KERNEL__
	q->u.v2.hdr_items = ndp_ctrl_v2_get_vmaps(q->sub->channel, (void**)&q->u.v2.hdr, (void**)&q->u.v2.off);
#else
	ret |= fdt_getprop64(fdt, fdt_offset, "hdr_mmap_size", &hdr_mmap_size);
	ret |= fdt_getprop64(fdt, fdt_offset, "off_mmap_size", &off_mmap_size);
	ret |= fdt_getprop64(fdt, fdt_offset, "hdr_mmap_base", &hdr_mmap_offset);
	ret |= fdt_getprop64(fdt, fdt_offset, "off_mmap_base", &off_mmap_offset);
	if (ret) {
		ret = EBADFD;
		goto err_fdt_getprop;
	}

	prot = PROT_READ;
	prot |= q->channel.type == NDP_CHANNEL_TYPE_TX ? PROT_WRITE : 0;

#endif
	if (q->channel.type == NDP_CHANNEL_TYPE_RX) {
		ops->burst.rx.get = nc_ndp_v2_rx_burst_get;
		ops->burst.rx.put = nc_ndp_v2_rx_burst_put;
	} else {
		ops->burst.tx.get = nc_ndp_v2_tx_burst_get;
		ops->burst.tx.put = nc_ndp_v2_tx_burst_put;
		ops->burst.tx.flush = nc_ndp_v2_tx_burst_flush;
	}

#ifndef __KERNEL__
	q->u.v2.hdr = mmap(NULL, hdr_mmap_size, prot, MAP_FILE | MAP_SHARED, q->fd, hdr_mmap_offset);
	if (q->u.v2.hdr == MAP_FAILED) {
		goto err_mmap_hdr;
	}

	q->u.v2.off = mmap(NULL, off_mmap_size, prot, MAP_FILE | MAP_SHARED, q->fd, off_mmap_offset);
	if (q->u.v2.off == MAP_FAILED) {
		goto err_mmap_off;
	}

	q->u.v2.hdr_items = hdr_mmap_size / 2 / sizeof(struct ndp_v2_packethdr);
#endif
	return 0;

#ifndef __KERNEL__
err_mmap_off:
	munmap(q->u.v2.hdr, hdr_mmap_size);
err_mmap_hdr:
err_fdt_getprop:
#endif
	return ret;
}

static inline int nc_ndp_queue_open_init_ext(const void *fdt, struct nc_ndp_queue *q, unsigned index, int type, ndp_open_flags_t ndp_flags)
{
	int ret = 0;
	int fdt_offset;
	int ctrl_offset;
	int ctrl_params_offset;
	int flags = 0;

	off_t mmap_offset;
	size_t mmap_size;

	struct ndp_queue_ops *ops = ndp_queue_get_ops(q->q);

	(void) ndp_flags;

	fdt_offset = nc_nfb_fdt_queue_offset(fdt, index, type);

	/* Fetch controller parameters */
	q->frame_size_min = q->frame_size_max = 0;
	ctrl_offset = fdt_node_offset_by_phandle_ref(fdt, fdt_offset, "ctrl");
	ctrl_params_offset = fdt_node_offset_by_phandle_ref(fdt, ctrl_offset, "params");
	fdt_getprop32(fdt, ctrl_params_offset, "frame_size_min", &q->frame_size_min);
	fdt_getprop32(fdt, ctrl_params_offset, "frame_size_max", &q->frame_size_max);

	ret |= fdt_getprop64(fdt, fdt_offset, "size", &q->size);
	ret |= fdt_getprop64(fdt, fdt_offset, "mmap_size", &mmap_size);
	ret |= fdt_getprop64(fdt, fdt_offset, "mmap_base", &mmap_offset);
	ret |= fdt_getprop32(fdt, fdt_offset, "protocol", &q->protocol);

	if (ret) {
		ret = EBADFD;
		goto err_fdt_getprop;
	}

	if (mmap_size == 0) {
		ret = ENOMEM;
		goto err_zero_mmap_size;
	}

	if (q->protocol == 2) {
		flags |= NDP_CHANNEL_FLAG_USE_HEADER | NDP_CHANNEL_FLAG_USE_OFFSET;
	}

	q->flags = flags;

	q->channel.index = index;
	q->channel.type = type;
	q->channel.flags = q->flags;

#ifdef __KERNEL__
	/* create subscription */
	q->sub = ndp_subscription_create(q->subscriber, &q->channel);
	if (IS_ERR(q->sub)) {
		ret = PTR_ERR(q->sub);
		printk(KERN_ERR "%s: failed to create subscription\n", __func__);
		goto err_subscribe;
	}

	q->buffer = q->sub->channel->ring.vmap;
#else

	if ((ret = ioctl(q->fd, NDP_IOC_SUBSCRIBE, &q->channel))) {
		goto err_subscribe;
	}

	/* Map the memory of buffer space. Driver ensures that the mmap_size
	 * is 2 times larger than q->size and the buffer space is shadowed.
	 * Due to this feature user normally needs not to check buffer boundary. */
	q->buffer = mmap(NULL, q->size * 2, PROT_READ | (type ? PROT_WRITE : 0),
			MAP_FILE | MAP_SHARED, q->fd, mmap_offset);
	if (q->buffer == MAP_FAILED) {
		goto err_mmap;
	}
#endif
	q->sync.id = q->channel.id;

	q->sync.swptr = 0;
	q->sync.hwptr = 0;

	if (q->protocol == 2) {
		ret = nc_ndp_v2_open_queue(q, fdt, fdt_offset);
	} else if (q->protocol == 1) {
		ret = nc_ndp_v1_open_queue(q);
	}

	ops->control.start = nc_ndp_queue_start;
	ops->control.stop = nc_ndp_queue_stop;

	if (ret)
		goto err_vx_open_queue;

	return 0;

err_vx_open_queue:
#ifndef __KERNEL__
	munmap(q->buffer, q->size * 2);
err_mmap:
#endif
err_subscribe:
err_zero_mmap_size:
err_fdt_getprop:
	return ret;
}

static inline int nc_ndp_queue_open_init(const void *fdt, struct nc_ndp_queue *q, unsigned index, int type)
{
	return nc_ndp_queue_open_init_ext(fdt, q, index, type, 0);
}

static inline void nc_ndp_queue_close(struct nc_ndp_queue *q)
{
#ifdef __KERNEL__
	if (q->sub) {
		ndp_subscription_destroy(q->sub);
		q->sub = NULL;
	}

#else
	munmap(q->buffer, q->size * 2);
#endif
}

static int nc_ndp_queue_start(void *priv)
{
	struct nc_ndp_queue *q = priv;
	int ret;

	q->sync.flags = 0;

	if ((ret = _ndp_queue_start(q)))
		return ret;

	if (q->channel.type == NDP_CHANNEL_TYPE_RX && q->protocol == 2 && (q->flags & NDP_CHANNEL_FLAG_EXCLUSIVE) == 0) {
		if ((ret = _ndp_queue_sync(q, &q->sync)))
			return ret;

		q->u.v2.rhp = q->sync.hwptr;
	}

	return ret;
}

static int nc_ndp_queue_stop(void *priv)
{
	struct nc_ndp_queue *q = priv;
	int ret;

	if ((ret = _ndp_queue_stop(q)))
		return ret;

	if (q->protocol == 1) {
		q->u.v1.bytes = 0;
	}
	return 0;
}

#endif
