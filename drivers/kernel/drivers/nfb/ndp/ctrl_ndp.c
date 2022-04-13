/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP driver of the NFB platform - DMA controller - Medusa/v2 type
 *
 * Copyright (C) 2020-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/timer.h>

#include "../fdt/libfdt.h"
#include "../nfb.h"
#include "ndp.h"


#define NDP_DESC_UPPER_ADDR(addr) (((uint64_t)addr) & 0xFFFFFFFFc0000000ull)

#define NDP_CTRL_REG_CONTROL		0x00
	#define NDP_CTRL_REG_CONTROL_STOP	0x0
	#define NDP_CTRL_REG_CONTROL_START	0x1
#define NDP_CTRL_REG_STATUS		0x04
	#define NDP_CTRL_REG_STATUS_RUNNING	0x1
#define NDP_CTRL_REG_SDP		0x10
#define NDP_CTRL_REG_SHP		0x14
#define NDP_CTRL_REG_HDP		0x18
#define NDP_CTRL_REG_HHP		0x1c
#define NDP_CTRL_REG_TIMEOUT		0x20
#define NDP_CTRL_REG_DESC		0x40
#define NDP_CTRL_REG_HDR		0x48
#define NDP_CTRL_REG_UPDATE		0x50
#define NDP_CTRL_REG_MDP		0x58
#define NDP_CTRL_REG_MHP		0x5c

#define NDP_CTRL_HDP			(*ctrl->update_buffer)

#define NDP_CTRL_UPDATE_SIZE 	(4)

#define NDP_CTRL_TX_DESC_SIZE	(sizeof(struct ndp_desc))
#define NDP_CTRL_RX_DESC_SIZE	(sizeof(struct ndp_desc))
#define NDP_CTRL_RX_HDR_SIZE	(sizeof(struct ndp_hdr))

#define NDP_TX_BURST (64u)
#define NDP_CTRL_RX_DESC_BURST (64u)

#define NDP_CTRL_MODE_PACKET_SIMPLE	0 /* One desc per packet only, except desc type0 */
#define NDP_CTRL_MODE_STREAM		1 /* More packets in one descriptor with 8B padding */
#define NDP_CTRL_MODE_USER  		2 /* User provides descriptors in offset + header buffer */

#define virt_to_phys_shift(x) (virt_to_phys(x) >> PAGE_SHIFT)

#define NDP_CTRL_NEXT_SDP_AGE_MAX 16

typedef uint64_t ndp_offset_t;

static const int desc_size = 4096;

struct ndp_desc {
	union {
		struct __attribute__((__packed__)) {
			uint64_t phys : 34;
			uint32_t rsvd : 28;
			unsigned type : 2;
		} type0;
		struct __attribute__((__packed__)) {
			uint64_t phys : 30;
			int int0 : 1;
			int rsvd0 : 1;
			uint16_t len : 16;
			uint16_t meta : 12;
			int rsvd1 : 1;
			int next0 : 1;
			unsigned type : 2;
		} type2;
	};
};

struct ndp_hdr{
	uint16_t frame_len;
	uint8_t hdr_len;
	unsigned meta : 4;
	/* TODO: layout! */
	unsigned reserved: 2;
	unsigned free_desc : 2;
} __attribute((packed));

struct ndp_ctrl {
char __cacheline0[0];
	struct ndp_desc *next_desc;
	struct ndp_hdr  *next_hdr;
	uint64_t last_upper_addr;
	uint64_t mps_last_offset;

	struct ndp_desc *end_desc;
	struct ndp_hdr  *end_hdr;

	uint32_t mode;
	uint32_t free_desc;

	uint32_t sdp;
	uint32_t hdp;

char __cacheline1[0];
	uint32_t shp;
	uint32_t hhp;

	uint32_t mdp;
	uint32_t mhp;

	uint32_t *update_buffer;
	struct nfb_comp *comp;

	uint32_t _reserved0;
	uint32_t php; /* Pushed header pointer (converted to descriptors) */

	/* INFO: virtual memory: shadowed */
	struct ndp_desc *desc_buffer_v;
	struct ndp_hdr  *hdr_buffer_v;
	ndp_offset_t    *off_buffer_v;

char __other_cachelines[0]; /* Depends on the size of struct ndp_channel */
	uint32_t next_sdp;
	uint8_t next_sdp_age;

	uint32_t flags;

	struct ndp_channel channel;

	/* INFO: Use only for alloc / free */
	struct ndp_desc *desc_buffer;
	struct ndp_hdr  *hdr_buffer;
	ndp_offset_t    *off_buffer;
	resource_size_t update_buffer_phys;
	resource_size_t desc_buffer_phys;
	resource_size_t off_buffer_phys;
	resource_size_t hdr_buffer_phys;
	unsigned long desc_buffer_size;
	unsigned long off_buffer_size;
	unsigned long hdr_buffer_size;

	size_t hdr_mmap_offset;
	size_t off_mmap_offset;
};

struct ndp_packet {
	void *addr;
	uint16_t len;
};

/* Attributes for sysfs - declarations */
static DEVICE_ATTR(ring_size,   (S_IRUGO | S_IWGRP | S_IWUSR), ndp_channel_get_ring_size, ndp_channel_set_ring_size);

static struct attribute *ndp_ctrl_rx_attrs[] = {
	&dev_attr_ring_size.attr,
	NULL,
};

static struct attribute *ndp_ctrl_tx_attrs[] = {
	&dev_attr_ring_size.attr,
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

static inline struct ndp_desc ndp_rx_desc0(dma_addr_t phys)
{
	struct ndp_desc x;
	*((uint64_t *)&x) = (
			(0ull << 62) |
			((phys >> 30) /*& 0x3FFFFFFFFull*/)
	);
	return x;
}

static inline struct ndp_desc ndp_rx_desc2(dma_addr_t phys, uint16_t len, int next)
{
	struct ndp_desc x;
	*((uint64_t *)&x) = (
			(2ull << 62) |
			(next ? (1ull << 61) : 0) |
//			((((uint64_t)meta) & 0xFFF) << 48) |
			((((uint64_t)phys) & 0x3FFFFFFFull) << 0) |
			((((uint64_t)len) & 0xFFFF) << 32)
	);
	return x;
}

static inline struct ndp_desc ndp_tx_desc0(dma_addr_t phys)
{
	return ndp_rx_desc0(phys);
}

static inline struct ndp_desc ndp_tx_desc2(dma_addr_t phys, uint16_t len, uint16_t meta, int next)
{
	struct ndp_desc x;
	*((uint64_t *)&x) = (
			(2ull << 62) |
			(next ? (1ull << 61) : 0) |
			((((uint64_t)meta) & 0xFFF) << 48) |
			((((uint64_t)phys) & 0x3FFFFFFFull) << 0) |
			((((uint64_t)len) & 0xFFFF) << 32)
	);
	return x;
}

int ndp_ctrl_v2_get_vmaps(struct ndp_channel *channel, void **hdr, void **off)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	*hdr = ctrl->hdr_buffer_v;
	*off = ctrl->off_buffer_v;
	return ctrl->mhp + 1;
}

static inline void ndp_ctrl_hdp_update(struct ndp_ctrl *ctrl)
{
	rmb();
	ctrl->hdp = ((uint32_t*) ctrl->update_buffer)[0];
}

static inline void ndp_ctrl_hhp_update(struct ndp_ctrl *ctrl)
{
	rmb();
	ctrl->hhp = ((uint32_t*) ctrl->update_buffer)[1];
}

static void ndp_ctrl_mps_fill_rx_descs(struct ndp_ctrl *ctrl, uint64_t count)
{
	int i;
	struct ndp_desc *desc = ctrl->desc_buffer_v + ctrl->sdp;
	ssize_t block_size = ctrl->channel.ring.blocks[0].size;

	/* TODO: Table of prepared descriptor bursts with all meta (desc. count etc) */
	for (i = 0; i < count; i++) {
		dma_addr_t addr;
		addr = ctrl->channel.ring.blocks[ctrl->mps_last_offset / block_size].phys;
		addr += ctrl->mps_last_offset % block_size;

		if (unlikely(NDP_DESC_UPPER_ADDR(addr) != ctrl->last_upper_addr)) {
			ctrl->last_upper_addr = NDP_DESC_UPPER_ADDR(addr);
			desc[i] = ndp_rx_desc0(addr);
			continue;
		}
		desc[i] = ndp_rx_desc2(addr, desc_size, 0);
		ctrl->mps_last_offset += desc_size;
		if (ctrl->mps_last_offset >= ctrl->channel.ring.size)
			ctrl->mps_last_offset = 0;
	}
	wmb();
	ctrl->sdp = (ctrl->sdp + count) & ctrl->mdp;
}

static void ndp_ctrl_user_fill_rx_descs(struct ndp_ctrl *ctrl)
{
	int i,j;
	int count;

	ndp_offset_t *off = ctrl->off_buffer_v + ctrl->php;
	struct ndp_hdr *hdr = ctrl->hdr_buffer_v + ctrl->php;
	struct ndp_desc *desc = ctrl->desc_buffer_v + ctrl->next_sdp;
	uint32_t sdp_shift;

	count = (ctrl->shp - ctrl->php) & ctrl->mhp;

	ctrl->free_desc = (ctrl->hdp - ctrl->next_sdp - 1) & ctrl->mhp;

	j = 0;
	for (i = 0; i < count; i++) {
		dma_addr_t addr = off[i];

		if (unlikely(NDP_DESC_UPPER_ADDR(addr) != ctrl->last_upper_addr)) {
			if (unlikely(ctrl->free_desc == 0)) {
				break;
			}

			ctrl->last_upper_addr = NDP_DESC_UPPER_ADDR(addr);
			desc[j] = ndp_rx_desc0(addr);
			ctrl->free_desc--;
			j++;
		}

		if (unlikely(ctrl->free_desc == 0)) {
			break;
		}

		desc[j] = ndp_rx_desc2(addr, hdr[i].frame_len, 0);
		ctrl->free_desc--;
		j++;
	}

	if (i == 0) {
		return;
	}

	wmb();

	ctrl->next_sdp = (ctrl->next_sdp + j) & ctrl->mdp;

	ctrl->php = (ctrl->php + i) & ctrl->mhp;

	sdp_shift = (ctrl->next_sdp - ctrl->sdp) & ctrl->mdp;

	if (j == 0 && sdp_shift != 0)
		ctrl->next_sdp_age++;
	else
		ctrl->next_sdp_age = 0;

	if (unlikely(ctrl->next_sdp_age == NDP_CTRL_NEXT_SDP_AGE_MAX)) {
		// SDP has been waiting to shift by a whole burst for quite some time
		// shift now, ignoring burst size, to avoid deadlock
		ctrl->sdp = (ctrl->sdp + sdp_shift) & ctrl->mdp;
		nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP,
				ctrl->sdp | (((uint64_t) ctrl->shp) << 32));
	} else {
		sdp_shift = (sdp_shift / NDP_CTRL_RX_DESC_BURST) * NDP_CTRL_RX_DESC_BURST;

		if (sdp_shift) {
			ctrl->sdp = (ctrl->sdp + sdp_shift) & ctrl->mdp;
			nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP,
					ctrl->sdp | (((uint64_t) ctrl->shp) << 32));
		}
	}
}

static void ndp_ctrl_rx_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	struct ndp_hdr *hdr = ctrl->hdr_buffer_v + ctrl->shp;
//	struct ndp_desc *desc_buffer = ctrl->desc_buffer_v + ctrl->sdp;

	if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
		int i;
		int count;
		int free_desc = 0;
		int free_desc2 = 0;

		count = (ptr - ctrl->shp) & ctrl->mhp;
		for (i = 0; i < count; i++) {
			/* Expecting only 1 or 2 free desc for each packet/header */
			if (hdr[i].free_desc == 1) {
				free_desc++;
			} else if (hdr[i].free_desc == 2) {
				free_desc2++;
			}
		}
		ctrl->free_desc += free_desc + free_desc2 * 2;
		ctrl->shp = ptr;

		count = 0;
		while (ctrl->free_desc >= NDP_CTRL_RX_DESC_BURST) {
			ndp_ctrl_mps_fill_rx_descs(ctrl, NDP_CTRL_RX_DESC_BURST);
			ctrl->free_desc -= NDP_CTRL_RX_DESC_BURST;
			count = 1;
		}
		if (count) {
			nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP,
					ctrl->sdp | (((uint64_t) ctrl->shp) << 32));
		}
	} else if (ctrl->mode == NDP_CTRL_MODE_STREAM) {
		/* TODO */
	} else if (ctrl->mode == NDP_CTRL_MODE_USER) {
		ctrl->shp = ptr;
		ndp_ctrl_hdp_update(ctrl);
		ndp_ctrl_user_fill_rx_descs(ctrl);
	}
}

static uint64_t ndp_ctrl_rx_get_hwptr(struct ndp_channel *channel)
{
	int i;
	int count;
	int hhp;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	hhp = ctrl->hhp;
	ndp_ctrl_hhp_update(ctrl);

	count = (ctrl->hhp - hhp) & ctrl->mhp;

	if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
		/* Constant packet offsets in this mode */
	} else if (ctrl->mode == NDP_CTRL_MODE_STREAM) {
		struct ndp_hdr *hdr = ctrl->hdr_buffer_v + hhp;
		ndp_offset_t *off = ctrl->off_buffer_v + hhp;
		for (i = 0; i < count; i++) {
			off[i+1] = off[i] + hdr[i].frame_len;
		}
		off[i] &= channel->ring.size - 1;
	} else if (ctrl->mode == NDP_CTRL_MODE_USER) {
		/* Check if some descs from userspace can be written */
		if (count && ctrl->php != ctrl->shp) {
			ndp_ctrl_hdp_update(ctrl);
			ndp_ctrl_user_fill_rx_descs(ctrl);
		}
	}
	return ctrl->hhp;
}

static uint64_t ndp_ctrl_tx_get_hwptr(struct ndp_channel *channel);

static void ndp_ctrl_tx_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	int i,j;
	int count;
	int dirty = 0;

	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct ndp_desc *desc = ctrl->desc_buffer_v + ctrl->sdp;
	ssize_t block_size = channel->ring.blocks[0].size;
	wmb();

	count = (ptr - ctrl->shp) & ctrl->mhp;

	j = 0;
	for (i = 0; i < count; i++) {
		dma_addr_t addr;
		ndp_offset_t *off;
		struct ndp_hdr *hdr;

		off = ctrl->off_buffer_v + ctrl->shp + i;
		hdr = ctrl->hdr_buffer_v + ctrl->shp + i;

		if (ctrl->mode == NDP_CTRL_MODE_USER) {
			addr = *off;
		} else {
			addr = channel->ring.blocks[*off / block_size].phys;
			addr += *off % block_size;
		}
		if (unlikely(NDP_DESC_UPPER_ADDR(addr) != ctrl->last_upper_addr)) {
			while (ctrl->free_desc == 0) {
				usleep_range(10, 50);
				ndp_ctrl_tx_get_hwptr(channel);

				if (unlikely(ndp_kill_signal_pending(current))) {
					dirty = 1;
					break;
				}
			}
			if (unlikely(dirty)) {
				break;
			}
			ctrl->last_upper_addr = NDP_DESC_UPPER_ADDR(addr);
			desc[j] = ndp_tx_desc0(addr);
			ctrl->sdp++;
			ctrl->free_desc--;
			j++;
		}

		while (ctrl->free_desc == 0) {
			usleep_range(10, 50);
			ndp_ctrl_tx_get_hwptr(channel);
			if (ndp_kill_signal_pending(current)) {
				dirty = 1;
				break;
			}
		}

		desc[j] = ndp_tx_desc2(addr, hdr->frame_len, hdr->meta, 0);
		ctrl->sdp++;
		ctrl->free_desc--;
		j++;
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
	ctrl->sdp &= ctrl->mdp;
	ctrl->shp = ptr;
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_SDP, ctrl->sdp);
}

static uint64_t ndp_ctrl_tx_get_hwptr(struct ndp_channel *channel)
{
	int hdp;
	int i, count, free_hdrs = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct ndp_desc *desc;

	rmb();
	hdp = ctrl->hdp;
	ndp_ctrl_hdp_update(ctrl);
	count = (ctrl->hdp - hdp) & ctrl->mdp;
	ctrl->free_desc += count;

	desc = ctrl->desc_buffer_v + hdp;
	for (i = 0; i < count; i++) {
		if (desc[i].type2.type == 2)
			free_hdrs++;

	}
	ctrl->hhp = (ctrl->hhp + free_hdrs) & ctrl->mhp;

	return ctrl->hhp;
}

static uint64_t ndp_ctrl_get_flags(struct ndp_channel *channel)
{
	uint64_t ret = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	ret |= NDP_CHANNEL_FLAG_USE_HEADER;
	ret |= NDP_CHANNEL_FLAG_USE_OFFSET;

	ret |= ctrl->flags;

	return ret;
}

static uint64_t ndp_ctrl_set_flags(struct ndp_channel *channel, uint64_t flags)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (flags & NDP_CHANNEL_FLAG_NO_BUFFER)
		ctrl->flags |= NDP_CHANNEL_FLAG_NO_BUFFER;
	else
		ctrl->flags &= ~NDP_CHANNEL_FLAG_NO_BUFFER;

	return ndp_ctrl_get_flags(channel);
}

static int ndp_ctrl_start(struct ndp_channel *channel, uint64_t *hwptr)
{
	uint32_t status;
	unsigned int param;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	status = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_STATUS);
	if (status & NDP_CTRL_REG_STATUS_RUNNING) {
		dev_warn(ctrl->comp->nfb->dev, "NDP queue %s is in dirty state, can't be started\n",
			dev_name(&channel->dev));
		return -1;
	}

	/* Set pointers to zero */
	ctrl->sdp = 0;
	ctrl->hdp = 0;
	ctrl->shp = 0;
	ctrl->hhp = 0;
	ctrl->next_sdp = 0;
	ctrl->update_buffer[0] = 0;
	ctrl->update_buffer[1] = 0;

	/* TODO */
	ctrl->last_upper_addr = 0xFFFFFFFFFFFFFFFFull;

	ctrl->mps_last_offset = 0;

	/* Set address of first descriptor */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_DESC, ctrl->desc_buffer_phys);

	/* Set address of RAM hwptr address */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_UPDATE, ctrl->update_buffer_phys);

	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_HDR, ctrl->hdr_buffer_phys);

	/* Set buffer size (mask) */
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_MDP, ctrl->mdp);
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_MHP, ctrl->mhp);

	/* Zero both buffer ptrs */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP, 0);

	/* Timeout */
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_TIMEOUT, 0x4000);

	/* Start controller */
	param  = NDP_CTRL_REG_CONTROL_START;
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_CONTROL, param);

	if (ctrl->flags & NDP_CHANNEL_FLAG_NO_BUFFER) {
		ctrl->mode = NDP_CTRL_MODE_USER;
	} else {
		ctrl->mode = NDP_CTRL_MODE_PACKET_SIMPLE;
	}

	if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
		int i;
		ndp_offset_t *off;

		/* Constant packet offsets in this mode */
		for (i = 0, off = ctrl->off_buffer_v; i < ctrl->desc_buffer_size / NDP_CTRL_RX_DESC_SIZE; i++, off++) {
			*off = i * desc_size;
		}
	} else if (ctrl->mode == NDP_CTRL_MODE_USER) {
		if (channel->id.type == NDP_CHANNEL_TYPE_RX) {
			ctrl->free_desc = ctrl->mhp;
			ctrl->php = 0;
		}
	}

	if (channel->id.type == NDP_CHANNEL_TYPE_RX) {
		if (ctrl->mode == NDP_CTRL_MODE_PACKET_SIMPLE) {
			ndp_ctrl_mps_fill_rx_descs(ctrl, ctrl->mdp + 1 - NDP_CTRL_RX_DESC_BURST);
			nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_SDP, ctrl->sdp);
			ctrl->free_desc = 0;
		}

		/* TODO: Check if SHP is to be 0 after start or must first set to an value */
	} else if (channel->id.type == NDP_CHANNEL_TYPE_TX) {
		ctrl->free_desc = ctrl->mdp;
	}
	*hwptr = 0;
	return 0;
}

static void ndp_ctrl_stop(struct ndp_channel *channel)
{
	int dirty = 0;
	unsigned int counter = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (channel->id.type == NDP_CHANNEL_TYPE_TX) {
		while (1) {
			ndp_ctrl_hdp_update(ctrl);
			if (ctrl->sdp == ctrl->hdp)
				break;
			if (ndp_kill_signal_pending(current)) {
				dev_warn(ctrl->comp->nfb->dev,
						"NDP queue %s has not completed all data transfers. "
						"Transfers aborted by users, queue is in dirty state.\n",
						dev_name(&channel->dev));
				dirty = 1;
				break;
			}

			msleep(10);
		}
	}

	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_CONTROL,
			NDP_CTRL_REG_CONTROL_STOP);

	while (1) {
		uint32_t status;
		status = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_STATUS);
		if (dirty || !(status & NDP_CTRL_REG_STATUS_RUNNING))
			break;
		if (counter++ > 100) {
			dev_warn(ctrl->comp->nfb->dev,
					"NDP queue %s did not stop in 1 sec. "
					"This may be due to hardware/firmware error.\n",
					dev_name(&channel->dev));
			break;
		}
		msleep(10);
	}
}

static int ndp_ctrl_hdr_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv)
{
	int ret;
	struct ndp_ctrl *ctrl = (struct ndp_ctrl*) priv;

	/* TODO: Check if channel is subscribed */

	/* Check permissions: read-only for RX */
	if ((ctrl->flags & NDP_CHANNEL_FLAG_NO_BUFFER) == 0 && ctrl->channel.id.type == NDP_CHANNEL_TYPE_RX && (vma->vm_flags & (VM_WRITE | VM_READ)) != VM_READ)
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
	if ((ctrl->flags & NDP_CHANNEL_FLAG_NO_BUFFER) == 0 && ctrl->channel.id.type == NDP_CHANNEL_TYPE_RX && (vma->vm_flags & (VM_WRITE | VM_READ)) != VM_READ)
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

void *ndp_ctrl_vmap_shadow(int size, void *virt)
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

static int ndp_ctrl_attach_ring(struct ndp_channel *channel)
{
	int ret = -ENOMEM;
	int desc_count;
	int hdr_count;
	int node_offset;
	void *fdt = channel->ndp->nfb->fdt;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	struct device *dev = channel->ring.dev;


	if (channel->ring.size == 0)
		return -EINVAL;

	desc_count = channel->ring.size / desc_size;
	if (desc_count * min(NDP_CTRL_RX_DESC_SIZE, NDP_CTRL_RX_HDR_SIZE) < PAGE_SIZE) {
		/* Can't do shadow-map for this ring size */
		return -EINVAL;
	}

	//dev_info(channel->ring.dev, "alloc %d", desc_count);
	hdr_count = desc_count;

	ctrl->mhp = hdr_count - 1;
	ctrl->mdp = desc_count - 1;

	channel->ptrmask = hdr_count - 1;

	ctrl->update_buffer = dma_alloc_coherent(dev, ALIGN(NDP_CTRL_UPDATE_SIZE, PAGE_SIZE),
			&ctrl->update_buffer_phys, GFP_KERNEL);
	if (ctrl->update_buffer == NULL) {
		goto err_alloc_update;
	}

	/* allocate descriptor area */
	ctrl->desc_buffer_size = ALIGN(desc_count * NDP_CTRL_RX_DESC_SIZE, PAGE_SIZE);
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
	ctrl->off_buffer_size = ALIGN(hdr_count * sizeof(ndp_offset_t), PAGE_SIZE);
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
	ctrl->hdr_buffer_size = ALIGN(hdr_count * NDP_CTRL_RX_HDR_SIZE, PAGE_SIZE);
	ctrl->hdr_buffer = dma_alloc_coherent(dev, ctrl->hdr_buffer_size,
			&ctrl->hdr_buffer_phys, GFP_KERNEL);
	if (ctrl->hdr_buffer == NULL) {
		goto err_alloc_hdr;
	}

	ctrl->hdr_buffer_v = ndp_ctrl_vmap_shadow(ctrl->hdr_buffer_size, ctrl->hdr_buffer);
	if (ctrl->hdr_buffer_v == NULL) {
		goto err_vmap_hdr_buffer;
	}

	ret = nfb_char_register_mmap(channel->ndp->nfb, ctrl->hdr_buffer_size * 2, &ctrl->hdr_mmap_offset, ndp_ctrl_hdr_mmap, ctrl);
	if (ret) {
		goto err_register_mmap_hdr;
	}

	node_offset = fdt_path_offset(fdt, channel->id.type == NDP_CHANNEL_TYPE_TX ?
				"/drivers/ndp/tx_queues" : "/drivers/ndp/rx_queues");
	node_offset = fdt_subnode_offset(fdt, node_offset, dev_name(&channel->dev));

	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_base", ctrl->hdr_mmap_offset);
	fdt_setprop_u64(fdt, node_offset, "hdr_mmap_size", ctrl->hdr_buffer_size * 2);

	fdt_setprop_u64(fdt, node_offset, "off_mmap_base", ctrl->off_mmap_offset);
	fdt_setprop_u64(fdt, node_offset, "off_mmap_size", ctrl->off_buffer_size * 2);

	return 0;

	//nfb_char_unregister_mmap(channel->ndp->nfb, ctrl->hdr_mmap_offset);
err_register_mmap_hdr:
	vunmap(ctrl->hdr_buffer_v);
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

static void ndp_ctrl_detach_ring(struct ndp_channel *channel)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	struct device *dev = channel->ring.dev;

	if (ctrl->hdr_buffer) {
		nfb_char_unregister_mmap(channel->ndp->nfb, ctrl->hdr_mmap_offset);
		vunmap(ctrl->hdr_buffer_v);
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

static void ndp_ctrl_destroy(struct device *dev)
{
	struct ndp_channel *channel = container_of(dev, struct ndp_channel, dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	nfb_comp_close(ctrl->comp);
	kfree(ctrl);
}

static struct ndp_channel *ndp_ctrl_create(struct ndp *ndp, struct ndp_channel_id id,
		const struct attribute_group **attrs, struct ndp_channel_ops *ops, int node_offset)
{
	int ret;
	const fdt32_t *prop;
	int proplen;
	int device_index;
	struct ndp_ctrl *ctrl;
	struct nfb_pci_device *pci_device;

	struct device *dev = &ndp->nfb->pci->dev;

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

	ctrl = kzalloc_node(sizeof(*ctrl), GFP_KERNEL, dev_to_node(dev));
	if (ctrl == NULL) {
		ret = -ENOMEM;
		goto err_alloc_ctrl;
	}
	ndp_channel_init(&ctrl->channel, id);

	ctrl->channel.dev.groups = attrs;
	ctrl->channel.dev.release = ndp_ctrl_destroy;
	ctrl->channel.ops = ops;
	ctrl->channel.ring.dev = dev;

	ctrl->comp = nfb_comp_open(ndp->nfb, node_offset);
	if (!ctrl->comp) {
		ret = -ENODEV;
		goto err_nfb_comp_open;
	}

	return &ctrl->channel;

//	nfb_comp_close(ctrl->comp);
err_nfb_comp_open:
	kfree(ctrl);
err_alloc_ctrl:
	return ERR_PTR(ret);
}

static struct ndp_channel_ops ndp_ctrl_rx_ops =
{
	.start = ndp_ctrl_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_rx_get_hwptr,
	.set_swptr = ndp_ctrl_rx_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_attach_ring,
	.detach_ring = ndp_ctrl_detach_ring,
};

static struct ndp_channel_ops ndp_ctrl_tx_ops =
{
	.start = ndp_ctrl_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_tx_get_hwptr,
	.set_swptr = ndp_ctrl_tx_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_attach_ring,
	.detach_ring = ndp_ctrl_detach_ring,
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
