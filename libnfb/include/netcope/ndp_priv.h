/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - private definitions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <nfb/ndp.h>

#ifndef _NC_NDP_PRIV_H_
#define _NC_NDP_PRIV_H_

enum ndp_queue_status {
	NDP_QUEUE_RUNNING,
	NDP_QUEUE_STOPPED,
};

struct ndp_packethdr {
	__u16	packet_size;	/**< size of whole packet (header incl.) */
	__u16	header_size;	/**< size of hw data (optional) */
} __attribute((packed));

struct ndp_v2_packethdr {
	__u16	packet_size;	/**< size of whole packet */
	__u8	header_size;	/**< size of hw data (optional) */
	__u8	flags;			/**< flags */
} __attribute((packed));

struct ndp_v2_offsethdr {
	__u64	offset;
};

struct ndp_queue
{
	uint32_t version;
	uint32_t flags;

	void *buffer;
	unsigned long long size;

	union {
		struct {
			unsigned char *data;
			unsigned long long bytes;
			unsigned long long total;

			uint64_t swptr;
		} v1;

		struct {
			unsigned pkts_available;
			unsigned rhp;
			unsigned hdr_items;

			struct ndp_v2_packethdr *hdr;
			struct ndp_v2_offsethdr *off;
		} v2;
	} u;

	int fd;
	struct ndp_subscription_sync sync;

	struct nfb_device *dev;
	struct ndp_channel_request channel;
	enum ndp_queue_status status;
	uint32_t frame_size_min;
	uint32_t frame_size_max;

	int numa;
	int type;
	int index;
#ifdef __KERNEL__
	int alloc;
	struct ndp_subscription *sub;
	struct ndp_subscriber *subscriber;
#else
#endif
	struct ndp_queue_ops ops;
	void *priv;
};

#endif
