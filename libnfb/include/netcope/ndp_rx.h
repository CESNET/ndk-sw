/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - receive functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.cz>
 */

static inline int nc_ndp_v1_rx_lock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	int ret;

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}

	q->u.v1.data = (unsigned char *)q->buffer + q->sync.swptr + q->u.v1.swptr;
	q->u.v1.bytes = (q->sync.hwptr - q->sync.swptr - q->u.v1.swptr) & (q->size - 1);
	q->u.v1.total = (q->sync.hwptr - q->sync.swptr) & (q->size - 1);

	return 0;
}

static inline int nc_ndp_v1_rx_unlock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

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

static inline unsigned nc_ndp_v1_rx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

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
			ndp_close_queue(q->q);
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
			ndp_close_queue(q->q);
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

static inline int nc_ndp_v1_rx_burst_put(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	/* Unlock when 1/2 of locked bytes readen */
	if (q->u.v1.total - q->u.v1.bytes > q->size / 2) {
		nc_ndp_v1_rx_unlock(q);
	}
	return 0;
}

static inline unsigned nc_ndp_v2_rx_lock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

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

static inline int nc_ndp_v2_rx_unlock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	int ret;
	q->sync.swptr = q->u.v2.rhp & (q->u.v2.hdr_items-1);

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}
	return 0;
}

static inline unsigned nc_ndp_v2_rx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

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

static inline int nc_ndp_v2_rx_burst_put(void *priv)
{
	return nc_ndp_v2_rx_unlock(priv);
}

static inline void _ndp_queue_rx_sync_v3_us(struct nc_ndp_queue *q)
{
#ifndef __KERNEL__
	struct ndp_v3_packethdr *hdr_base;

	if (q->sync.swptr != q->u.v3.uspace_shp) {
#if 0
		q->u.v3.uspace_shp = q->sync.swptr & q->u.v3.hdr_ptr_mask;
		nfb_comp_write64(q->u.v3.comp, NDP_CTRL_REG_SDP, q->u.v3.sdp | (((uint64_t) q->u.v3.uspace_shp) << 32));
#else
		unsigned i;
		unsigned count_blks = 0;
		unsigned count = (q->sync.swptr - q->u.v3.uspace_shp) & q->u.v3.uspace_mhp;

		hdr_base = q->u.v3.hdrs + q->u.v3.uspace_shp;
		for (i = 0; i < count; i++) {
			count_blks += (hdr_base[i].frame_len + NDP_RX_CALYPTE_BLOCK_SIZE - 1) / NDP_RX_CALYPTE_BLOCK_SIZE;
		}

		q->u.v3.uspace_shp = q->sync.swptr;
		q->u.v3.uspace_sdp = (q->u.v3.uspace_sdp + count_blks) & q->u.v3.uspace_mdp;
		q->u.v3.uspace_acc += count;

		/* TODO: magic number */
		if (q->u.v3.uspace_acc >= 32) {
			q->u.v3.uspace_acc = 0;
			////nc_ndp_ctrl_sp_flush(q->u.v3.comp);
			nfb_comp_write64(q->u.v3.comp, NDP_CTRL_REG_SDP, q->u.v3.uspace_sdp | (((uint64_t) q->u.v3.uspace_shp) << 32));
		}
#endif
	}

	do {
		hdr_base = q->u.v3.hdrs + q->u.v3.uspace_hhp;
		if (hdr_base->valid == 0)
			break;
		q->u.v3.uspace_hhp = (q->u.v3.uspace_hhp + 1) & q->u.v3.uspace_mhp;
	} while (1);
	q->sync.hwptr = q->u.v3.uspace_hhp;
#endif
}

static inline unsigned nc_ndp_v3_rx_lock(void *priv)
{
	int ret = 0;
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->flags & NDP_CHANNEL_FLAG_USERSPACE) {
		_ndp_queue_rx_sync_v3_us(q);
	} else {
		if ((ret = _ndp_queue_sync(q, &q->sync))) {
			return ret;
		}
	}

	q->u.v3.pkts_available = (q->sync.hwptr - q->u.v3.shp) & (q->u.v3.hdr_ptr_mask);

	return ret;
}

static inline int nc_ndp_v3_rx_unlock(void *priv)
{
	int ret = 0;
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	q->sync.swptr = q->u.v3.shp & (q->u.v3.hdr_ptr_mask);
	if (q->flags & NDP_CHANNEL_FLAG_USERSPACE) {
		_ndp_queue_rx_sync_v3_us(q);
	} else {
		ret = _ndp_queue_sync(q, &q->sync);
	}
	return ret;
}

static inline unsigned nc_ndp_v3_rx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	unsigned i = 0;
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	struct ndp_v3_packethdr *hdr_base;
	unsigned char *data_base;

	if (unlikely(q->u.v3.pkts_available < count)) {
		nc_ndp_v3_rx_lock(q);
		count = min(q->u.v3.pkts_available, count);
		if (count == 0)
			return 0;
	}

	hdr_base = q->u.v3.hdrs + q->u.v3.shp;
	data_base = q->buffer;
	__builtin_prefetch(hdr_base);
	__builtin_prefetch(data_base);

	for (i = 0; i < count; i++) {
		unsigned packet_size;
		unsigned header_size = 0;
		struct ndp_v3_packethdr *hdr;
		unsigned char *data;

		hdr = hdr_base + i;

		if (!hdr->valid) {
			break;
		}

		header_size = hdr->metadata & NDP_CALYPTE_METADATA_HDR_SIZE_MASK;
		packet_size = le16_to_cpu(hdr->frame_len) - header_size;

		data = data_base + hdr->frame_ptr * NDP_RX_CALYPTE_BLOCK_SIZE;
		/* Assign pointer and length of header */
		packets[i].header = data;
		packets[i].header_length = header_size;

		data += header_size;

		/* Assign pointer and length of data */
		packets[i].data = data;
		packets[i].data_length = packet_size;

		hdr->valid = 0;
		q->u.v3.sdp += (hdr->frame_len + NDP_RX_CALYPTE_BLOCK_SIZE - 1) / NDP_RX_CALYPTE_BLOCK_SIZE;
	}

	q->u.v3.sdp &= q->u.v3.data_ptr_mask;
	q->u.v3.shp = (q->u.v3.shp + count) & q->u.v3.hdr_ptr_mask;
	q->u.v3.pkts_available -= count;

	return count;
}

static inline int nc_ndp_v3_rx_burst_put(void *priv)
{
	return nc_ndp_v3_rx_unlock(priv);
}

static inline unsigned nc_ndp_rx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->protocol == 3) {
		return nc_ndp_v3_rx_burst_get(priv, packets, count);
	} else if (q->protocol == 2) {
		return nc_ndp_v2_rx_burst_get(priv, packets, count);
	} else if (q->protocol == 1) {
		return nc_ndp_v1_rx_burst_get(priv, packets, count);
	}
	return 0;
}

static inline void nc_ndp_rx_burst_put(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->protocol == 3) {
		nc_ndp_v3_rx_unlock(priv);
	} else if (q->protocol == 2) {
		nc_ndp_v2_rx_unlock(priv);
	} else if (q->protocol == 1) {
		nc_ndp_v1_rx_burst_put(priv);
	}
}
