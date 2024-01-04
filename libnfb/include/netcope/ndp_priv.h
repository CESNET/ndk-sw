/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - private definitions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.cz>
 */

#include <nfb/ndp.h>

#ifndef _NC_NDP_PRIV_H_
#define _NC_NDP_PRIV_H_

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

struct ndp_v3_packethdr {
	__u16 frame_len;        /**< size of the packet */
	__u16 frame_ptr;        /**< index into the data array */
	__u8 valid : 1;         /**< bit indicating the validity of the header/packet */
	unsigned reserved : 7;  /**< bits reserved for future use */
	unsigned metadata:24;   /**< user metadata */
} __attribute((packed));

struct ndp_queue;

struct nc_ndp_queue {
	/* Data path */
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

		struct {
			// DMA Calypte data and header buffer
			struct nfb_comp *tx_hdr_buff;
			struct nfb_comp *tx_data_buff;

			// Used to indicate the number of packets that are
			// locked in the queue. It also means the number of free
			// headers.
			uint32_t pkts_available;
			uint32_t pkts_to_send;
			uint64_t bytes_available;

			uint32_t sdp;
			uint32_t shp;
			uint32_t data_ptr_mask;
			uint32_t hdr_ptr_mask;

			// Packet descriptions
			struct ndp_packet *packets;
			// Buffer for headers
			struct ndp_v3_packethdr *hdrs;
#ifndef __KERNEL__
			struct ndp_v3_packethdr *uspace_hdrs;
			struct nfb_comp *comp;
			uint32_t uspace_shp;
			uint32_t uspace_hhp;
			uint32_t uspace_sdp;
			uint32_t uspace_hdp;
			uint32_t uspace_mhp;
			uint32_t uspace_mdp;
			uint32_t uspace_free;
			uint32_t uspace_acc;
#endif
		} v3;
	} u;

	int fd;
	struct ndp_subscription_sync sync;

	uint32_t frame_size_min;
	uint32_t frame_size_max;

#ifdef __KERNEL__
	struct ndp_subscription *sub;
#endif

	/* Control path */
	struct ndp_queue *q;
	struct nfb_device *dev;
	uint32_t protocol;
	uint32_t flags;

	struct ndp_channel_request channel;
#ifdef __KERNEL__
	struct ndp_subscriber *subscriber;
#endif
};

#endif
