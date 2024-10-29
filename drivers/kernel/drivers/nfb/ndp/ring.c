/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * NDP driver of the NFB platform - ring buffer module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/vmalloc.h>

#include "ndp.h"
#include "../nfb.h"

unsigned long ndp_ring_size = 4 * 1024 * 1024;
unsigned long ndp_ring_block_size = 4 * 1024 * 1024;


ssize_t ndp_channel_get_ring_size(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ndp_channel *channel = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%zu\n", channel->ring.size);
}

ssize_t ndp_channel_set_ring_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned long long value;
	struct ndp_channel *channel = dev_get_drvdata(dev);

	value = memparse(buf, NULL);
	ret = ndp_channel_ring_resize(channel, value);
	if (ret)
		return ret;

	return size;
}

/**
 * ndp_block_alloc - alloc DMAable space (lowlevel)
 * @dev: device which will use this DMA area
 * @count: count of members to allocate
 * @size: size of the members
 * @return: array of ndp_block structure with allocated addresses
 */
struct ndp_block *ndp_block_alloc(struct device *dev,
		unsigned int count, size_t size)
{
	int ret = -ENOMEM;
	struct ndp_block *block;
	struct ndp_block *blocks;

	blocks = kmalloc_node(count * sizeof(struct ndp_block), GFP_KERNEL, dev_to_node(dev));
	if (blocks == NULL)
		goto err_alloc_struct;

	for (block = blocks; block < blocks + count; block++) {
		block->virt = dma_alloc_coherent(dev, size, &block->phys,
				GFP_KERNEL);
		if (block->virt == NULL) {
			ret = -ENODEV;
			goto err_alloc_blocks;
		}
		memset(block->virt, 0, size);
		block->size = size;
	}
	return blocks;

err_alloc_blocks:
	while (block-- != blocks) {
		dma_free_coherent(dev, size, block->virt, block->phys);
	}
	kfree(blocks);
err_alloc_struct:
	return NULL;
}

/**
 * ndp_free_dma - free DMAable space (lowlevel)
 * @dev: device which used this DMA area
 * @blks: array returned by ndp_alloc_dma
 * @count: count of members in @blks
 * @size: size of members in @blks
 */
void ndp_block_free(struct device *dev, struct ndp_block *blocks,
		unsigned int count)
{
	struct ndp_block *block;
	for (block = blocks; block < blocks + count; block++) {
		dma_free_coherent(dev, block->size, block->virt, block->phys);
	}
	if (blocks != NULL) {
		kfree(blocks);
	}
}

void ndp_channel_update_fdt(struct ndp_channel *channel)
{
	int node;
	int fdt_offset;
	struct nfb_device *nfb = channel->ndp->nfb;
	void *fdt = nfb->fdt;

	write_lock(&nfb->fdt_lock);

	fdt_offset = fdt_path_offset(fdt, channel->id.type == NDP_CHANNEL_TYPE_TX ?
				"/drivers/ndp/tx_queues" : "/drivers/ndp/rx_queues");
	fdt_offset = fdt_subnode_offset(fdt, fdt_offset, dev_name(&channel->dev));

	node = dev_to_node(channel->ring.dev);
	if (node != NUMA_NO_NODE)
		fdt_setprop_u32(fdt, fdt_offset, "numa", node);
	fdt_setprop_u64(fdt, fdt_offset, "size", channel->ring.size);
	fdt_setprop_u64(fdt, fdt_offset, "mmap_size", channel->ring.mmap_size);
	fdt_setprop_u64(fdt, fdt_offset, "mmap_base", channel->ring.mmap_offset);

	write_unlock(&nfb->fdt_lock);
}

/**
 * ndp_channel_ring_alloc - allocate one ring buffer
 * @dev: device structure
 * @channel: channel for allocation
 * @count: block count
 * @size: block size (must be PAGE_SIZE aligned)
 *
 */
int ndp_channel_ring_alloc(struct device *dev,
		struct ndp_channel *channel, size_t count, size_t size)
{
	int i, j, k;
	int ret = -ENOMEM;

	int page_count;
	struct page **pages;

	channel->ring.size = 0;
	channel->ring.mmap_size = 0;
	channel->ring.block_count = 0;
	channel->ring.vmap = NULL;
	channel->ring.dev = dev;

	channel->ring.blocks = ndp_block_alloc(dev, count, size);
	if (channel->ring.blocks == NULL)
		goto err_block_alloc;

	page_count = count * (size / PAGE_SIZE);
	pages = kmalloc_node(sizeof(struct page*) * page_count * 2, GFP_KERNEL, dev_to_node(dev));
	if (pages == NULL)
		goto err_pages_alloc;

	k = 0;
	for (i = 0; i < count; i++) {
		for (j = 0; j < size; j += PAGE_SIZE) {
			pages[k] = virt_to_page(channel->ring.blocks[i].virt + j);
			pages[k + page_count] = pages[k];
			k++;
		}
	}
	channel->ring.vmap = vmap(pages, page_count * 2, VM_MAP, PAGE_KERNEL);
	kfree(pages);
	if (channel->ring.vmap == NULL)
		goto err_vmap;

	channel->ring.block_count = count;
	channel->ring.size = count * size;
	channel->ring.mmap_size = 2 * channel->ring.size;

	return 0;

err_vmap:
err_pages_alloc:
	ndp_block_free(dev, channel->ring.blocks, count);
	channel->ring.blocks = NULL;
err_block_alloc:
	return ret;
}

/**
 * ndp_channel_ring_free - free one ring buffer
 * @channel: channel for deallocation
 *
 */
void ndp_channel_ring_free(struct ndp_channel * channel)
{
	if (channel->ring.vmap)
		vunmap(channel->ring.vmap);
	ndp_block_free(channel->ring.dev, channel->ring.blocks,
			channel->ring.block_count);

	channel->ring.vmap = NULL;
	channel->ring.blocks = NULL;
	channel->ring.block_count = 0;
	channel->ring.mmap_size = 0;
	channel->ring.size = 0;
}

int ndp_ring_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv)
{
	struct ndp_channel *channel;
	struct ndp_ring *ring;
	struct ndp_block *block;
	unsigned long block_index;
	unsigned long block_offset;
//	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
//	unsigned long size = vma->vm_end - vma->vm_start;

	//channel = ndp_channel_by_ring_offset(szedata, offset);
	channel = (struct ndp_channel*) priv;
	if (channel == NULL)
		return -ENODEV;

	/* TODO: Check if channel is subscribed */

	/* Check permissions: read-only for RX */
	if (channel->id.type == NDP_CHANNEL_TYPE_RX &&
			(vma->vm_flags & (VM_WRITE | VM_READ)) != VM_READ)
		return -EINVAL;

	ring = &channel->ring;

	/* Allow mmap only for exact offset & size match */
	if (offset != ring->mmap_offset || size != channel->ring.mmap_size)
		return -EINVAL;

	for (offset = 0; offset < size; offset += ring->blocks[0].size) {
		block_offset = offset;
		while (block_offset >= ring->size) {
			block_offset -= ring->size;
		}
		block_index  = block_offset / ring->blocks[0].size;
		block = &ring->blocks[block_index];

		remap_pfn_range(vma, vma->vm_start + offset,
				virt_to_phys(block->virt) >> PAGE_SHIFT,
				ring->blocks[0].size, vma->vm_page_prot);
	}
	return 0;
}

int ndp_channel_ring_create(struct ndp_channel *channel, struct device *dev,
		 size_t block_count, size_t block_size)
{
	int ret;

	if (dev == NULL || channel == NULL)
		return -EINVAL;

	if (block_count == 0)
		return 0;

	ret = ndp_channel_ring_alloc(dev, channel, block_count, block_size);
	if (ret)
		goto err_ring_alloc;

	ret = nfb_char_register_mmap(channel->ndp->nfb, channel->ring.mmap_size, &channel->ring.mmap_offset, ndp_ring_mmap, channel);
	if (ret)
		goto err_register_mmap;

	ndp_channel_update_fdt(channel);

	ret = channel->ops->attach_ring(channel);
	if (ret)
		goto err_attach_ring;
	return ret;

	//channel->ops->detach_ring(channel);
err_attach_ring:
	nfb_char_unregister_mmap(channel->ndp->nfb, channel->ring.mmap_offset);
err_register_mmap:
	ndp_channel_ring_free(channel);
err_ring_alloc:
	return ret;
}

void ndp_channel_ring_destroy(struct ndp_channel *channel)
{
	if (channel->ring.size) {
		nfb_char_unregister_mmap(channel->ndp->nfb, channel->ring.mmap_offset);
		channel->ops->detach_ring(channel);
		ndp_channel_ring_free(channel);
		ndp_channel_update_fdt(channel);
	}
}

int ndp_channel_ring_resize(struct ndp_channel *channel, size_t size)
{
	int ret = -EBUSY;
	struct device *dev;

	size_t original_size;
	size_t block_count;

	original_size = channel->ring.size;

	if (!ispow2(size))
		return -EINVAL;

	block_count = size / ndp_ring_block_size;

	mutex_lock(&channel->mutex);

	if (channel->start_count)
		goto err_started;

	dev = channel->ring.dev;
	if (dev == NULL) {
		ret = -EBADF;
		goto err_nodev;
	}

	/* Detach old ring */
	ndp_channel_ring_destroy(channel);

	/* Create new ring */
	ret = ndp_channel_ring_create(channel, dev, block_count, ndp_ring_block_size);
	if (ret)
		goto err_ring_create;

	mutex_unlock(&channel->mutex);

	return 0;

err_ring_create:
	if (original_size)
		ndp_channel_ring_create(channel, dev, original_size / ndp_ring_block_size, ndp_ring_block_size);
err_nodev:
err_started:
	mutex_unlock(&channel->mutex);
	return ret;
}

static int ndp_param_size_set(const char *val, const struct kernel_param *kp)
{
	unsigned long long value = memparse(val, NULL);
	*((unsigned long*)kp->arg) = value;
	return 0;
}

const struct kernel_param_ops ndp_param_size_ops = {
	.set	= ndp_param_size_set,
	.get	= param_get_int,
};

module_param_cb(ndp_ring_size, &ndp_param_size_ops, &ndp_ring_size, S_IRUGO);
MODULE_PARM_DESC(ndp_ring_size, "Default size for new ring [4 MiB]");
module_param_cb(ndp_ring_block_size, &ndp_param_size_ops, &ndp_ring_block_size, S_IRUGO);
MODULE_PARM_DESC(ndp_ring_block_size, "Default size of block in new ring [4 MiB]");
