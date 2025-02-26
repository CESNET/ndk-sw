/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - DMA controller - Medusa/v2 type, Calypte/v3 type
 *
 * Copyright (C) 2020-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.cz>
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>

#include "../fdt/libfdt.h"
#include "../nfb.h"
#include "ndp.h"

#include <netcope/dma_ctrl_ndp.h>

#define NDP_CTRL_TX_DESC_SIZE	      (sizeof(struct nc_ndp_desc))
#define NDP_CTRL_RX_DESC_SIZE	      (sizeof(struct nc_ndp_desc))
#define NDP_CTRL_RX_NDP_HDR_SIZE      (sizeof(struct nc_ndp_hdr))
#define NDP_CTRL_RX_CALYPTE_HDR_SIZE  (sizeof(struct nc_calypte_hdr))

#define NDP_CTRL_RX_DESC_BURST (64u)

#define NDP_CTRL_MODE_PACKET_SIMPLE  0 /* One desc per packet only, except desc type0 */
#define NDP_CTRL_MODE_STREAM         1 /* More packets in one descriptor with 8B padding */
#define NDP_CTRL_MODE_USER           2 /* User provides descriptors in offset + header buffer */

#define NDP_CTRL_NEXT_SDP_AGE_MAX 16

#define virt_to_phys_shift(x) (virt_to_phys(x) >> PAGE_SHIFT)

#define NDP_CTRL_DEFAULT_BUFFER_SIZE 4096
#define NDP_CTRL_DEFAULT_INITIAL_OFFSET 64

extern unsigned long ndp_ring_size;

static const uint32_t mps_non_first_block_max_offset = PAGE_SIZE;

typedef uint64_t ndp_offset_t;

/* Size of buffer for one packet in ring */
static unsigned long ndp_ctrl_buffer_size = NDP_CTRL_DEFAULT_BUFFER_SIZE;
static unsigned long ndp_ctrl_initial_offset = NDP_CTRL_DEFAULT_INITIAL_OFFSET;

struct ndp_ctrl_cfg {
	uint32_t buffer_count;
	uint32_t buffer_size;
	uint32_t block_count;
	uint32_t block_size;
	uint32_t initial_offset;
};

/* buffer/ring pointers for walkthrough in packet-simple mode */
struct ndp_ctrl_state_mps {
	struct ndp_ctrl_cfg cfg; /* read-only after attach_ring */
	uint32_t block_offset;
	uint32_t block_index;
	uint32_t buffer_index;
};

struct ndp_ctrl {
	struct nc_ndp_ctrl c;
	uint32_t php; /* Pushed header pointer (converted to descriptors) */
	uint32_t free_desc;
	struct ndp_ctrl_state_mps mps;
	struct ndp_ctrl_cfg cfg; /* applied at next attach_ring / ndp_channel_rinng_resize call */

	uint32_t mode;

	/* INFO: virtual memory: shadowed */
	struct nc_ndp_desc *desc_buffer_v;
	ndp_offset_t    *off_buffer_v;

	union {
		struct {
			void *hdr_buffer_v;
		} common;
		struct {
			struct nc_ndp_hdr *hdr_buffer;
		} medusa;
		struct {
			struct nc_calypte_hdr *hdr_buffer;
			uint64_t free_bytes;
		} calypte;
	} ts;

	uint32_t next_sdp;

	uint32_t flags;

	struct ndp_channel channel;
	struct nfb_device *nfb;

	/* INFO: Use only for alloc / free */
	int desc_count;
	int hdr_count;

	void *hdr_buffer;

	uint8_t hdr_buff_en_rw_map;
	uint8_t next_sdp_age;

	struct nc_ndp_desc *desc_buffer;
	ndp_offset_t    *off_buffer;
	uint32_t *update_buffer;
	resource_size_t update_buffer_phys;
	resource_size_t desc_buffer_phys;
	resource_size_t off_buffer_phys;
	resource_size_t hdr_buffer_phys;
	unsigned long desc_buffer_size;
	unsigned long off_buffer_size;
	unsigned long hdr_buffer_size;
	unsigned long data_buffer_size;

	size_t hdr_mmap_offset;
	size_t off_mmap_offset;
};

struct ndp_packet {
	void *addr;
	uint16_t len;
};

static inline void ndp_ctrl_medusa_mps_meta_first(struct ndp_ctrl_state_mps *s)
{
	s->buffer_index = 0;
	s->block_offset = s->cfg.initial_offset;
	s->block_index = 0;
}

static inline void ndp_ctrl_medusa_mps_meta_init(struct ndp_ctrl_state_mps *s, uint32_t block_size, uint32_t block_count, uint32_t buffer_size, size_t buffer_count, uint32_t initial_offset)
{
	s->cfg.block_size = block_size;
	s->cfg.block_count = block_count;
	s->cfg.buffer_size = buffer_size;
	s->cfg.buffer_count = buffer_count;
	s->cfg.initial_offset = initial_offset;
	ndp_ctrl_medusa_mps_meta_first(s);
}

/* Walk through ring for every packet.
 * returns -1 on ring wrap
 *          1 on block increment (without ring wrap)
 *          0 otherwise
 */
static inline int ndp_ctrl_medusa_mps_inc(struct ndp_ctrl_state_mps *s)
{
	s->buffer_index++;
	if (s->buffer_index == s->cfg.buffer_count) {
		/* wrap ring to first buffer */
		ndp_ctrl_medusa_mps_meta_first(s);
		return -1;
	} else {
		/* move offset to next buffer */
		s->block_offset += s->cfg.buffer_size;
		if (s->block_offset + s->cfg.buffer_size > s->cfg.block_size) {
			/* this offset crosses the current block and therefore cannot be used; advance to next block */
			s->block_index++;
			/* do not reset block_offset, ideally continue with the same value,
			 * but use some reasonable maximum offset in the block */
			s->block_offset %= mps_non_first_block_max_offset;
			if (s->block_index == s->cfg.block_count) {
				/* wrap ring block (initial offset is larger than block_size) */
				s->block_index = 0;
			}
			return 1;
		}
	}
	return 0;
}

/* Check buffer configuration and compute block_count for ring allocation.
 * Optionally resize the ring with new configuration.
 */
static int ndp_ctrl_medusa_req_block_update(struct ndp_ctrl *ctrl, int do_resize, size_t buffer_size, size_t buffer_count, size_t initial_offset)
{
	int ret = 0;
	/* optimization - enable shadowed mmap, which needs at least PAGE_SIZE space */
	const int min_buffer_items = PAGE_SIZE / min(NDP_CTRL_RX_DESC_SIZE, NDP_CTRL_RX_NDP_HDR_SIZE);

	struct ndp_ctrl_state_mps s;

	if (buffer_size == 0 || buffer_count < min_buffer_items)
		return -EINVAL;

	/* walk through (virtual) ring to obtain parameters
	 * - the inc with buffer_count = 0 never wraps/resets buffer_index
	 * - the inc with block_count = 0 never wraps/resets block_index
	 */
	ndp_ctrl_medusa_mps_meta_init(&s, ctrl->channel.req_block_size, 0, buffer_size, 0, initial_offset);
	while (s.buffer_index < buffer_count) {
		ndp_ctrl_medusa_mps_inc(&s);
	}
	/* Check if last buffer(s) fits into offseted first block.
	 * This save one block for some configurations. */
	if (s.block_offset <= s.cfg.initial_offset)
		s.block_index--;

	s.cfg.block_count = s.block_index + 1;
	s.cfg.buffer_count = s.buffer_index;

	ctrl->cfg = s.cfg;
	ctrl->channel.req_block_count = s.cfg.block_count;

	if (do_resize) {
		ret = ndp_channel_ring_resize(&ctrl->channel);
	}

	if (ret)
		return ret;

	return 0;
}

static ssize_t ndp_ctrl_get_buffer_size(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	return scnprintf(buf, PAGE_SIZE, "%u\n", ctrl->cfg.buffer_size);
}

static ssize_t ndp_ctrl_set_buffer_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = -1;
	unsigned long long value;
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	value = memparse(buf, NULL);

	ret = ndp_ctrl_medusa_req_block_update(ctrl, 1, value, ctrl->cfg.buffer_count, ctrl->cfg.initial_offset);
	if (ret)
		return ret;

	return size;
}

static ssize_t ndp_ctrl_get_buffer_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	return scnprintf(buf, PAGE_SIZE, "%u\n", ctrl->cfg.buffer_count);
}

static ssize_t ndp_ctrl_set_buffer_count(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = -1;
	unsigned long long value;
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	value = memparse(buf, NULL);

	ret = ndp_ctrl_medusa_req_block_update(ctrl, 1, ctrl->cfg.buffer_size, value, ctrl->cfg.initial_offset);
	if (ret)
		return ret;

	return size;
}

static ssize_t ndp_ctrl_get_initial_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	return scnprintf(buf, PAGE_SIZE, "%u\n", ctrl->cfg.initial_offset);
}

static ssize_t ndp_ctrl_set_initial_offset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = -1;
	unsigned long long value;
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	value = memparse(buf, NULL);

	ret = ndp_ctrl_medusa_req_block_update(ctrl, 1, ctrl->cfg.buffer_size, ctrl->cfg.buffer_count, value);
	if (ret)
		return ret;

	return size;
}

static ssize_t ndp_ctrl_get_ring_size(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	return scnprintf(buf, PAGE_SIZE, "%u\n", ctrl->cfg.buffer_size * ctrl->cfg.buffer_count);
}

static ssize_t ndp_ctrl_set_ring_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = -1;
	unsigned long long value;
	unsigned long buffer_count = 1;
	struct ndp_channel *channel = dev_get_drvdata(dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	value = memparse(buf, NULL);

	if (ctrl->cfg.buffer_size == 0)
		return -EINVAL;

	while (buffer_count * 2 <= value / ctrl->cfg.buffer_size)
		buffer_count *= 2;

	ret = ndp_ctrl_medusa_req_block_update(ctrl, 1, ctrl->cfg.buffer_size, buffer_count, ctrl->cfg.initial_offset);
	if (ret)
		return ret;

	return size;
}

/// @brief Function sets `hdr` and `off` with information from `channel`. Returns header count.
/// @param channel
/// @param hdr buffer of headers
/// @param off offset into the buffer
/// @return Header count
int ndp_ctrl_v2_get_vmaps(struct ndp_channel *channel, void **hdr, void **off)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	*hdr = ctrl->ts.common.hdr_buffer_v;
	*off = ctrl->off_buffer_v;
	return ctrl->hdr_count;
}

int ndp_ctrl_v3_get_vmaps(struct ndp_channel *channel, void **hdr, size_t * hdr_mmap_size, size_t * data_buf_size, size_t * hdr_buf_size)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	*hdr = ctrl->ts.common.hdr_buffer_v;
	*hdr_mmap_size = ctrl->hdr_buffer_size * 2;
	*hdr_buf_size = ctrl->hdr_buffer_size;
	*data_buf_size = ctrl->data_buffer_size;
	return ctrl->hdr_count;
}

static void ndp_ctrl_mps_fill_rx_descs(struct ndp_ctrl *ctrl, uint64_t count)
{
	int i;
	uint32_t sdp = ctrl->c.sdp;
	struct nc_ndp_desc *desc = ctrl->desc_buffer_v + sdp;
	uint64_t last_upper_addr = ctrl->c.last_upper_addr;

	/* TODO: Table of prepared descriptor bursts with all meta (desc. count etc) */
	for (i = 0; i < count; i++) {
		dma_addr_t addr;

		addr = ctrl->channel.ring.blocks[ctrl->mps.block_index].phys;
		addr += ctrl->mps.block_offset;

		if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(addr) != last_upper_addr)) {
			last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(addr);
			ctrl->c.last_upper_addr = last_upper_addr;
			desc[i] = nc_ndp_rx_desc0(addr);
			continue;
		}
		desc[i] = nc_ndp_rx_desc2(addr, ctrl->mps.cfg.buffer_size, 0);

		ndp_ctrl_medusa_mps_inc(&ctrl->mps);
	}
	ctrl->c.sdp = (sdp + count) & ctrl->c.mdp;
}

static void ndp_ctrl_user_fill_rx_descs(struct ndp_ctrl *ctrl)
{
	int i,j;
	int count;
	uint32_t php = ctrl->php;
	uint32_t mdp = ctrl->c.mdp;
	uint32_t sdp = ctrl->c.sdp;

	ndp_offset_t *off = ctrl->off_buffer_v + php;
	struct nc_ndp_hdr *hdr = ctrl->ts.medusa.hdr_buffer + php;
	struct nc_ndp_desc *desc = ctrl->desc_buffer_v + ctrl->next_sdp;
	uint32_t sdp_shift;
	uint64_t last_upper_addr = ctrl->c.last_upper_addr;

	count = (ctrl->c.shp - php) & ctrl->c.mhp;

	ctrl->free_desc = (ctrl->c.hdp - ctrl->next_sdp - 1) & ctrl->c.mhp;

	j = 0;
	for (i = 0; i < count; i++) {
		dma_addr_t addr = off[i];

		if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(addr) != last_upper_addr)) {
			if (unlikely(ctrl->free_desc == 0)) {
				break;
			}

			last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(addr);
			ctrl->c.last_upper_addr = last_upper_addr;
			desc[j] = nc_ndp_rx_desc0(addr);
			ctrl->free_desc--;
			j++;
		}

		if (unlikely(ctrl->free_desc == 0)) {
			break;
		}

		desc[j] = nc_ndp_rx_desc2(addr, hdr[i].frame_len, 0);
		ctrl->free_desc--;
		j++;
	}

	if (i == 0) {
		return;
	}

	wmb();

	ctrl->next_sdp = (ctrl->next_sdp + j) & mdp;

	ctrl->php = (php + i) & ctrl->c.mhp;

	sdp_shift = (ctrl->next_sdp - sdp) & mdp;

	if (j == 0 && sdp_shift != 0)
		ctrl->next_sdp_age++;
	else
		ctrl->next_sdp_age = 0;

	if (unlikely(ctrl->next_sdp_age == NDP_CTRL_NEXT_SDP_AGE_MAX)) {
		// SDP has been waiting to shift by a whole burst for quite some time
		// shift now, ignoring burst size, to avoid deadlock
		ctrl->c.sdp = (sdp + sdp_shift) & mdp;
		nc_ndp_ctrl_sp_flush(&ctrl->c);
	} else {
		sdp_shift = (sdp_shift / NDP_CTRL_RX_DESC_BURST) * NDP_CTRL_RX_DESC_BURST;

		if (sdp_shift) {
			ctrl->c.sdp = (sdp + sdp_shift) & mdp;
			nc_ndp_ctrl_sp_flush(&ctrl->c);
		}
	}
}

static void ndp_ctrl_medusa_rx_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	uint32_t shp = ctrl->c.shp;

	struct nc_ndp_hdr *hdr = ctrl->ts.medusa.hdr_buffer + shp;
//	struct nc_ndp_desc *desc_buffer = ctrl->desc_buffer_v + ctrl->sdp;

	if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
		int i;
		int count;
		int free_desc = 0;
		int free_desc2 = 0;

		count = (ptr - shp) & ctrl->c.mhp;
		for (i = 0; i < count; i++) {
			/* Expecting only 1 or 2 free desc for each packet/header */
			if (hdr[i].free_desc == 1) {
				free_desc++;
			} else if (hdr[i].free_desc == 2) {
				free_desc2++;
			}
		}
		ctrl->free_desc += free_desc + free_desc2 * 2;
		ctrl->c.shp = ptr;

		count = 0;
		while (ctrl->free_desc >= NDP_CTRL_RX_DESC_BURST) {
			ndp_ctrl_mps_fill_rx_descs(ctrl, NDP_CTRL_RX_DESC_BURST);
			ctrl->free_desc -= NDP_CTRL_RX_DESC_BURST;
			count = 1;
		}
		if (count) {
			nc_ndp_ctrl_sp_flush(&ctrl->c);
		}
	} else if (ctrl->mode == NDP_CTRL_MODE_STREAM) {
		/* TODO */
	} else if (ctrl->mode == NDP_CTRL_MODE_USER) {
		ctrl->c.shp = ptr;
		nc_ndp_ctrl_hdp_update(&ctrl->c);
		ndp_ctrl_user_fill_rx_descs(ctrl);
	}
}

static void ndp_ctrl_calypte_rx_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	struct nc_calypte_hdr *hdr = ctrl->ts.calypte.hdr_buffer + ctrl->c.shp;
	int count = (ptr - ctrl->c.shp) & ctrl->c.mhp;

	int new_sdp = 0;
	unsigned i;

	for (i = 0; i < count; i++) {
		new_sdp += (hdr[i].frame_len + NDP_RX_CALYPTE_BLOCK_SIZE - 1) / NDP_RX_CALYPTE_BLOCK_SIZE;
	}

	ctrl->c.shp = ptr;
	ctrl->c.sdp = (ctrl->c.sdp + new_sdp) & ctrl->c.mdp;
	if (count) {
		nc_ndp_ctrl_sp_flush(&ctrl->c);
	}
}

static uint64_t ndp_ctrl_rx_get_hwptr(struct ndp_channel *channel)
{
	int i;
	int count;
	uint32_t hhp;
	uint32_t hhp_new;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	hhp = ctrl->c.hhp;

	if (ctrl->c.type == DMA_TYPE_CALYPTE) {
		struct nc_calypte_hdr *hdr_base;
		uint32_t hwptr = ctrl->c.hhp;
		do {
			hdr_base = ctrl->ts.calypte.hdr_buffer + hwptr;
			if (hdr_base->valid == 0)
				break;
			hwptr++;
		} while (hwptr < ctrl->hdr_buffer_size * 2);
		ctrl->c.hhp = hwptr & channel->ptrmask;
		return ctrl->c.hhp;
	}
	nc_ndp_ctrl_hhp_update(&ctrl->c);

	hhp_new = ctrl->c.hhp;
	count = (hhp_new - hhp) & ctrl->c.mhp;

	if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
		/* Constant packet offsets in this mode */
	} else if (ctrl->mode == NDP_CTRL_MODE_STREAM) {
		struct nc_ndp_hdr *hdr = ctrl->ts.medusa.hdr_buffer + hhp;
		ndp_offset_t *off = ctrl->off_buffer_v + hhp;
		for (i = 0; i < count; i++) {
			off[i+1] = off[i] + hdr[i].frame_len;
		}
		off[i] &= channel->ring.size - 1;
	} else if (ctrl->mode == NDP_CTRL_MODE_USER) {
		/* Check if some descs from userspace can be written */
		if (count && ctrl->php != ctrl->c.shp) {
			nc_ndp_ctrl_hdp_update(&ctrl->c);
			ndp_ctrl_user_fill_rx_descs(ctrl);
		}
	}
	return hhp_new;
}

static uint64_t ndp_ctrl_medusa_tx_get_hwptr(struct ndp_channel *channel);

inline int ndp_ctrl_medusa_tx_wait_for_free_desc(struct ndp_channel *channel, int count)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	while (ctrl->free_desc < count) {
		udelay(10);
		ndp_ctrl_medusa_tx_get_hwptr(channel);

		if (unlikely(ndp_kill_signal_pending(current))) {
			return 1;
		}
	}
	return 0;
}

static void ndp_ctrl_medusa_tx_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	int i;
	int count;

	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	uint64_t last_upper_addr = ctrl->c.last_upper_addr;
	uint32_t sdp = ctrl->c.sdp;
	uint32_t shp = ctrl->c.shp;
	struct nc_ndp_desc *desc = ctrl->desc_buffer_v;
	wmb();

	count = (ptr - shp) & ctrl->c.mhp;

	for (i = 0; i < count; i++) {
		dma_addr_t addr;
		ndp_offset_t *off;
		struct nc_ndp_hdr *hdr;

		off = ctrl->off_buffer_v + shp + i;
		hdr = ctrl->ts.medusa.hdr_buffer + shp + i;

		if (ctrl->mode == NDP_CTRL_MODE_USER) {
			addr = *off;
		} else {
			addr = channel->ring.blocks[ctrl->mps.block_index].phys;
			addr += ctrl->mps.block_offset;
		}

		if (ndp_ctrl_medusa_tx_wait_for_free_desc(channel, 2))
			break;

		if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(addr) != last_upper_addr)) {
			last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(addr);
			ctrl->c.last_upper_addr = last_upper_addr;
			desc[sdp] = nc_ndp_tx_desc0(addr);
			sdp++;
			ctrl->free_desc--;
		}

		desc[sdp] = nc_ndp_tx_desc2(addr, hdr->frame_len, hdr->meta, 0);
		sdp++;
		ctrl->free_desc--;

		ndp_ctrl_medusa_mps_inc(&ctrl->mps);
	}
	/*
	if (unlikely(dirty)) {
		dev_warn(ctrl->comp->nfb->dev,
				"NDP queue %s failed to shift SDP due to HDP being stuck "
				"Transfers aborted by users, queue might be in dirty state.\n",
				dev_name(&channel->dev));
	}
	*/

	wmb();
	ctrl->c.sdp = sdp & ctrl->c.mdp;
	ctrl->c.shp = ptr;
	nc_ndp_ctrl_sdp_flush(&ctrl->c);
}

static uint64_t ndp_ctrl_medusa_tx_get_hwptr(struct ndp_channel *channel)
{
	uint32_t hdp;
	int i, count, free_hdrs = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct nc_ndp_desc *desc;

	rmb();
	hdp = ctrl->c.hdp;
	nc_ndp_ctrl_hdp_update(&ctrl->c);
	count = (ctrl->c.hdp - hdp) & ctrl->c.mdp;
	ctrl->free_desc += count;

	desc = ctrl->desc_buffer_v + hdp;
	for (i = 0; i < count; i++) {
		if (desc[i].d.type2.type == 2)
			free_hdrs++;

	}
	ctrl->c.hhp = (ctrl->c.hhp + free_hdrs) & ctrl->c.mhp;

	return ctrl->c.hhp;
}

static uint64_t ndp_ctrl_calypte_tx_get_free_space(struct ndp_channel *channel) {
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	return ctrl->ts.calypte.free_bytes;
}

static uint64_t ndp_ctrl_calypte_tx_get_hwptr(struct ndp_channel *channel)
{
	uint32_t hdp;
	int count;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	hdp = ctrl->c.hdp;
	nc_ndp_ctrl_hp_update(&ctrl->c);
	count = (ctrl->c.hdp - hdp) & ctrl->c.mdp;

	ctrl->ts.calypte.free_bytes += count;
	return ctrl->c.hhp;
}

static void ndp_ctrl_calypte_tx_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	int i;
	int count;

	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	uint32_t sdp = ctrl->c.sdp;
	uint32_t shp = ctrl->c.shp;
	wmb();

	count = (ptr - shp) & ctrl->c.mhp;

	for (i = 0; i < count; i++) {
		struct nc_calypte_hdr *hdr;
		hdr = ctrl->ts.calypte.hdr_buffer + shp + i;
		// Subtracting whole multiples of NDP_TX_CALYPTE_BLOCK_SIZE from the ctrl->free_bytes
		ctrl->ts.calypte.free_bytes -= (hdr->frame_len + (NDP_TX_CALYPTE_BLOCK_SIZE -1)) & (~(NDP_TX_CALYPTE_BLOCK_SIZE -1));

		// Rounding the SDP to the nearest higher multiple of NDP_TX_CALYPTE_BLOCK_SIZE
		sdp = ((sdp + hdr->frame_len + (NDP_TX_CALYPTE_BLOCK_SIZE -1)) & (~(NDP_TX_CALYPTE_BLOCK_SIZE -1))) & ctrl->c.mdp;
	}

	wmb();
	ctrl->c.sdp = sdp;
	ctrl->c.shp = ptr;
	nc_ndp_ctrl_sp_flush(&ctrl->c);
}

static uint64_t ndp_ctrl_get_flags(struct ndp_channel *channel)
{
	uint64_t ret = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (ctrl->c.type == DMA_TYPE_MEDUSA) {
		ret |= NDP_CHANNEL_FLAG_USE_HEADER;
		ret |= NDP_CHANNEL_FLAG_USE_OFFSET;
	}

	ret |= ctrl->flags;

	return ret;
}

static uint64_t ndp_ctrl_set_flags(struct ndp_channel *channel, uint64_t flags)
{
	uint64_t ret;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	ret = ndp_ctrl_get_flags(channel);

	if (ctrl->c.type == DMA_TYPE_CALYPTE) {
		if (flags & NDP_CHANNEL_FLAG_USERSPACE) {
			ret |= NDP_CHANNEL_FLAG_USERSPACE;
			ctrl->flags |= NDP_CHANNEL_FLAG_USERSPACE;
		} else {
			ctrl->flags &= ~NDP_CHANNEL_FLAG_USERSPACE;
		}
	}

	return ret;
}

static int ndp_ctrl_start(struct ndp_ctrl *ctrl, struct nc_ndp_ctrl_start_params *sp)
{
	int ret;

	ret = nc_ndp_ctrl_start(&ctrl->c, sp);
	if (ret == -EALREADY) {
		/* Try to stop */
		nc_ndp_ctrl_stop_force(&ctrl->c);
		msleep(10);
		ret = nc_ndp_ctrl_start(&ctrl->c, sp);
		if (ret == 0) {
			dev_err(ctrl->nfb->dev, "NDP queue %s was in dirty state, restart seems succesfull, "
					"but errors can occur\n", dev_name(&ctrl->channel.dev));
		} else {
			dev_err(ctrl->nfb->dev, "NDP queue %s is in dirty state, can't be started\n", dev_name(&ctrl->channel.dev));
			return -1;
		}
	}

	return ret;
}

static int ndp_ctrl_medusa_start(struct ndp_channel *channel, uint64_t *hwptr)
{
	int ret;
	struct nc_ndp_ctrl_start_params sp;

	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	ndp_offset_t *off;

	sp.update_buffer_virt = ctrl->update_buffer;
	sp.desc_buffer = ctrl->desc_buffer_phys;
	sp.hdr_buffer = ctrl->hdr_buffer_phys;
	sp.update_buffer = ctrl->update_buffer_phys;
	sp.nb_desc = ctrl->desc_count;
	sp.nb_hdr = ctrl->hdr_count;

	ret = ndp_ctrl_start(ctrl, &sp);
	if (ret)
		return ret;

	ctrl->next_sdp = 0;

	ctrl->mode = NDP_CTRL_MODE_PACKET_SIMPLE;

	if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
		/* Constant packet offsets in this mode */
		off = ctrl->off_buffer_v;

		ndp_ctrl_medusa_mps_meta_first(&ctrl->mps);
		do {
			*(off++) = (ctrl->mps.block_index * ctrl->mps.cfg.block_size) + ctrl->mps.block_offset;
		} while (ndp_ctrl_medusa_mps_inc(&ctrl->mps) != -1);
	} else if (ctrl->mode == NDP_CTRL_MODE_USER) {
		if (channel->id.type == NDP_CHANNEL_TYPE_RX) {
			ctrl->free_desc = ctrl->c.mhp;
			ctrl->php = 0;
		}
	}

	if (channel->id.type == NDP_CHANNEL_TYPE_RX) {
		if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
			ndp_ctrl_mps_fill_rx_descs(ctrl, ctrl->c.mdp + 1 - NDP_CTRL_RX_DESC_BURST);
			nc_ndp_ctrl_sdp_flush(&ctrl->c);
			ctrl->free_desc = 0;
		}

		/* TODO: Check if SHP is to be 0 after start or must first set to an value */
	} else if (channel->id.type == NDP_CHANNEL_TYPE_TX) {
		ctrl->free_desc = ctrl->c.mdp;
	}

	*hwptr = 0;
	return 0;
}

static int ndp_ctrl_calypte_start(struct ndp_channel *channel, uint64_t *hwptr)
{
	int ret;
	struct nc_ndp_ctrl_start_params sp;

	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct nc_calypte_hdr *hdr_base;

	// Only one block is used, therefore the physical address of the first one in "blocks"
	// list is used.
	sp.data_buffer = channel->ring.blocks->phys;
	sp.hdr_buffer = ctrl->hdr_buffer_phys;
	sp.nb_data = ctrl->hdr_count;
	sp.nb_hdr = ctrl->hdr_count;

	for (ret = 0; ret < ctrl->hdr_count; ret++) {
		hdr_base = ctrl->ts.calypte.hdr_buffer + ret;
		hdr_base->valid = 0;
	}

	ret = ndp_ctrl_start(ctrl, &sp);
	if (ret)
		return ret;

	ctrl->ts.calypte.free_bytes = ctrl->c.mdp;

	*hwptr = 0;
	return 0;
}

static int ndp_ctrl_stop(struct ndp_channel *channel, int force)
{
	int ret;
	int cnt = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (ctrl->c.type == DMA_TYPE_CALYPTE && ctrl->c.dir != 0 && ctrl->flags & NDP_CHANNEL_FLAG_USERSPACE) {
		nc_ndp_ctrl_hp_update(&ctrl->c);
		ctrl->c.sdp = ctrl->c.hdp;
		ctrl->c.shp = ctrl->c.hhp;
		nc_ndp_ctrl_sp_flush(&ctrl->c);
	}

	while (cnt < 10 || (!ndp_kill_signal_pending(current) && !force)) {
		ret = nc_ndp_ctrl_stop(&ctrl->c);
		if (ret == 0) {
			break;
		} else if (ret == -EINPROGRESS) {
			cnt = 0;
		} else if (ret == -EAGAIN && !force) {
			return -EAGAIN;
		}
		msleep(10);
		cnt++;
	}

	if (ret) {
		nc_ndp_ctrl_stop_force(&ctrl->c);
		dev_err(ctrl->nfb->dev,
			"NDP queue %s did't stop in %d msecs. "
			"This may be due to firmware error.\n",
			dev_name(&channel->dev), cnt * 10);
	}

	ctrl->flags &= ~NDP_CHANNEL_FLAG_USERSPACE;

	return 0;
}

static int ndp_ctrl_hdr_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv)
{
	int ret = -1;
	struct ndp_ctrl *ctrl = (struct ndp_ctrl*) priv;

	/* TODO: Check if channel is subscribed */

	//Check permissions: read-only for RX unless it is DMA Calypte
	if (ctrl->channel.id.type == NDP_CHANNEL_TYPE_RX && (vma->vm_flags & (VM_WRITE | VM_READ)) != VM_READ && ctrl->hdr_buff_en_rw_map == 0)
		return -EINVAL;

	/* Allow mmap only for exact offset & size match */
	if (offset != ctrl->hdr_mmap_offset || size != ctrl->hdr_buffer_size * 2) {
		return -EINVAL;
	}

	ret = remap_pfn_range(vma, vma->vm_start, virt_to_phys_shift(ctrl->hdr_buffer), size / 2, vma->vm_page_prot);
	if (ret)
		return ret;
	ret = remap_pfn_range(vma, vma->vm_start + size / 2, virt_to_phys_shift(ctrl->hdr_buffer), size / 2, vma->vm_page_prot);
	return ret;
}

static int ndp_ctrl_off_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv)
{
	int ret;
	struct ndp_ctrl *ctrl = (struct ndp_ctrl*) priv;

	/* TODO: Check if channel is subscribed */

	/* Check permissions: read-only for RX */
	if (ctrl->channel.id.type == NDP_CHANNEL_TYPE_RX && (vma->vm_flags & (VM_WRITE | VM_READ)) != VM_READ)
		return -EINVAL;

	/* Allow mmap only for exact offset & size match */
	if (offset != ctrl->off_mmap_offset || size != ctrl->off_buffer_size * 2) {
		return -EINVAL;
	}

	ret = remap_pfn_range(vma, vma->vm_start, virt_to_phys_shift(ctrl->off_buffer), size / 2, vma->vm_page_prot);
	if (ret)
		return ret;
	ret = remap_pfn_range(vma, vma->vm_start + size / 2, virt_to_phys_shift(ctrl->off_buffer), size / 2, vma->vm_page_prot);

	return ret;
}

static void *ndp_ctrl_vmap_shadow(int size, void *virt)
{
	int i;
	int page_count;
	struct page **pages;
	void *ret;

	page_count = (size / PAGE_SIZE);
	pages = kmalloc(sizeof(struct page*) * page_count * 2, GFP_KERNEL);
	if (pages == NULL)
		return NULL;

	for (i = 0; i < page_count; i++) {
		pages[i] = virt_to_page(virt + i*PAGE_SIZE);
		pages[i + page_count] = pages[i];
	}
	ret = vmap(pages, page_count * 2, VM_MAP, PAGE_KERNEL);
	kfree(pages);
	return ret;
}

static int ndp_ctrl_medusa_attach_ring(struct ndp_channel *channel)
{
	int ret = -ENOMEM;
	int node_offset;
	void *fdt = channel->ndp->nfb->fdt;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	struct device *dev = channel->ring.dev;

	if (channel->ring.size == 0)
		return -EINVAL;

	/* just check already requested ring parameters */
	if (ndp_ctrl_medusa_req_block_update(ctrl, 0, ctrl->cfg.buffer_size, ctrl->cfg.buffer_count, ctrl->cfg.initial_offset))
		return -EINVAL;

	/* apply configuration */
	ctrl->mps.cfg = ctrl->cfg;

	ctrl->desc_count = ctrl->hdr_count = ctrl->mps.cfg.buffer_count;

	channel->ptrmask = ctrl->hdr_count - 1;

	/* allocate update buffer */
	ctrl->update_buffer = dma_alloc_coherent(dev, ALIGN(NDP_CTRL_UPDATE_SIZE, PAGE_SIZE),
			&ctrl->update_buffer_phys, GFP_KERNEL);
	if (ctrl->update_buffer == NULL) {
		goto err_alloc_update;
	}

	/* allocate descriptor area */
	ctrl->desc_buffer_size = ALIGN(ctrl->desc_count * NDP_CTRL_RX_DESC_SIZE, PAGE_SIZE);
	ctrl->desc_buffer = dma_alloc_coherent(dev, ctrl->desc_buffer_size,
			&ctrl->desc_buffer_phys, GFP_KERNEL);
	if (ctrl->desc_buffer == NULL) {
		goto err_alloc_desc;
	}

	ctrl->desc_buffer_v = ndp_ctrl_vmap_shadow(ctrl->desc_buffer_size, ctrl->desc_buffer);
	if (ctrl->desc_buffer_v == NULL) {
		goto err_vmap_desc;
	}

	/* allocate offsets area */
	ctrl->off_buffer_size = ALIGN(ctrl->hdr_count * sizeof(ndp_offset_t), PAGE_SIZE);
	ctrl->off_buffer = dma_alloc_coherent(dev, ctrl->off_buffer_size,
			&ctrl->off_buffer_phys, GFP_KERNEL);
	if (ctrl->off_buffer == NULL) {
		goto err_alloc_off;
	}

	ctrl->off_buffer_v = ndp_ctrl_vmap_shadow(ctrl->off_buffer_size, ctrl->off_buffer);
	if (ctrl->off_buffer_v == NULL) {
		goto err_vmap_off_buffer;
	}

	ret = nfb_char_register_mmap(channel->ndp->nfb, ctrl->off_buffer_size * 2, &ctrl->off_mmap_offset, ndp_ctrl_off_mmap, ctrl);
	if (ret) {
		goto err_register_mmap_off;
	}

	/* allocate header area */
	ctrl->hdr_buffer_size = ALIGN(ctrl->hdr_count * NDP_CTRL_RX_NDP_HDR_SIZE, PAGE_SIZE);
	ctrl->hdr_buffer = dma_alloc_coherent(dev, ctrl->hdr_buffer_size,
			&ctrl->hdr_buffer_phys, GFP_KERNEL);
	if (ctrl->hdr_buffer == NULL) {
		goto err_alloc_hdr;
	}

	ctrl->ts.common.hdr_buffer_v = ndp_ctrl_vmap_shadow(ctrl->hdr_buffer_size, ctrl->hdr_buffer);
	if (ctrl->ts.common.hdr_buffer_v == NULL) {
		goto err_vmap_hdr_buffer;
	}

	ret = nfb_char_register_mmap(channel->ndp->nfb, ctrl->hdr_buffer_size * 2, &ctrl->hdr_mmap_offset, ndp_ctrl_hdr_mmap, ctrl);
	if (ret) {
		goto err_register_mmap_hdr;
	}

	node_offset = fdt_path_offset(fdt, channel->id.type == NDP_CHANNEL_TYPE_TX ?
				"/drivers/ndp/tx_queues" : "/drivers/ndp/rx_queues");
	node_offset = fdt_subnode_offset(fdt, node_offset, dev_name(&channel->dev));

	fdt_setprop_u32(fdt, node_offset, "protocol", 2);
	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_base", ctrl->hdr_mmap_offset);
	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_size", ctrl->hdr_buffer_size * 2);

	fdt_setprop_u64(fdt, node_offset, "off_mmap_base", ctrl->off_mmap_offset);
	fdt_setprop_u64(fdt, node_offset, "off_mmap_size", ctrl->off_buffer_size * 2);

	fdt_setprop_u32(fdt, node_offset, "buffer_size", ctrl->mps.cfg.buffer_size);

	return 0;

	//nfb_char_unregister_mmap(channel->ndp->nfb, ctrl->hdr_mmap_offset);
err_register_mmap_hdr:
	vunmap(ctrl->ts.common.hdr_buffer_v);
err_vmap_hdr_buffer:
	dma_free_coherent(dev, ctrl->hdr_buffer_size,
			ctrl->hdr_buffer, ctrl->hdr_buffer_phys);
	ctrl->hdr_buffer = NULL;

err_alloc_hdr:
	nfb_char_unregister_mmap(channel->ndp->nfb, ctrl->off_mmap_offset);
err_register_mmap_off:
	vunmap(ctrl->off_buffer_v);
err_vmap_off_buffer:
	dma_free_coherent(dev, ctrl->off_buffer_size,
			ctrl->off_buffer, ctrl->off_buffer_phys);
	ctrl->off_buffer = NULL;

err_alloc_off:
	vunmap(ctrl->desc_buffer_v);
err_vmap_desc:
	dma_free_coherent(dev, ctrl->desc_buffer_size,
			ctrl->desc_buffer, ctrl->desc_buffer_phys);
	ctrl->desc_buffer = NULL;

err_alloc_desc:
	dma_free_coherent(dev, ALIGN(NDP_CTRL_UPDATE_SIZE, PAGE_SIZE),
			ctrl->update_buffer, ctrl->update_buffer_phys);
	ctrl->update_buffer = NULL;

err_alloc_update:
	return ret;
}

static int ndp_ctrl_rx_calypte_attach_ring(struct ndp_channel *channel)
{
	int ret = -ENOMEM;
	int node_offset;
	void *fdt = channel->ndp->nfb->fdt;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	struct device *dev = channel->ring.dev;

	if (channel->ring.size == 0)
		return -EINVAL;

	if (channel->ring.block_count != 1)
		return -EINVAL;

	ctrl->hdr_buff_en_rw_map = 1;

	ctrl->hdr_count = channel->ring.size / NDP_RX_CALYPTE_BLOCK_SIZE;
	if (ctrl->hdr_count * min((uint16_t)NDP_RX_CALYPTE_BLOCK_SIZE, (uint16_t)NDP_CTRL_RX_CALYPTE_HDR_SIZE) < PAGE_SIZE) {
		/* Can't do shadow-map for this ring size */
		return -EINVAL;
	}

	channel->ptrmask = ctrl->hdr_count - 1;

	/* allocate header area */
	ctrl->hdr_buffer_size = ALIGN(ctrl->hdr_count * NDP_CTRL_RX_CALYPTE_HDR_SIZE, PAGE_SIZE);
	ctrl->hdr_buffer = dma_alloc_coherent(dev, ctrl->hdr_buffer_size,
			&ctrl->hdr_buffer_phys, GFP_KERNEL);
	if (ctrl->hdr_buffer == NULL) {
		return -EINVAL;
	}

	ctrl->ts.common.hdr_buffer_v = ndp_ctrl_vmap_shadow(ctrl->hdr_buffer_size, ctrl->hdr_buffer);
	if (ctrl->ts.common.hdr_buffer_v == NULL) {
		goto err_vmap_hdr_buffer;
	}

	ret = nfb_char_register_mmap(channel->ndp->nfb, ctrl->hdr_buffer_size * 2, &ctrl->hdr_mmap_offset, ndp_ctrl_hdr_mmap, ctrl);
	if (ret) {
		goto err_register_mmap_hdr;
	}

	node_offset = fdt_path_offset(fdt, "/drivers/ndp/rx_queues");
	node_offset = fdt_subnode_offset(fdt, node_offset, dev_name(&channel->dev));

	fdt_setprop_u32(fdt, node_offset, "protocol", 3);
	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_base", ctrl->hdr_mmap_offset);
	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_size", ctrl->hdr_buffer_size * 2);

	return 0;

err_register_mmap_hdr:
	vunmap(ctrl->ts.common.hdr_buffer_v);

err_vmap_hdr_buffer:
	dma_free_coherent(dev, ctrl->hdr_buffer_size,
			ctrl->hdr_buffer, ctrl->hdr_buffer_phys);
	ctrl->hdr_buffer = NULL;
	return ret;
}

static int ndp_ctrl_tx_calypte_attach_ring(struct ndp_channel *channel)
{
	int ret = -ENOMEM;
	int node_offset;
	void *fdt = channel->ndp->nfb->fdt;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	int ctrl_node_offset = -1;
	int buffer_offset = -1;
	int proplen;
	const fdt32_t *prop = NULL;

	struct device *dev = channel->ring.dev;

	node_offset = fdt_path_offset(fdt, "/drivers/ndp/tx_queues");
	node_offset = fdt_subnode_offset(fdt, node_offset, dev_name(&channel->dev));

	// Find control node of the channel
	ctrl_node_offset = fdt_node_offset_by_phandle_ref(fdt, node_offset, "ctrl");
	if (ctrl_node_offset < 0)
		return -EBADFD;

	// Find mask for the data buffer
	buffer_offset = fdt_node_offset_by_phandle_ref(fdt, ctrl_node_offset, "data_buff");
	if (buffer_offset < 0)
		return -EBADFD;

	prop = fdt_getprop(fdt, buffer_offset, "reg", &proplen);
	if (prop == NULL)
		return -EBADFD;

	ctrl->data_buffer_size = fdt32_to_cpu(prop[1]);

	// Find mask for the header buffer
	buffer_offset = fdt_node_offset_by_phandle_ref(fdt, ctrl_node_offset, "hdr_buff");
	if (buffer_offset < 0)
		return -EBADFD;

	prop = fdt_getprop(fdt, buffer_offset, "reg", &proplen);
	if (prop == NULL)
		return -EBADFD;

	ctrl->hdr_buffer_size = fdt32_to_cpu(prop[1]);

	/* Allocate header area */
	ctrl->hdr_buffer = dma_alloc_coherent(dev, ctrl->hdr_buffer_size, &ctrl->hdr_buffer_phys, GFP_KERNEL);
	if (ctrl->hdr_buffer == NULL) {
		goto err_alloc_hdr;
	}

	ctrl->ts.common.hdr_buffer_v = ndp_ctrl_vmap_shadow(ctrl->hdr_buffer_size, ctrl->hdr_buffer);
	if (ctrl->ts.common.hdr_buffer_v == NULL) {
		goto err_vmap_hdr_buffer;
	}

	ret = nfb_char_register_mmap(channel->ndp->nfb, ctrl->hdr_buffer_size * 2, &ctrl->hdr_mmap_offset, ndp_ctrl_hdr_mmap, ctrl);
	if (ret) {
		goto err_register_mmap_hdr;
	}

	// Store read values of buffer sizes to structure variables
	ctrl->c.mdp = (ctrl->data_buffer_size/2 -1) & 0x0000FFFFul;
	ctrl->c.mhp = (ctrl->hdr_buffer_size/(2*NDP_CTRL_RX_CALYPTE_HDR_SIZE) -1) & 0x0000FFFFul;
	channel->ptrmask = ctrl->c.mhp;

	fdt_setprop_u32(fdt, node_offset, "protocol", 3);
	fdt_setprop_u32(fdt, node_offset, "data_buff_size", ctrl->data_buffer_size);
	fdt_setprop_u32(fdt, node_offset, "hdr_buff_size", ctrl->hdr_buffer_size);

	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_base", ctrl->hdr_mmap_offset);
	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_size", ctrl->hdr_buffer_size * 2);

	return 0;

err_register_mmap_hdr:
	vunmap(ctrl->ts.common.hdr_buffer_v);

err_vmap_hdr_buffer:
	dma_free_coherent(dev, ctrl->hdr_buffer_size,
			ctrl->hdr_buffer, ctrl->hdr_buffer_phys);
	ctrl->hdr_buffer = NULL;

err_alloc_hdr:
	return ret;
}

static void ndp_ctrl_medusa_detach_ring(struct ndp_channel *channel)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct device *dev = channel->ring.dev;

	if (ctrl->hdr_buffer) {
		nfb_char_unregister_mmap(channel->ndp->nfb, ctrl->hdr_mmap_offset);
		vunmap(ctrl->ts.common.hdr_buffer_v);
		dma_free_coherent(dev, ctrl->hdr_buffer_size, ctrl->hdr_buffer, ctrl->hdr_buffer_phys);
		ctrl->hdr_buffer = NULL;
	}

	if (ctrl->off_buffer) {
		vunmap(ctrl->off_buffer_v);
		dma_free_coherent(dev, ctrl->off_buffer_size, ctrl->off_buffer, ctrl->off_buffer_phys);
		ctrl->off_buffer = NULL;
	}

	if (ctrl->desc_buffer) {
		vunmap(ctrl->desc_buffer_v);
		dma_free_coherent(dev, ctrl->desc_buffer_size, ctrl->desc_buffer, ctrl->desc_buffer_phys);
		ctrl->desc_buffer = NULL;
	}

	if (ctrl->update_buffer) {
		dma_free_coherent(dev, ALIGN(NDP_CTRL_UPDATE_SIZE, PAGE_SIZE),
				ctrl->update_buffer, ctrl->update_buffer_phys);
		ctrl->update_buffer = NULL;
	}
}

static void ndp_ctrl_calypte_detach_ring(struct ndp_channel *channel)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct device *dev = channel->ring.dev;

	if (ctrl->hdr_buffer) {
		nfb_char_unregister_mmap(channel->ndp->nfb, ctrl->hdr_mmap_offset);
		vunmap(ctrl->ts.common.hdr_buffer_v);
		dma_free_coherent(dev, ctrl->hdr_buffer_size, ctrl->hdr_buffer, ctrl->hdr_buffer_phys);
		ctrl->hdr_buffer = NULL;
	}
}

static void ndp_ctrl_destroy(struct device *dev)
{
	struct ndp_channel *channel = container_of(dev, struct ndp_channel, dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	nc_ndp_ctrl_close(&ctrl->c);
	kfree(ctrl);
}

static struct ndp_channel_ops ndp_ctrl_tx_ops;
static struct ndp_channel_ops ndp_ctrl_rx_ops;

static struct ndp_channel *ndp_ctrl_create(struct ndp *ndp, struct ndp_channel_id id,
		const struct attribute_group **attrs, struct ndp_channel_ops *ops, int node_offset)
{
	int ret;
	const fdt32_t *prop;
	int proplen;
	int device_index;
	struct ndp_ctrl *ctrl;
	struct ndp_channel *channel;
	struct nfb_pci_device *pci_device;

	struct device *dev = NULL;
	size_t ndp_buffer_size;

	int is_medusa = (ops == &ndp_ctrl_rx_ops || ops == &ndp_ctrl_tx_ops);

	prop = fdt_getprop(ndp->nfb->fdt, node_offset, "pcie", &proplen);
	if (proplen >= sizeof(*prop)) {
		device_index = fdt32_to_cpu(*prop);

		list_for_each_entry(pci_device, &ndp->nfb->pci_devices, pci_device_list) {
			if (device_index == pci_device->index) {
				dev = &pci_device->pci->dev;
				break;
			}
		}
	}

	if (dev == NULL) {
		dev = &ndp->nfb->pci->dev;
		if (!ndp->dev_node_warn) {
			dev_warn(ndp->nfb->dev, "can't find exact pci_device for NDP queue, this can affect performance on NUMA systems\n");
			ndp->dev_node_warn = 1;
		}
	}

	ctrl = kzalloc_node(sizeof(*ctrl), GFP_KERNEL, dev_to_node(dev));
	if (ctrl == NULL) {
		ret = -ENOMEM;
		goto err_alloc_ctrl;
	}
	channel = &ctrl->channel;
	ndp_channel_init(channel, id);

	channel->dev.groups = attrs;
	channel->dev.release = ndp_ctrl_destroy;
	channel->ops = ops;
	channel->ring.dev = dev;

	ctrl->nfb = ndp->nfb;

	ret = nc_ndp_ctrl_open(ndp->nfb, node_offset, &ctrl->c);
	if (ret)
		goto err_ctrl_open;

	if (is_medusa) {
		/* Set initial parameters for ring */
		ndp_buffer_size = ndp_ctrl_buffer_size == 0 ? NDP_CTRL_DEFAULT_BUFFER_SIZE : ndp_ctrl_buffer_size,
		ndp_ctrl_medusa_req_block_update(ctrl, 0,
				ndp_buffer_size,
				ndp_ring_size / ndp_buffer_size,
				(id.index + 1) * ndp_ctrl_initial_offset);
	}

	return channel;

//	nc_ndp_ctrl_close(&ctrl->c);
err_ctrl_open:
	kfree(ctrl);
err_alloc_ctrl:
	return ERR_PTR(ret);
}

static struct ndp_channel_ops ndp_ctrl_rx_ops =
{
	.start = ndp_ctrl_medusa_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_rx_get_hwptr,
	.set_swptr = ndp_ctrl_medusa_rx_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_medusa_attach_ring,
	.detach_ring = ndp_ctrl_medusa_detach_ring,
	.get_free_space = NULL,
};

static struct ndp_channel_ops ndp_ctrl_tx_ops =
{
	.start = ndp_ctrl_medusa_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_medusa_tx_get_hwptr,
	.set_swptr = ndp_ctrl_medusa_tx_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_medusa_attach_ring,
	.detach_ring = ndp_ctrl_medusa_detach_ring,
	.get_free_space = NULL,
};

static struct ndp_channel_ops ndp_ctrl_calypte_rx_ops =
{
	.start = ndp_ctrl_calypte_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_rx_get_hwptr,
	.set_swptr = ndp_ctrl_calypte_rx_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_rx_calypte_attach_ring,
	.detach_ring = ndp_ctrl_calypte_detach_ring,
	.get_free_space = NULL,
};

static struct ndp_channel_ops ndp_ctrl_calypte_tx_ops =
{
	.start = ndp_ctrl_calypte_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_calypte_tx_get_hwptr,
	.set_swptr = ndp_ctrl_calypte_tx_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_tx_calypte_attach_ring,
	.detach_ring = ndp_ctrl_calypte_detach_ring,
	.get_free_space = ndp_ctrl_calypte_tx_get_free_space,
};

/* Attributes for sysfs - declarations */
static DEVICE_ATTR(ring_size,   (S_IRUGO | S_IWGRP | S_IWUSR), ndp_ctrl_get_ring_size, ndp_ctrl_set_ring_size);
static DEVICE_ATTR(buffer_size, (S_IRUGO | S_IWGRP | S_IWUSR), ndp_ctrl_get_buffer_size, ndp_ctrl_set_buffer_size);
static DEVICE_ATTR(buffer_count, (S_IRUGO | S_IWGRP | S_IWUSR), ndp_ctrl_get_buffer_count, ndp_ctrl_set_buffer_count);
static DEVICE_ATTR(initial_offset, (S_IRUGO | S_IWGRP | S_IWUSR), ndp_ctrl_get_initial_offset, ndp_ctrl_set_initial_offset);

static struct device_attribute dev_attr_calypte_ring_size = __ATTR(ring_size, (S_IRUGO | S_IWGRP | S_IWUSR), ndp_channel_get_ring_size, ndp_channel_set_ring_size);

static struct attribute *ndp_ctrl_rx_attrs[] = {
	&dev_attr_ring_size.attr,
	&dev_attr_buffer_size.attr,
	&dev_attr_buffer_count.attr,
	&dev_attr_initial_offset.attr,
	NULL,
};

static struct attribute *ndp_ctrl_tx_attrs[] = {
	&dev_attr_ring_size.attr,
	&dev_attr_buffer_size.attr,
	&dev_attr_buffer_count.attr,
	&dev_attr_initial_offset.attr,
	NULL,
};

static struct attribute_group ndp_ctrl_attr_rx_group = {
	.attrs = ndp_ctrl_rx_attrs,
};

static struct attribute_group ndp_ctrl_attr_tx_group = {
	.attrs = ndp_ctrl_tx_attrs,
};

static const struct attribute_group *ndp_ctrl_attr_rx_groups[] = {
	&ndp_ctrl_attr_rx_group,
	NULL,
};

static const struct attribute_group *ndp_ctrl_attr_tx_groups[] = {
	&ndp_ctrl_attr_tx_group,
	NULL,
};

static struct attribute *ndp_ctrl_calypte_rx_attrs[] = {
	&dev_attr_calypte_ring_size.attr,
	NULL,
};

static struct attribute *ndp_ctrl_calypte_tx_attrs[] = {
	&dev_attr_calypte_ring_size.attr,
	NULL,
};

static struct attribute_group ndp_ctrl_calypte_attr_rx_group = {
	.attrs = ndp_ctrl_calypte_rx_attrs,
};

static struct attribute_group ndp_ctrl_calypte_attr_tx_group = {
	.attrs = ndp_ctrl_calypte_tx_attrs,
};

static const struct attribute_group *ndp_ctrl_calypte_attr_rx_groups[] = {
	&ndp_ctrl_calypte_attr_rx_group,
	NULL,
};

static const struct attribute_group *ndp_ctrl_calypte_attr_tx_groups[] = {
	&ndp_ctrl_calypte_attr_tx_group,
	NULL,
};

struct ndp_channel *ndp_ctrl_v2_create_rx(struct ndp *ndp, int index, int node_offset)
{
	struct ndp_channel_id id = {.index = index, .type = NDP_CHANNEL_TYPE_RX};
	return ndp_ctrl_create(ndp, id, ndp_ctrl_attr_rx_groups, &ndp_ctrl_rx_ops, node_offset);
}

struct ndp_channel *ndp_ctrl_v2_create_tx(struct ndp *ndp, int index, int node_offset)
{
	struct ndp_channel_id id = {.index = index, .type = NDP_CHANNEL_TYPE_TX};
	return ndp_ctrl_create(ndp, id, ndp_ctrl_attr_tx_groups, &ndp_ctrl_tx_ops, node_offset);
}

struct ndp_channel *ndp_ctrl_v3_create_rx(struct ndp *ndp, int index, int node_offset)
{
	struct ndp_channel_id id = {.index = index, .type = NDP_CHANNEL_TYPE_RX};
	return ndp_ctrl_create(ndp, id, ndp_ctrl_calypte_attr_rx_groups, &ndp_ctrl_calypte_rx_ops, node_offset);
}

struct ndp_channel *ndp_ctrl_v3_create_tx(struct ndp *ndp, int index, int node_offset)
{
	struct ndp_channel_id id = {.index = index, .type = NDP_CHANNEL_TYPE_TX};
	return ndp_ctrl_create(ndp, id, ndp_ctrl_calypte_attr_tx_groups, &ndp_ctrl_calypte_tx_ops, node_offset);
}

module_param_cb(ndp_ctrl_buffer_size, &ndp_param_size_ops, &ndp_ctrl_buffer_size, S_IRUGO);
MODULE_PARM_DESC(ndp_ctrl_buffer_size, "Size of buffer for one packet in NDP ring (max size of RX/TX packet) [4096]");

module_param_cb(ndp_ctrl_initial_offset, &ndp_param_size_ops, &ndp_ctrl_initial_offset, S_IRUGO);
MODULE_PARM_DESC(ndp_ctrl_initial_offset, "Offset for the first buffer (packet) in ring in bytes; will be multiplied by (channel_index + 1) [64]");
