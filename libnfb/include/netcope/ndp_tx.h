/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - data transmission - transmit functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
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

static inline int nc_ndp_v1_tx_lock(struct ndp_queue *q)
{
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

static inline int nc_ndp_v1_tx_unlock(struct ndp_queue *q)
{
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

static inline unsigned nc_ndp_v1_tx_burst_get(ndp_tx_queue_t *q, struct ndp_packet *packets, unsigned count)
{
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

static inline void nc_ndp_v2_tx_lock(struct ndp_queue *q)
{
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

static inline void nc_ndp_v2_tx_burst_flush(struct ndp_queue *q)
{
	if (q->u.v2.rhp >= q->u.v2.hdr_items) {
		q->u.v2.rhp -= q->u.v2.hdr_items;
		q->u.v2.hdr -= q->u.v2.hdr_items;
		q->u.v2.off -= q->u.v2.hdr_items;
	}
	q->sync.hwptr = q->u.v2.rhp;
	q->sync.swptr = q->u.v2.rhp;
	q->u.v2.pkts_available = 0;

	if (_ndp_queue_sync(q, &q->sync)) {
		return;
	}
}

static inline unsigned nc_ndp_v2_tx_burst_get(ndp_tx_queue_t *q, struct ndp_packet *packets, unsigned count)
{
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

static inline unsigned nc_ndp_tx_burst_get(ndp_tx_queue_t *q, struct ndp_packet *packets, unsigned count)
{
	if (q->version == 2) {
		return nc_ndp_v2_tx_burst_get(q, packets, count);
	} else if (q->version == 1) {
		return nc_ndp_v1_tx_burst_get(q, packets, count);
	}
	return 0;
}

static inline void nc_ndp_tx_burst_put(struct ndp_queue *q)
{
	if (q->version == 2) {
		if (((q->u.v2.rhp - q->sync.hwptr) & (q->u.v2.hdr_items-1)) > q->u.v2.hdr_items / 4) {
			nc_ndp_v2_tx_burst_flush(q);
		}
	} else if (q->version == 1) {
		/* Publish written data if their size is bigger than quarter of buffer size */
		if (q->u.v1.swptr > q->size / 4) {
			q->sync.hwptr = (q->sync.hwptr + q->u.v1.swptr) & (q->size-1);
			nc_ndp_v1_tx_unlock(q);
		}
	}
}

static inline void nc_ndp_tx_burst_flush(struct ndp_queue *q)
{
	if (q->version == 2) {
		nc_ndp_v2_tx_burst_flush(q);
	} else if (q->version == 1) {
		q->sync.hwptr = (q->sync.hwptr + q->u.v1.swptr) & (q->size-1);
		nc_ndp_v1_tx_unlock(q);
	}
}
