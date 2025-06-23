/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - common ctrl header
 *	contains functions common to both XDP modes
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#ifndef CTRL_XDP_COMMON_H
#define CTRL_XDP_COMMON_H

#include "ctrl_xdp.h"
#include "ethdev.h"
#include "channel.h"

#ifdef CONFIG_HAVE_PAGE_POOL_HELPERS
#include <net/page_pool/helpers.h>
#else
#include <net/page_pool.h>
#endif

// TODO: to make the logic easier this could be split into pp and xsk version of the function
/**
 * @brief Reclaims buffers from tx
 * 
 * @param ctrl 
 */
static inline void nfb_xctrl_tx_free_buffers(struct xctrl *ctrl)
{
	u32 i;
	u32 hdp = ctrl->c.hdp;
	u32 mdp = ctrl->c.mdp;
	u32 fdp = ctrl->tx.fdp;

	for (i = fdp; i != hdp; i++, i &= mdp) {
		switch (ctrl->tx.buffers[i].type) {
		case NFB_XCTRL_BUFF_FRAME_PP: // xdp buff to be recycled to page pool
			// xdp_return_buff definition is missing in 4.18.0-477.10.1.el8_8.x86_64
			// xdp_return_buff(ctrl->tx.buffers[i].buff);
			xdp_return_frame(ctrl->tx.buffers[i].frame);
			break;
		case NFB_XCTRL_BUFF_XSK:
			ctrl->tx.completed_xsk_tx += ctrl->tx.buffers[i].num_of_xsk_completions;
			break;
		case NFB_XCTRL_BUFF_SKB:
			dma_unmap_single(ctrl->dma_dev, ctrl->tx.buffers[i].dma, ctrl->tx.buffers[i].len, DMA_TO_DEVICE);
			dev_kfree_skb(ctrl->tx.buffers[i].skb);
			break;
		case NFB_XCTRL_BUFF_FRAME: // redirectedd frame - can be from another device all together
			// TODO check out xdp_return_frame_bulk()
			dma_unmap_single(ctrl->dma_dev, ctrl->tx.buffers[i].dma, ctrl->tx.buffers[i].len, DMA_TO_DEVICE);
			xdp_return_frame(ctrl->tx.buffers[i].frame);
			break;
		case NFB_XCTRL_BUFF_DESC_TYPE0:
			break;
		default:
			printk(KERN_ERR "Tried to freed non existing type on tx completion. This is a driver bug.\n");
			BUG();
			break;
		}
		ctrl->tx.buffers[i].type = NFB_XCTRL_BUFF_DESC_TYPE0;
	}
	ctrl->tx.fdp = hdp;
}

/**
 * @brief Submits and maps frame onto tx. Not as straight forward as i would have liked -> check note
 * @note 
 * spin_lock(&lock)
 * 	  nc_ndp_ctrl_hdp_update(&ctrl->c);
 * 	  nfb_xctrl_tx_free_buffers(ctrl);
 * 	  for (;;)
 * 	     nfb_xctrl_tx_submit_frame_needs_lock()
 *	  nc_ndp_ctrl_sdp_flush()
 * spin_unlock(&lock)
 * @param ctrl 
 * @param frame 
 * @return 0 on success
 */
static inline int nfb_xctrl_tx_submit_frame_needs_lock(struct xctrl *ctrl, struct xdp_frame *frame)
{
	dma_addr_t dma;
	u32 free_desc;
	u32 len;
	u16 min_len = ETH_ZLEN;
	int ret = 0;

	// ctrl vars
	u64 last_upper_addr = ctrl->c.last_upper_addr;
	u32 sdp = ctrl->c.sdp;
	u32 mdp = ctrl->c.mdp;
	struct nc_ndp_desc *descs = ctrl->desc_buffer_virt;

	free_desc = (ctrl->c.hdp - sdp - 1) & mdp;

	// Handle small frames
	len = max(frame->len, min_len);
	// Here we don't know where the frames came from. (Could be different driver all together)
	// Therefore if we cannot make the frame bigger we would have to alloc a new one. -ENOTSUPP
	if (frame->len < min_len) { // Usable frame space is smaller than min_len.
		if (unlikely(frame->frame_sz - frame->headroom < min_len)) {
			ret = -ENOTSUPP;
			goto exit;
		}
		memset(frame->data + frame->len, 0, min_len - frame->len);
	}

	dma = dma_map_single(ctrl->dma_dev, frame->data, len, DMA_TO_DEVICE);
	if (unlikely(ret = dma_mapping_error(ctrl->dma_dev, dma))) {
		printk(KERN_ERR "nfb: %s failed to map frame\n", __func__);
		goto exit;
	}

	if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
		if (unlikely(free_desc < 2)) {
			printk(KERN_ERR "nfb: %s busy warning\n", __func__);
			dma_unmap_single(ctrl->dma_dev, dma, len, DMA_TO_DEVICE);
			ret = -EBUSY;
			goto exit;
		}

		last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
		ctrl->c.last_upper_addr = last_upper_addr;

		descs[sdp] = nc_ndp_tx_desc0(dma);

		ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
		free_desc--;
		sdp = (sdp + 1) & mdp;
	}

	if (unlikely(free_desc == 0)) {
		printk(KERN_ERR "nfb: nfb_xctrl_xmit busy warning\n");
		dma_unmap_single(ctrl->dma_dev, dma, len, DMA_TO_DEVICE);
		ret = -EBUSY;
		goto exit;
	}

	ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_FRAME;
	ctrl->tx.buffers[sdp].frame = frame;
	ctrl->tx.buffers[sdp].dma = dma;
	ctrl->tx.buffers[sdp].len = len;
	descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
	sdp = (sdp + 1) & mdp;

	// update ctrl state
	ctrl->c.sdp = sdp;
exit:
	return ret;
}

#endif // CTRL_XDP_COMMON_H
