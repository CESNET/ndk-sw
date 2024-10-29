/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - DMA controller - SZE/v1 type
 *
 * Copyright (C) 2017-2022 CESNET
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
#include "ndp.h"
#include "../nfb.h"

#include <netcope/dma_ctrl_sze.h>

#define SZE_CTRL_REG_IRQ_PTR(p)	((p) & ~0x3)
#define SZE_CTRL_REG_TIMEOUT_NS(ns)	((ns) / 5)

struct ndp_ctrl {
	struct nfb_comp* comp;
	struct ndp_channel channel;

	void *descriptor_ptr;
	resource_size_t descriptor_phys;
	unsigned long desc_size;

	void *update_ptr;
	resource_size_t update_phys;

	unsigned long swptr;
	unsigned long hwptr;

	int flags;
	size_t initial_offset;
};

static unsigned int timeout = 10000;

static const size_t MAX_DESC_SPACE = 4096 * 1024;

unsigned ndp_ctrl_rx_request_size = 256;
unsigned ndp_ctrl_tx_request_size = 512;

/* Attributes for sysfs - declarations */
static DEVICE_ATTR(ring_size,   (S_IRUGO | S_IWGRP | S_IWUSR), ndp_channel_get_ring_size, ndp_channel_set_ring_size);
static DEVICE_ATTR(discard,     (S_IRUGO | S_IWGRP | S_IWUSR), ndp_channel_get_discard, ndp_channel_set_discard);

static struct attribute *ndp_ctrl_rx_attrs[] = {
	&dev_attr_ring_size.attr,
	&dev_attr_discard.attr,
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

/*
 * Descriptor format
 *
 * NDP hardware controllers needs a descriptor area, which describes NDP ring.
 * All descriptors must be prepared before issuing a start command.
 * Hardware preloads them on start for no-lag transfers.
 *
 * We have two descriptor types:
 * Type 0 (direct): Use this type to let hardware know on which address is the part of ring
 * - | 63 - 12 | physical address without lowest 12 bits (the area must be page aligned)
 * - | 11 -  1 | size of continous described area in pages minus 1 (e.g. for 4kB: value 0, for 4MB: value 1023)
 * - |       0 | descriptor type: value 0
 * Type 1 (pointer): Use this type to let hardware read descriptors at another address.
 * - | 63 -  1 | physical address of next part of descriptor area
 * - |       0 | descriptor type: value 1
 *
 */

/*
 * Initial offset feature
 *
 * Memory controller in CPU has significant throughput loss, when the (write) requests have some address bits equal.
 * This feature increases performance in case, when all channel hardware pointers are synchronized
   (e.g. the hardware uses round-robin channel distribution with discarding disabled).
 * This feature adds a variable initial offset for each channel.
 * This feature is hardware independent and works in two steps:
 * 1. Descriptor array is split to two stages.
 *    First stage describes the buffer up from specified offset and is used by hardware controller just once on start.
 *    Then the controller continues using descriptor from second stage, in the same way as previous mechanism (with descriptor pointer loop to beginning of second stage).
 * 2. Software / hardware pointer values in controller are not modified (begins still from 0), therefore must be these values shifted for software.
 *
 */

static int ndp_ctrl_desc_ring(struct ndp_ctrl *ctrl, uint64_t *desc, size_t initial_offset)
{
	int i;
	size_t size;
	size_t block_size;
	uint64_t flags;
	struct ndp_ring *ring;
	dma_addr_t phys;
	unsigned int desc_count = 0;

	ring = &ctrl->channel.ring;
	for (i = 0; i < ring->block_count; i++) {
		phys = ring->blocks[i].phys;
		block_size = ring->blocks[i].size;
		while (block_size) {
			size = min(block_size, MAX_DESC_SPACE);

			if (initial_offset) {
				/* Initial offset in action:
				 * skip this part of buffer without descriptor assignment */
				size = min(size, initial_offset);
				initial_offset -= size;
			} else {
				if (desc) {
					flags = (size / PAGE_SIZE - 1) << 1;
					*(desc++) = cpu_to_le64(phys | flags);
				}
				desc_count++;
			}

			block_size -= size;
			phys += size;
		}
	}

	return desc_count;
}

static int ndp_ctrl_desc(struct ndp_ctrl *ctrl, uint64_t *desc)
{
	unsigned int desc_count = 0;
	int second_stage_offset = 0;

	/* first stage: use initial offset */
	if (ctrl->initial_offset) {
		desc_count += ndp_ctrl_desc_ring(ctrl, desc, ctrl->initial_offset);
		second_stage_offset = desc_count * 8;
	}

	/* Second stage: loop (without initial offset) */
	desc_count += ndp_ctrl_desc_ring(ctrl, desc ? desc + desc_count : NULL, 0);

	/* one descriptor for round-trip */
	if (desc) {
		*(desc + desc_count) = cpu_to_le64((ctrl->descriptor_phys + second_stage_offset) | SZE_CTRL_DESC_PTR);
	}
	desc_count++;

	return desc_count;
}

static uint64_t ndp_ctrl_get_hwptr(struct ndp_channel *channel)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	rmb();
	ctrl->hwptr = *((uint64_t*) ctrl->update_ptr);
	ctrl->hwptr = (ctrl->hwptr + ctrl->initial_offset) & channel->ptrmask;
	return ctrl->hwptr;
}

static void ndp_ctrl_set_swptr(struct ndp_channel *channel, uint64_t ptr)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);
	ptr = (ptr - ctrl->initial_offset) & channel->ptrmask;
	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_SW_POINTER, ptr);
	ctrl->swptr = ptr;
}

static int ndp_ctrl_start(struct ndp_channel *channel, uint64_t *hwptr)
{
	uint32_t status;
	unsigned int param;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	status = nfb_comp_read32(ctrl->comp, SZE_CTRL_REG_STATUS);
	if (status & SZE_CTRL_REG_STATUS_RUNNING) {
		dev_warn(ctrl->comp->nfb->dev, "NDP queue %s is in dirty state, can't be started\n",
			dev_name(&channel->dev));
		return -1;
	}

	/* Set pointers to zero */
	ctrl->swptr = 0;

	ctrl->hwptr = 0;
	*((uint64_t*) ctrl->update_ptr) = 0;

	/* Set address of first descriptor */
	nfb_comp_write64(ctrl->comp, SZE_CTRL_REG_DESC_BASE, ctrl->descriptor_phys);

	/* Set address of RAM hwptr address */
	nfb_comp_write64(ctrl->comp, SZE_CTRL_REG_UPDATE_BASE, ctrl->update_phys);

	/* Set buffer size (mask) */
	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_BUFFER_SIZE, channel->ptrmask);

	/* Zero buffer ptr */
	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_SW_POINTER, 0);

	/* Timeout */
	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_TIMEOUT, timeout);

	/* Max size */
	param = channel->id.type == NDP_CHANNEL_TYPE_RX ? ndp_ctrl_rx_request_size : ndp_ctrl_tx_request_size;
	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_MAX_REQUEST, param);

	/* Start controller */
	param  = SZE_CTRL_REG_CONTROL_START;
	param |= (ctrl->flags & NDP_CHANNEL_FLAG_DISCARD) ? SZE_CTRL_REG_CONTROL_DISCARD : 0;
	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_CONTROL, param);

	*hwptr = ctrl->initial_offset;
	return 0;
}

static uint64_t ndp_ctrl_get_flags(struct ndp_channel *channel)
{
	uint64_t ret = 0;
	uint32_t reg;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	reg = nfb_comp_read32(ctrl->comp, SZE_CTRL_REG_CONTROL);
	if (reg & SZE_CTRL_REG_CONTROL_DISCARD)
		ret |= NDP_CHANNEL_FLAG_DISCARD;

	return ret;
}

static uint64_t ndp_ctrl_set_flags(struct ndp_channel *channel, uint64_t flags)
{
	uint64_t ret;
	uint32_t reg, regwr;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	ctrl->flags = flags;

	regwr = reg = nfb_comp_read32(ctrl->comp, SZE_CTRL_REG_CONTROL);
	regwr &= ~(SZE_CTRL_REG_CONTROL_DISCARD);

	regwr |= (ctrl->flags & NDP_CHANNEL_FLAG_DISCARD) ? SZE_CTRL_REG_CONTROL_DISCARD : 0;

	if (reg != regwr)
		nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_CONTROL, regwr);

	ret = flags;
	ret &= ~NDP_CHANNEL_FLAG_DISCARD;
	return ret;
}

static int ndp_ctrl_stop(struct ndp_channel *channel, int force)
{
	uint64_t s, e;
	int dirty = 0;
	unsigned int cnt = 0;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (channel->id.type == NDP_CHANNEL_TYPE_TX) {
		dirty = 1;
		while (cnt < 10 || (!ndp_kill_signal_pending(current) && !force)) {
			s = *((uint64_t *)ctrl->update_ptr);
			e = ctrl->swptr;
			if (s == e) {
				dirty = 0;
				break;
			} else if (/*ret == -EAGAIN && */!force) {
				return -EAGAIN;
			}

			msleep(10);
			cnt++;
		}
		if (dirty) {
			dev_warn(ctrl->comp->nfb->dev,
					"NDP queue %s has not completed all data transfers. "
					"Transfers aborted by users, queue is in dirty state.\n",
					dev_name(&channel->dev));
		}
	}

	nfb_comp_write32(ctrl->comp, SZE_CTRL_REG_CONTROL,
			SZE_CTRL_REG_CONTROL_STOP | SZE_CTRL_REG_CONTROL_DISCARD);

	if (channel->id.type == NDP_CHANNEL_TYPE_RX) {
		ndp_ctrl_set_swptr(channel, ndp_ctrl_get_hwptr(channel));
	}

	cnt = 0;
	while (1) {
		uint32_t status;
		status = nfb_comp_read32(ctrl->comp, SZE_CTRL_REG_STATUS);
		if (dirty || !(status & SZE_CTRL_REG_STATUS_RUNNING))
			break;
		if (cnt++ > 100) {
			dev_warn(ctrl->comp->nfb->dev,
					"NDP queue %s did not stop in 1 sec. "
					"This may be due to hardware/firmware error.\n",
					dev_name(&channel->dev));
			break;
		}
		msleep(10);
	}
	return 0;
}

static int ndp_ctrl_attach_ring(struct ndp_channel *channel)
{
	int ret;
	int desc_count;
	int node_offset;
	void *fdt = channel->ndp->nfb->fdt;
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (channel->ring.block_count == 0)
		return 0;

	ctrl->update_ptr = dma_alloc_coherent(channel->ring.dev, SZE_CTRL_UPDATE_SIZE,
			&ctrl->update_phys, GFP_KERNEL);
	if (ctrl->update_ptr == NULL) {
		ret = -ENOMEM;
		goto err_alloc_update;
	}

	/* Compute initial_offset value */
	ctrl->initial_offset = (channel->id.index * PAGE_SIZE) % channel->ring.size;

	desc_count = ndp_ctrl_desc(ctrl, NULL);
	if (desc_count <= 0) {
		ret = -ENOMEM;
		goto err_desc_count;
	}


	/* allocate descriptor & status area */
	ctrl->desc_size = ALIGN(desc_count * 8, PAGE_SIZE);

	ctrl->descriptor_ptr = dma_alloc_coherent(channel->ring.dev, ctrl->desc_size,
			&ctrl->descriptor_phys, GFP_KERNEL);
	if (ctrl->descriptor_ptr == NULL) {
		ret = -ENOMEM;
		goto err_alloc_desc;
	}

	node_offset = fdt_path_offset(fdt, channel->id.type == NDP_CHANNEL_TYPE_TX ?
				"/drivers/ndp/tx_queues" : "/drivers/ndp/rx_queues");
	node_offset = fdt_subnode_offset(fdt, node_offset, dev_name(&channel->dev));
	fdt_setprop_u32(fdt, node_offset, "protocol", 1);

	/* fill descs */
	ndp_ctrl_desc(ctrl, ctrl->descriptor_ptr);

	channel->ptrmask = channel->ring.size - 1;

	return 0;

err_alloc_desc:
err_desc_count:
	dma_free_coherent(channel->ring.dev, SZE_CTRL_UPDATE_SIZE,
			ctrl->update_ptr, ctrl->update_phys);
err_alloc_update:
	return ret;
}

static void ndp_ctrl_detach_ring(struct ndp_channel *channel)
{
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	if (channel->ring.block_count == 0)
		return;

	if (ctrl->descriptor_ptr) {
		dma_free_coherent(channel->ring.dev, ctrl->desc_size,
				ctrl->descriptor_ptr, ctrl->descriptor_phys);
		ctrl->descriptor_ptr = NULL;
	}

	if (ctrl->update_ptr) {
		dma_free_coherent(channel->ring.dev, SZE_CTRL_UPDATE_SIZE,
				ctrl->update_ptr, ctrl->update_phys);
		ctrl->update_ptr = NULL;
	}
}

static void ndp_ctrl_destroy(struct device *dev)
{
	struct ndp_channel *channel = container_of(dev, struct ndp_channel, dev);
	struct ndp_ctrl *ctrl = container_of(channel, struct ndp_ctrl, channel);

	nfb_comp_close(ctrl->comp);
	kfree(ctrl);
}

static struct ndp_channel_ops ndp_ctrl_rx_ops =
{
	.start = ndp_ctrl_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_get_hwptr,
	.set_swptr = ndp_ctrl_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_attach_ring,
	.detach_ring = ndp_ctrl_detach_ring,
};

static struct ndp_channel_ops ndp_ctrl_tx_ops =
{
	.start = ndp_ctrl_start,
	.stop = ndp_ctrl_stop,
	.get_hwptr = ndp_ctrl_get_hwptr,
	.set_swptr = ndp_ctrl_set_swptr,
	.get_flags = ndp_ctrl_get_flags,
	.set_flags = ndp_ctrl_set_flags,
	.attach_ring = ndp_ctrl_attach_ring,
	.detach_ring = ndp_ctrl_detach_ring,
};

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

struct ndp_channel *ndp_ctrl_v1_create_rx(struct ndp *ndp, int index, int node_offset)
{
	struct ndp_channel_id id = {.index = index, .type = NDP_CHANNEL_TYPE_RX};
	return ndp_ctrl_create(ndp, id, ndp_ctrl_attr_rx_groups, &ndp_ctrl_rx_ops, node_offset);
}

struct ndp_channel *ndp_ctrl_v1_create_tx(struct ndp *ndp, int index, int node_offset)
{
	struct ndp_channel_id id = {.index = index, .type = NDP_CHANNEL_TYPE_TX};
	return ndp_ctrl_create(ndp, id, ndp_ctrl_attr_tx_groups, &ndp_ctrl_tx_ops, node_offset);
}

module_param(ndp_ctrl_rx_request_size, uint, S_IRUGO);
MODULE_PARM_DESC(ndp_ctrl_rx_request_size, "Maximum request size for RX DMA controller (MPS: write transaction) [256]");
module_param(ndp_ctrl_tx_request_size, uint, S_IRUGO);
MODULE_PARM_DESC(ndp_ctrl_tx_request_size, "Maximum request size for TX DMA controller (MRRS: read transaction) [512]");
