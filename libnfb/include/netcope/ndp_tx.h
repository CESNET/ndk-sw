/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - transmit functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.cz>
 */

#define NDP_TX_BURST_COPY_ATTEMPTS 1000

/*
 * Before the IOCTL SYNC call without active lock:
 * - hwptr offset is invalid, but the value is used by driver to compute requesting size:
 *   requesting_size = swptr - hwptr
 * Before the IOCTL SYNC call with active lock:
 * - hwptr points to last byte of written data, which user want to publish to driver.
 * - swptr points to free space, which we requesting (want to lock).
 * - User can also give in the active lock with no data written (published):
 *   hwptr is same as hwptr received from last IOCTL SYNC call and swptr is equal to hwptr
 * After the IOCTL SYNC call:
 * - hwptr and swptr can generally differs from values before the IOCTL SYNC call.
 * - Size of returned lock can be equal to requesting_size or smaller.
 * - If hwptr equals swptr, we can't acquire a lock (there is no available space or
 *   there can be an other subscriber holding a lock).
 */

static inline int nc_ndp_v1_tx_lock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	int ret;
	/* Try to request whole buffer space. */
	q->sync.swptr = (q->sync.hwptr - 1) & (q->size - 1);

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}

	/*
	 * Data can be written to buffer at hwptr offset.
	 * However, we can also have some data written into active lock,
	 * but not yet published to driver (e.g. for flush optimizations):
	 * Increase the offset by the unpublished data amount stored in q->swptr.
	 */
	q->u.v1.data = (unsigned char *)q->buffer + q->sync.hwptr + q->u.v1.swptr;
	/* q->bytes stores free space amount (which excludes unpublished data amount). */
	q->u.v1.bytes = (q->sync.swptr - q->sync.hwptr - q->u.v1.swptr) & (q->size - 1);
	/* q->total stores the total size of current lock. Variable is currently not used. */
	q->u.v1.total = (q->sync.swptr - q->sync.hwptr) & (q->size - 1);

	return 0;
}

static inline int nc_ndp_v1_tx_unlock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	int ret;

	/* Publish all data, release lock (if any) */
	q->sync.swptr = q->sync.hwptr;
	q->u.v1.total = 0;
	q->u.v1.bytes = 0;
	q->u.v1.swptr = 0;

	if ((ret = _ndp_queue_sync(q, &q->sync))) {
		return ret;
	}

	return 0;
}

static inline unsigned nc_ndp_v1_tx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	uint16_t packet_size;
	uint16_t header_size;
	unsigned cnt = 0;
	size_t burst_size = 0;

	unsigned long long bytes;
	unsigned char *data;
	uint64_t swptr;

	const unsigned long long orig_bytes = q->u.v1.bytes;
	unsigned char *const orig_data = q->u.v1.data;
	const uint64_t orig_swptr = q->u.v1.swptr;

	bytes = q->u.v1.bytes;
	swptr = q->u.v1.swptr;
	data = q->u.v1.data;

	while (count--) {
		header_size = ALIGN(packets->header_length + NDP_PACKET_HEADER_SIZE, 8);
		packet_size = ALIGN(packets->data_length, 8) + header_size;

		__builtin_prefetch(data);

		/* If we don't have enough free space, try to lock some */
		if (unlikely(bytes < packet_size)) {
			q->u.v1.data  = data;
			q->u.v1.swptr = swptr;
			q->u.v1.bytes = bytes;

			nc_ndp_v1_tx_lock(q);

			bytes = q->u.v1.bytes;
			swptr = q->u.v1.swptr;
			data = q->u.v1.data;

			/* If we still don't have enough free space, return */
			if (bytes < packet_size) {
				/* rollback all */
				q->u.v1.data  = orig_data;
				q->u.v1.swptr = orig_swptr;
				q->u.v1.bytes = orig_bytes;
				return 0;
			}
		}

		/* Write NDP TX header */
		((struct ndp_packethdr*)data)->packet_size = cpu_to_le16(packets->data_length + header_size);
		((struct ndp_packethdr*)data)->header_size = cpu_to_le16(packets->header_length);

		/* Set pointers, where user can write packet content */
		packets->header = data + NDP_PACKET_HEADER_SIZE;
		packets->data   = data + header_size;
		packets++;

		/* Move pointers behind the end of packet, update free space */
		data  += packet_size;
		swptr += packet_size;
		bytes -= packet_size;

		burst_size += packet_size;
		cnt++;
	}
	q->u.v1.data  = data;
	q->u.v1.swptr = swptr;
	q->u.v1.bytes = bytes;
	return cnt;
}

static inline int nc_ndp_v1_tx_burst_put(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	/* Publish written data if their size is bigger than quarter of buffer size */
	if (q->u.v1.swptr > q->size / 4) {
		q->sync.hwptr = (q->sync.hwptr + q->u.v1.swptr) & (q->size-1);
		nc_ndp_v1_tx_unlock(q);
	}
	return 0;
}

static inline int nc_ndp_v1_tx_burst_flush(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	q->sync.hwptr = (q->sync.hwptr + q->u.v1.swptr) & (q->size-1);
	nc_ndp_v1_tx_unlock(q);
	return 0;
}

static inline void nc_ndp_v2_tx_lock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	signed offset;
	int lock_valid = q->sync.swptr == q->sync.hwptr ? 0 : 1;

	q->sync.swptr = (q->sync.hwptr - 1) & (q->u.v2.hdr_items-1);

	if (_ndp_queue_sync(q, &q->sync)) {
		return;
	}
	if (!lock_valid) {
		offset = q->sync.hwptr - q->u.v2.rhp;

		q->u.v2.rhp += offset;
		q->u.v2.hdr += offset;
		q->u.v2.off += offset;
	}
	q->u.v2.pkts_available = (q->sync.swptr - q->u.v2.rhp) & (q->u.v2.hdr_items-1);
}

static inline unsigned nc_ndp_v2_tx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	unsigned i;

	unsigned char *data_base;
	struct ndp_v2_packethdr *hdr_base;
	struct ndp_v2_offsethdr *off_base;


	__builtin_prefetch(q->u.v2.hdr);

	if (unlikely(q->u.v2.pkts_available < count)) {
		nc_ndp_v2_tx_lock(q);
		if (unlikely(q->u.v2.pkts_available < count || count == 0)) {
			return 0;
		}
	}

	data_base = q->buffer;
	hdr_base = q->u.v2.hdr;
	off_base = q->u.v2.off;

	for (i = 0; i < count; i++) {
		unsigned packet_size;
		unsigned header_size;

		struct ndp_v2_packethdr *hdr;
		struct ndp_v2_offsethdr *off;

		hdr = hdr_base + i;
		off = off_base + i;

		header_size = packets[i].header_length;
		packet_size = packets[i].data_length + header_size;

		if (unlikely(packet_size < q->frame_size_min)) {
			/* Enlarge packets smaller than min size & clean remaining data */
			memset(data_base + off->offset + header_size + packet_size, 0, q->frame_size_min - packet_size);
			packet_size = q->frame_size_min;
		} else if (unlikely(packet_size > q->frame_size_max)) {
			/* Can't handle packets larger than max size */
			return 0;
		}

		/* Write NDP TX header */
		hdr->packet_size = cpu_to_le16(packet_size);
		hdr->header_size = header_size;
		hdr->flags = packets[i].flags & 0xF;

		/* Set pointers, where user can write packet content */
		packets[i].header = data_base + off->offset;
		packets[i].data   = data_base + off->offset + header_size;

		/* Move pointers behind the end of packet, update free space */
	}
	q->u.v2.hdr += count;
	q->u.v2.off += count;
	q->u.v2.rhp += count;
	q->u.v2.pkts_available -= count;
	return count;
}

static inline int nc_ndp_v2_tx_burst_flush(void *priv);

static inline int nc_ndp_v2_tx_burst_put(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (((q->u.v2.rhp - q->sync.hwptr) & (q->u.v2.hdr_items-1)) > q->u.v2.hdr_items / 4) {
		nc_ndp_v2_tx_burst_flush(priv);
	}
	return 0;
}

static inline int nc_ndp_v2_tx_burst_flush(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->u.v2.rhp >= q->u.v2.hdr_items) {
		q->u.v2.rhp -= q->u.v2.hdr_items;
		q->u.v2.hdr -= q->u.v2.hdr_items;
		q->u.v2.off -= q->u.v2.hdr_items;
	}
	q->sync.hwptr = q->u.v2.rhp;
	q->sync.swptr = q->u.v2.rhp;
	q->u.v2.pkts_available = 0;

	if (_ndp_queue_sync(q, &q->sync)) {
		return -1;
	}
	return 0;
}

// Figure out, how many packets can be stored in a queue right now.
static inline void nc_ndp_v3_tx_lock(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	signed offset;
	int lock_valid = q->sync.swptr == q->sync.hwptr ? 0 : 1;

	// This assignment allows to determine the amount of free headers in the buffer
	q->sync.swptr = (q->sync.hwptr - 1) & (q->u.v3.hdr_ptr_mask);

	if (_ndp_queue_sync(q, &q->sync))
		return;

	if (!lock_valid) {
		offset = q->sync.hwptr - q->u.v3.shp;

		q->u.v3.shp  += offset;
		q->u.v3.hdrs += offset;
	}

	q->u.v3.pkts_available  = (q->sync.swptr - q->u.v3.shp) & (q->u.v3.hdr_ptr_mask);
	q->u.v3.bytes_available = q->sync.size;
}

static inline unsigned nc_ndp_v3_tx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	unsigned i;

	// Pointer of buffer bases
	unsigned char *data_base;
	struct ndp_v3_packethdr *hdr_base;

	// Iterator pointers
	uint32_t sdp_int;

	// All previously reserved packets need to be sent before new reservation takes place
	if (unlikely(q->u.v3.pkts_to_send != 0))
		return 0;

	__builtin_prefetch(q->u.v3.hdrs);

	if (unlikely(q->u.v3.pkts_available < count)) {
		// Figure out the current state of the queue in terms of its free space
		nc_ndp_v3_tx_lock(q);

		if (unlikely(q->u.v3.pkts_available < count || count == 0))
			return 0;
	}

	sdp_int = q->u.v3.sdp;

	data_base = q->buffer;
	hdr_base = q->u.v3.hdrs;

	for (i = 0; i < count; i++) {
		struct ndp_v3_packethdr *hdr;
		unsigned packet_size;
		unsigned header_size;

		hdr = hdr_base + i;

		header_size = packets[i].header_length;
		packet_size = packets[i].data_length + header_size;

		if (unlikely(packet_size < q->frame_size_min)) {
			/* Enlarge packets smaller than min size & clean remaining data */
			memset(data_base + sdp_int + header_size + packet_size, 0, q->frame_size_min - packet_size);
			packet_size = q->frame_size_min;
		} else if (unlikely(packet_size > q->frame_size_max)) {
			/* Can't handle packets larger than max size */
			return 0;
		}

		/* Write DMA TX header */
		hdr->metadata = 0;
		hdr->frame_len = cpu_to_le16(packet_size);
		hdr->frame_ptr = sdp_int & q->u.v3.data_ptr_mask;

		/* Set pointers, where user can write packet content */
		packets[i].header = data_base + sdp_int;
		packets[i].data = data_base + sdp_int + header_size;

		// Ceil SDP to the multiple of NDP_TX_CALYPTE_BLOCK_SIZE
		sdp_int = ((sdp_int + packet_size + (NDP_TX_CALYPTE_BLOCK_SIZE -1)) & (~(NDP_TX_CALYPTE_BLOCK_SIZE -1)));
	}

	// For pointer synchronization
	q->u.v3.hdrs           += count;
	q->u.v3.sdp             = sdp_int & q->u.v3.data_ptr_mask;
	q->u.v3.shp            += count;
	q->u.v3.pkts_available -= count;

	// For packet sending
	q->u.v3.packets = packets;
	q->u.v3.pkts_to_send += count;
	return count;
}

static inline int nc_ndp_v3_tx_burst_flush(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->u.v3.shp >= (q->u.v3.hdr_ptr_mask +1)) {
		q->u.v3.shp  -= (q->u.v3.hdr_ptr_mask + 1);
		q->u.v3.hdrs -= (q->u.v3.hdr_ptr_mask + 1);
	}

	// Synchronize pointers after all packets in a burst have been sent
	q->sync.swptr = q->u.v3.shp;
	q->sync.hwptr = q->u.v3.shp;
	q->u.v3.pkts_available = 0;

	if (_ndp_queue_sync(q, &q->sync))
		return -1;

	return 0;
}

static inline int nc_ndp_v3_tx_burst_put(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	unsigned i;
	uint32_t frame_len_ceil;
	struct ndp_packet *packet = q->u.v3.packets;
	struct ndp_v3_packethdr *hdr = q->u.v3.hdrs - q->u.v3.pkts_to_send;
	uint32_t shp = q->u.v3.shp - q->u.v3.pkts_to_send;


	for (i = 0; i < q->u.v3.pkts_to_send; i++) {

		frame_len_ceil = (hdr[i].frame_len + (NDP_TX_CALYPTE_BLOCK_SIZE -1)) & (~(NDP_TX_CALYPTE_BLOCK_SIZE -1));

		while (q->u.v3.bytes_available < frame_len_ceil) {
			q->sync.hwptr = shp;

			if (_ndp_queue_sync(q, &q->sync))
				return -1;

			q->u.v3.bytes_available = q->sync.size;
		}

		nfb_comp_write(q->u.v3.tx_data_buff, packet[i].header, hdr[i].frame_len, hdr[i].frame_ptr);
		q->u.v3.bytes_available -= frame_len_ceil;


		nfb_comp_write(q->u.v3.tx_hdr_buff, &hdr[i], 8, (uint64_t)shp*8);
		// This below line needs to updated somewhere
		shp = (shp + 1) & (q->u.v3.hdr_ptr_mask);
	}
	q->u.v3.packets -= q->u.v3.pkts_to_send;
	q->u.v3.pkts_to_send = 0;
	nc_ndp_v3_tx_burst_flush(priv);

	return 0;
}

static inline unsigned nc_ndp_tx_burst_get(void *priv, struct ndp_packet *packets, unsigned count)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->protocol == 3) {
		return nc_ndp_v3_tx_burst_get(priv, packets, count);
	} else if (q->protocol == 2) {
		return nc_ndp_v2_tx_burst_get(priv, packets, count);
	} else if (q->protocol == 1) {
		return nc_ndp_v1_tx_burst_get(priv, packets, count);
	}
	return 0;
}

static inline void nc_ndp_tx_burst_put(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->protocol == 3) {
		nc_ndp_v3_tx_burst_put(priv);
	} else if (q->protocol == 2) {
		nc_ndp_v2_tx_burst_put(priv);
	} else if (q->protocol == 1) {
		nc_ndp_v1_tx_burst_put(priv);
	}
}

static inline void nc_ndp_tx_burst_flush(void *priv)
{
	struct nc_ndp_queue *q = (struct nc_ndp_queue*) priv;

	if (q->protocol == 3) {
		nc_ndp_v3_tx_burst_flush(priv);
	} else if (q->protocol == 2) {
		nc_ndp_v2_tx_burst_flush(priv);
	} else if (q->protocol == 1) {
		nc_ndp_v1_tx_burst_flush(priv);
	}
}
