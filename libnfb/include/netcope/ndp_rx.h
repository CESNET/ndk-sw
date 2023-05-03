/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - receive functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

static inline int nc_ndp_v1_rx_lock(struct ndp_queue *q)
{
	int ret;

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}

	q->u.v1.data = (unsigned char *)q->buffer + q->sync.swptr + q->u.v1.swptr;
	q->u.v1.bytes = (q->sync.hwptr - q->sync.swptr - q->u.v1.swptr) & (q->size - 1);
	q->u.v1.total = (q->sync.hwptr - q->sync.swptr) & (q->size - 1);

	return 0;
}

static inline int nc_ndp_v1_rx_unlock(struct ndp_queue *q)
{
	int ret;
	unsigned long long unlock_bytes = (q->u.v1.total - q->u.v1.bytes);

	q->sync.swptr = (q->sync.swptr + unlock_bytes) & (q->size - 1);
	q->u.v1.total -= unlock_bytes;
	q->u.v1.swptr = 0;

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}

	return 0;
}

static inline unsigned nc_ndp_v1_rx_burst_get(struct ndp_queue *q, struct ndp_packet *packets, unsigned count)
{
	uint16_t packet_size;
	uint16_t header_size;
	unsigned cnt = 0;

	unsigned long long bytes;
	unsigned char *data;
	uint64_t swptr;

	bytes = q->u.v1.bytes;
	swptr = q->u.v1.swptr;
	data = q->u.v1.data;

	__builtin_prefetch(data);

	while (count--) {
		/* try lock when no data available */
		if (unlikely(bytes == 0)) {
			q->u.v1.data  = data;
			q->u.v1.swptr = swptr;
			q->u.v1.bytes = bytes;

			if (nc_ndp_v1_rx_lock(q))
				return 0;

			bytes = q->u.v1.bytes;
			swptr = q->u.v1.swptr;
			data = q->u.v1.data;

			if (bytes == 0) {
				return cnt;
			}
		}

		packet_size = le16_to_cpu(((struct ndp_packethdr*)data)->packet_size);
		header_size = le16_to_cpu(((struct ndp_packethdr*)data)->header_size);

		/* prefetch next packet header */
		__builtin_prefetch(data + packet_size, 0, 3);

		/* check packet header */
		if (unlikely(packet_size == 0 || header_size > packet_size - NDP_PACKET_HEADER_SIZE)) {
			nc_ndp_queue_stop(q);
			ndp_close_queue(q);
#ifdef __KERNEL__
                        printk(KERN_ERR "%s: NDP packet header malformed %d\n", __func__, packet_size);
			return 0;
#else
			errx(5, "NDP packet header malformed %d", packet_size);
#endif
		}
		/* check locked space */
		if (unlikely(packet_size > bytes)) {
			nc_ndp_queue_stop(q);
			ndp_close_queue(q);
#ifdef __KERNEL__
			return 0;
#else
			errx(15, "NDP sync error");
#endif
		}

		packets->flags = 0;

		/* Assign pointer and length of data */
		packets->header = data + NDP_PACKET_HEADER_SIZE;
		packets->header_length = header_size;
		header_size = ALIGN(header_size + NDP_PACKET_HEADER_SIZE, 8);

		/* Assign pointer and length of header */
		packets->data = data + header_size;
		packets->data_length = packet_size - header_size;
		packet_size = ALIGN(packet_size, 8);

		/* Move pointers */
		data  += packet_size;
		swptr += packet_size;
		bytes -= packet_size;
		cnt++;
		packets++;
	}
	q->u.v1.data  = data;
	q->u.v1.swptr = swptr;
	q->u.v1.bytes = bytes;
	return cnt;
}

static inline unsigned nc_ndp_v2_rx_lock(struct ndp_queue *q)
{
	int ret;
	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}

	q->u.v2.pkts_available = (q->sync.hwptr - q->u.v2.rhp) & (q->u.v2.hdr_items-1);
	/* TODO: start offset! */
	if (q->u.v2.rhp >= q->u.v2.hdr_items) {
		q->u.v2.rhp -= q->u.v2.hdr_items;
	}
	return 0;
}

static inline int nc_ndp_v2_rx_unlock(struct ndp_queue *q)
{
	int ret;
	q->sync.swptr = q->u.v2.rhp & (q->u.v2.hdr_items-1);

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}
	return 0;
}

static inline unsigned nc_ndp_v2_rx_burst_get(struct ndp_queue *q, struct ndp_packet *packets, unsigned count)
{
	unsigned i;
	unsigned char *data_base = q->buffer;
	struct ndp_v2_packethdr *hdr_base;
	struct ndp_v2_offsethdr *off_base;

	if (unlikely(q->u.v2.pkts_available < count)) {
		nc_ndp_v2_rx_lock(q);
		count = min(q->u.v2.pkts_available, count);
		if (count == 0)
			return 0;
	}

	hdr_base = q->u.v2.hdr + q->u.v2.rhp;
	off_base = q->u.v2.off + q->u.v2.rhp;
	__builtin_prefetch(hdr_base);
	__builtin_prefetch(off_base);

	for (i = 0; i < count; i++) {
		unsigned packet_size;
		unsigned header_size;
		struct ndp_v2_packethdr *hdr;
		struct ndp_v2_offsethdr *off;

		hdr = hdr_base + i;
		off = off_base + i;

		packet_size = le16_to_cpu(hdr->packet_size);
		header_size = hdr->header_size;

		/* Assign pointer and length of data */
		packets[i].header = data_base + off->offset;
		packets[i].header_length = header_size;
		packets[i].flags = hdr->flags & 0xF;

		//header_size = ALIGN(header_size + NDP_PACKET_HEADER_SIZE, 8);

		/* Assign pointer and length of header */
		packets[i].data = data_base + off->offset + header_size;
		packets[i].data_length = packet_size - header_size;
	}

	q->u.v2.rhp += count;
	q->u.v2.pkts_available -= count;

	return count;
}

static inline unsigned nc_ndp_rx_burst_get(struct ndp_queue *q, struct ndp_packet *packets, unsigned count)
{
	if (q->version == 2) {
		return nc_ndp_v2_rx_burst_get(q, packets, count);
	} else if (q->version == 1) {
		return nc_ndp_v1_rx_burst_get(q, packets, count);
	}
	return 0;
}

static inline void nc_ndp_rx_burst_put(struct ndp_queue *q)
{
	if (q->version == 2) {
		nc_ndp_v2_rx_unlock(q);
	} else if (q->version == 1) {
		/* Unlock when 1/2 of locked bytes readen */
		if (q->u.v1.total - q->u.v1.bytes > q->size / 2) {
			nc_ndp_v1_rx_unlock(q);
		}
	}
}
