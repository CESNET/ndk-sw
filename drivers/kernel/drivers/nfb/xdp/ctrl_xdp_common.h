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
static inline void nfb_xctrl_tx_free_buffers(struct xctrl *ctrl, bool cleanup)
{
	u32 i;
	u32 mdp = ctrl->c.mdp;
	u32 hdp_old = ctrl->c.hdp;
	u32 hdp_new;

	if(!cleanup) // When cleaning up the queues we no longer have the underliing controller running
		nc_ndp_ctrl_hdp_update(&ctrl->c);

	hdp_new = ctrl->c.hdp;

	for (i = hdp_old; i != hdp_new; i++, i &= mdp) {
		switch (ctrl->tx.buffers[i].type) {
		case NFB_XCTRL_BUFF_FRAME_PP: // xdp buff to be recycled to page pool
			// xdp_return_buff definition is missing in 4.18.0-477.10.1.el8_8.x86_64
			// xdp_return_buff(ctrl->tx.buffers[i].buff);
			xdp_return_frame(ctrl->tx.buffers[i].frame);
			break;
		case NFB_XCTRL_BUFF_XSK:
			ctrl->tx.completed_xsk_tx += 1;
			break;
		case NFB_XCTRL_BUFF_XSK_REXMIT:
			xsk_buff_free(ctrl->tx.buffers[i].xsk);
			break;
		case NFB_XCTRL_BUFF_SKB:
			dma_unmap_single(ctrl->dma_dev, ctrl->tx.buffers[i].dma, ctrl->tx.buffers[i].len, DMA_TO_DEVICE);
			if(ctrl->tx.buffers[i].skb)
				dev_kfree_skb(ctrl->tx.buffers[i].skb);
			
			break;
		case NFB_XCTRL_BUFF_FRAME: // Redirected frame - can be from another device all together
			// TODO check out xdp_return_frame_bulk()
			dma_unmap_single(ctrl->dma_dev, ctrl->tx.buffers[i].dma, ctrl->tx.buffers[i].len, DMA_TO_DEVICE);
			if(ctrl->tx.buffers[i].frame)
				xdp_return_frame(ctrl->tx.buffers[i].frame);
			
			break;
		case NFB_XCTRL_BUFF_DESC_TYPE0:
			break;
		default:
			printk(KERN_ERR "Tried to freed non existing type on tx completion. This is a driver bug.\n");
			BUG();
			break;
		}
		ctrl->tx.buffers[i].type = NFB_XCTRL_BUFF_BUG;
	}
}

/**
 * @brief Submits and maps frame onto tx. Not as straight forward as i would have liked -> check note
 * @note 
 * spin_lock(&lock)
 * 	  for (;;)
 * 	     nfb_xctrl_tx_submit_frame_needs_lock()
 *	  nc_ndp_ctrl_sdp_flush()
 * spin_unlock(&lock)
 * @param ctrl 
 * @param frame 
 * @return 0 on success
 */
static inline int nfb_xctrl_tx_submit_frame_needs_lock(struct xctrl *ctrl, struct xdp_frame *frame, bool pp)
{
	dma_addr_t dma;
	u32 len;
	u32 sdp;
	u32 mdp;

	struct nc_ndp_desc *descs;
	u64 last_upper_addr;
	u32 free_desc;
	int ret = 0;

	skb_frag_t *frag;
	
	u32 nr_frags = xdp_get_shared_info_from_frame(frame)->nr_frags;
	u32 i, j;
	struct xctrl_tx_buffer *tx_buff;

	sdp = ctrl->c.sdp;
	mdp = ctrl->c.mdp;
	descs = ctrl->desc_buffer_virt;

	// Max 2 descriptors per frag and head will be used
	// One to update the addr and second with data
	free_desc = (ctrl->c.hdp - sdp - 1) & mdp;
	if(free_desc < (nr_frags + 1) * 2) {
		printk(KERN_WARNING "nfb: submit_frame TX busy warning, packet dropped\n");
		ret = -EBUSY;
		goto exit;
	}

	// Handle small frames
	// Here we don't know where the frames came from. (Could be different driver all together)
	// Therefore if we cannot make the frame bigger we would have to alloc a new one. -ENOTSUPP
	if (frame->len < ETH_ZLEN) {
		// Usable frame space is smaller than min_len.
		if (unlikely(frame->frame_sz - frame->headroom < ETH_ZLEN)) {
			printk(KERN_WARNING "nfb: submit_frame TX got frame too small, packet dropped\n");
			ret = -ENOTSUPP;
			goto exit;
		}
		memset(frame->data + frame->len, 0, ETH_ZLEN - frame->len);
		len = ETH_ZLEN;
	} else {
		len = frame->len;
	}

	// If page_pool, then the page is already mapped
	if (!pp) {
		dma = dma_map_single(ctrl->dma_dev, frame->data, len, DMA_TO_DEVICE);
		if (unlikely(ret = dma_mapping_error(ctrl->dma_dev, dma))) {
			printk(KERN_ERR "nfb: %s failed to map frame\n", __func__);
			goto exit;
		}
	} else {
		dma = page_pool_get_dma_addr(virt_to_page(frame->data)) + XDP_PACKET_HEADROOM;
	}

	last_upper_addr = ctrl->c.last_upper_addr;
	if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
		last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
		ctrl->c.last_upper_addr = last_upper_addr;
		descs[sdp] = nc_ndp_tx_desc0(dma);
		ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
		sdp = (sdp + 1) & mdp;
	}

	if (!pp) { // Unmapped at reclaim
		ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_FRAME;
	} else { // Returned to page_pool
		ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_FRAME_PP;
	}
	ctrl->tx.buffers[sdp].frame = frame;
	ctrl->tx.buffers[sdp].dma = dma;
	ctrl->tx.buffers[sdp].len = len;
	descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);

#ifndef CONFIG_HAVE_XDP_SG
	if (true) { // Single buffer
#else 
	if (!xdp_frame_has_frags(frame)) { // Single buffer
#endif
		descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
	} else { // Multi buffer
		descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 1);
	} 
	sdp = (sdp + 1) & mdp;

	// FRAG PROCESSING
	for(i = 0; i < nr_frags; ++i) {
		frag = &xdp_get_shared_info_from_frame(frame)->frags[i];
		len = skb_frag_size(frag);

		// If page_pool, then the page is already mapped
		if (!pp) {
			dma = skb_frag_dma_map(ctrl->dma_dev, frag, 0, len, DMA_TO_DEVICE);
			if (unlikely(ret = dma_mapping_error(ctrl->dma_dev, dma))) {
				printk(KERN_ERR "nfb: %s failed to map frame\n", __func__);
				if(!pp) {
					goto unmap_exit;
				} else {
					goto exit;
				}
			}
		} else {
			dma = page_pool_get_dma_addr(skb_frag_page(frag)) + skb_frag_off(frag);
		}

		last_upper_addr = ctrl->c.last_upper_addr;
		if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
			last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
			ctrl->c.last_upper_addr = last_upper_addr;
			descs[sdp] = nc_ndp_tx_desc0(dma);
			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
			sdp = (sdp + 1) & mdp;
		}

		if (!pp) {
			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_FRAME;
			ctrl->tx.buffers[sdp].frame = NULL;
		} else {
			// Already completed by calling xdp_return_frame on the first fragment
			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
		}
		ctrl->tx.buffers[sdp].dma = dma;
		ctrl->tx.buffers[sdp].len = len;

		if (i == nr_frags - 1) { // Last frag
			descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
		} else { // Middle frag
			descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 1);
		}
		sdp = (sdp + 1) & mdp;
	}

	// Update ctrl state
	ctrl->c.sdp = sdp;
exit:
	return ret;

unmap_exit:
	// Reset sdp and unmap everything that was mapped (i frags + 1 head)
	sdp = ctrl->c.sdp;
	for (j = 0; j < i + 1; j++) {
		tx_buff = &ctrl->tx.buffers[sdp];
		if (tx_buff->type == NFB_XCTRL_BUFF_FRAME)
			dma_unmap_single(ctrl->dma_dev, tx_buff->dma, tx_buff->len, DMA_TO_DEVICE);
		
		sdp = (sdp + 1) & mdp;
	}
	return ret;
}

#endif // CTRL_XDP_COMMON_H
