/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - ctrl module for AF_XDP / zero-copy operation
 *	memory is allocated from the userspace
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include "ctrl_xdp_common.h"
#include <linux/pci.h>

/**
 * @brief Tries to rexmit xsk buffer.
 * 
 * @param ctrl 
 * @param xdp 
 * @return 0 on success
 */
static inline int nfb_xctrl_rexmit_xsk(struct xctrl *ctrl, struct xsk_buff_pool *pool, struct xdp_buff *xdp)
{
	int ret = 0;
	u32 free_desc;
	dma_addr_t dma;
	void *data;
	u32 len;

	u64 last_upper_addr;
	u32 sdp;
	u32 mdp;
	u32 n_frags, i;

	struct nc_ndp_desc *descs = ctrl->desc_buffer_virt;
	struct xdp_buff *frags[NFB_MAX_AF_XDP_FRAGS];

	// Prepare frags
	n_frags = 0; 
	frags[0] = xdp; // First frag
	do {
		len = frags[n_frags]->data_end - frags[n_frags]->data;
		if (unlikely(len < ETH_ZLEN)) { // Pad the frag to min len
			memset(data + len, 0, ETH_ZLEN - len);
			len = ETH_ZLEN;
			frags[n_frags]->data_end = frags[n_frags]->data + len;
		}
		++n_frags;
#ifdef CONFIG_HAVE_AF_XDP_SG
	} while((frags[n_frags] = xsk_buff_get_frag(xdp)));
#else 
	} while(false);
#endif

	spin_lock(&ctrl->tx.tx_lock);
	{
		sdp = ctrl->c.sdp;
		mdp = ctrl->c.mdp;
		free_desc = (ctrl->c.hdp - sdp - 1) & mdp;

		// Max 2 descriptors per frag will be used
		// One to update the addr and second with data
		if(free_desc < n_frags * 2) {
			printk(KERN_ERR "nfb: XDP_TX busy warning, packet dropped\n");
			for (i = 0; i < n_frags; i++)
				xsk_buff_free(frags[i]);

			ret = -EBUSY;
			goto out;
		}

		for (i = 0; i < n_frags; i++) {
			data = frags[i]->data;
			dma = xsk_buff_xdp_get_dma(frags[i]);
			len = frags[i]->data_end - frags[i]->data;

			last_upper_addr = ctrl->c.last_upper_addr;
			if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
				last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
				ctrl->c.last_upper_addr = last_upper_addr;
				descs[sdp] = nc_ndp_tx_desc0(dma);
				ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
				sdp = (sdp + 1) & mdp;
			}

			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_XSK_REXMIT;
			ctrl->tx.buffers[sdp].xsk = frags[i];
			ctrl->tx.buffers[sdp].dma = dma;
			ctrl->tx.buffers[sdp].len = len;
			if(i == n_frags - 1) { // Last part of the packet
				descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
			} else { // Another fragment incomming
				descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 1);
			}
			xsk_buff_raw_dma_sync_for_device(pool, dma, len);
			sdp = (sdp + 1) & mdp;
		}

		// Update ctrl
		ctrl->c.sdp = sdp;
	}
out:
	spin_unlock(&ctrl->tx.tx_lock);
	return ret;
}

#ifndef CONFIG_HAVE_XSK_BUFF_ALLOC_BATCH
static inline u32 xsk_buff_alloc_batch(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 max)
{
	unsigned i = 0;
	do {
		xdp[i] = xsk_buff_alloc(pool);
	} while (xdp[i++] && i < max);
	return i;
}
#endif

/**
 * @brief XSK pool operation to fill card with rx descriptors.
 * 
 * @param ctrl 
 * @param pool struct xsk_buff_pool
 * @return int number of descriptors fileld
 */
static inline int nfb_xctrl_rx_fill_xsk(struct xctrl *ctrl)
{
	const u32 batch_size = NFB_XDP_CTRL_PACKET_BURST;

	// Ctrl vars
	u64 last_upper_addr = ctrl->c.last_upper_addr;
	const u32 mdp = ctrl->c.mdp;
	u32 sdp = ctrl->c.sdp;
	const u32 mbp = ctrl->rx.mbp;
	u32 fbp = ctrl->rx.fbp;

	u32 frame_len;
	struct xsk_buff_pool *pool = ctrl->rx.xsk.pool;
	struct xdp_buff *buffs[NFB_XDP_CTRL_PACKET_BURST];
	dma_addr_t dma;
	struct nc_ndp_desc *descs = ctrl->desc_buffer_virt;
	u32 free_desc, free_buffs;
	u32 real_count, filled;

#ifdef CONFIG_HAVE_AF_XDP_SG
	// TODO: check if there is a better way to know if SG was enabled in the userspace AF_XDP
	bool sg_enabled	= pool->umem->flags & XDP_UMEM_SG_FLAG;
#else
	const bool sg_enabled = false;
#endif

	// Check if refill needed
	nc_ndp_ctrl_hdp_update(&ctrl->c);
	free_buffs = (ctrl->rx.pbp - fbp - 1) & mbp;
	free_desc = (ctrl->c.hdp - sdp - 1) & mdp;
	if (free_buffs < batch_size || free_desc < batch_size)
		return 0;

	// Alloc xsk buffers
	// Internaly calculates with XDP_PACKET_HEADROOM, shared info is not used
	frame_len = xsk_pool_get_rx_frame_size(pool);
	real_count = xsk_buff_alloc_batch(pool, buffs, batch_size);
	for (filled = 0; filled < real_count; filled++) {
		dma = xsk_buff_xdp_get_dma(buffs[filled]); // Takes XDP_PACKET_HEADROOM into account
		if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
			if (unlikely(free_desc == 0)) {
				break;
			}
			last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
			ctrl->c.last_upper_addr = last_upper_addr;
			descs[sdp] = nc_ndp_rx_desc0(dma);
			sdp = (sdp + 1) & mdp;
			free_desc--;
		}
		if (unlikely(free_desc == 0)) {
			break;
		}
		ctrl->rx.xsk.xdp_ring[fbp] = buffs[filled];
		descs[sdp] = nc_ndp_rx_desc2(dma, frame_len, sg_enabled);
		sdp = (sdp + 1) & mdp;
		fbp = (fbp + 1) & mbp;
		free_desc--;
	}

	// If the loop quits because there was too little free descriptors
	// the remaining buffers have to be freed
	for (; filled < real_count; filled++) {
		xsk_buff_free(buffs[filled]);
	}

	// Update ctrl state
	ctrl->rx.fbp = fbp;
	ctrl->c.sdp = sdp;
	return filled;
}

static inline struct sk_buff *nfb_napi_build_skb_from_xsk(struct xdp_buff *xdp, struct napi_struct *napi) {
	struct sk_buff *skb;
	u32 n_frags, i;
	u32 len;
	struct xdp_buff *frags[NFB_MAX_AF_XDP_FRAGS];

	frags[0] = xdp; // First frag
	len = xdp->data_end - xdp->data;
#ifdef CONFIG_HAVE_AF_XDP_SG
	for (n_frags = 1; (frags[n_frags] = xsk_buff_get_frag(xdp)); n_frags++) {
		len += frags[n_frags]->data_end - frags[n_frags]->data;
	}
#else
	n_frags = 1;
#endif

	// Alloc skb
	skb = napi_alloc_skb(napi, len);
	if (unlikely(!skb)) {
		printk(KERN_ERR "%s: Failed to allocate SKB of len %u\n", __func__, len);
		for (i = 0; i < n_frags; i++)
			xsk_buff_free(frags[i]);

		goto out;
	}

	// Memcpy data
	for (i = 0; i < n_frags; i++) {
		skb_put_data(skb, frags[i]->data, frags[i]->data_end - frags[i]->data);
		xsk_buff_free(frags[i]); // Free the copied buffer
	}
out:			
	return skb;
}

/**
 * @brief XDP handler
 * 
 * @param prog 
 * @param xdp 
 * @param rxq 
 * @return result
 */
static inline void nfb_xctrl_handle_xsk(struct bpf_prog *prog, struct xdp_buff *xdp, struct nfb_xdp_queue *rxq)
{
	unsigned act;
	int ret;
	struct sk_buff *skb;
	struct bpf_prog *xdp_prog;
	struct nfb_xdp_channel *channel = container_of(rxq, struct nfb_xdp_channel, rxq);
	struct nfb_ethdev *ethdev = channel->ethdev;

	// Non zero copy redirect not supported with AF_XDP_SG as of (6.15)
	// xdp_do_redirect calls xdp_convert_zc_to_xdp_frame which can only copy one page of data
	struct bpf_redirect_info *ri;
	enum bpf_map_type map_type;

	rcu_read_lock();
	xdp_prog = rcu_dereference(prog);
	if (xdp_prog) {
		act = bpf_prog_run_xdp(xdp_prog, xdp);
	} else {
		act = XDP_PASS;
	}
	switch (act) {
	case XDP_PASS:
		// This allocates memory => AF_XDP slow fallback for normal operation
		// All frags are freed on success and fail
		skb = nfb_napi_build_skb_from_xsk(xdp, &rxq->napi);
		if (unlikely(IS_ERR_OR_NULL(skb))) {
			printk(KERN_DEBUG "SKB build failed\n");
			break;
		}
		// Receive packet onto queue it arriverd on
		skb_record_rx_queue(skb, channel->index);
		skb->protocol = eth_type_trans(skb, channel->pool->netdev);
		// TODO gro_receive is supposed to be a free
		// 		performance boost but it is quite bad for
		//		debugging because you cannot check if all packet
		//		received are the same as sent
		// napi_gro_receive(napi, skb);
		netif_receive_skb(skb);
		// TODO: future extension
		// if (netif_receive_skb(skb) != NET_RX_DROP) {
		// 	ctrl.stats = ...
		// } else {
		// 	ctrl.stats = ...
		// }
		break;
	case XDP_TX:
		// Returned on tx reclaim or freed on error;
		nfb_xctrl_rexmit_xsk(channel->txq.ctrl, channel->pool, xdp);
		break;
	case XDP_REDIRECT:
		// Non zero copy redirect not supported with AF_XDP_SG as of (6.15)
		// xdp_do_redirect calls xdp_convert_zc_to_xdp_frame which can only copy one page of data
#ifdef CONFIG_HAVE_BPF_NET_CTX_GET_RI
		ri = bpf_net_ctx_get_ri();
#else
		ri = this_cpu_ptr(&bpf_redirect_info);
#endif
		map_type = ri->map_type;
		if (unlikely(map_type != BPF_MAP_TYPE_XSKMAP)) {
			printk(KERN_ERR "nfb: Only redirect to userspace supported in AF_XDP mode, dropping packet.\n");
			goto aborted;
		} else {
			if(unlikely((ret = xdp_do_redirect(ethdev->netdev, xdp, xdp_prog)))) {
				printk(KERN_ERR "nfb: xdp_do_redirect error ret: %d\n", ret);
				goto aborted;
			}
		}
		break;
	default:
		fallthrough;
	case XDP_ABORTED:
aborted:
		printk(KERN_ERR "nfb: %s packet aborted\n", __func__);
		fallthrough;
	case XDP_DROP:
		xsk_buff_free(xdp);
		break;
	}
	rcu_read_unlock();
}

#ifndef CONFIG_HAVE_XSK_BUFF_SET_SIZE
static inline void xsk_buff_set_size(struct xdp_buff *xdp, u32 size)
{
	xdp->data = xdp->data_hard_start + XDP_PACKET_HEADROOM;
	xdp->data_meta = xdp->data;
	xdp->data_end = xdp->data + size;
}
#endif

static inline u16 nfb_xctrl_rx_xsk(struct xctrl *ctrl, u16 nb_pkts, struct nfb_ethdev *ethdev, struct nfb_xdp_queue *rxq, struct napi_struct *napi)
{
	u32 len_remain;
	struct nc_ndp_hdr *hdrs = ctrl->rx.hdr_buffer_cpu;
	struct nc_ndp_hdr *hdr;
	u32 i;
	u16 nb_rx;
	u32 shp = ctrl->c.shp;
	const u32 mhp = ctrl->c.mhp;
	u32 pbp = ctrl->rx.pbp;
	const u32 mbp = ctrl->rx.mbp;

	struct xdp_buff *head;
#ifdef CONFIG_HAVE_XDP_SG
	struct xdp_buff *frag;
	const u32 frame_size = xsk_pool_get_rx_frame_size(ctrl->rx.xsk.pool);
#endif
#ifndef CONFIG_HAVE_ONE_ARG_XSK_BUFF_ADD_FRAG
	struct xdp_buff_xsk *xsk_struct;
#endif

	// Fill the card with empty buffers
	while (nfb_xctrl_rx_fill_xsk(ctrl))
		;
	nc_ndp_ctrl_sdp_flush(&ctrl->c);

	// Get the amount of packets ready to be processed
	nc_ndp_ctrl_hhp_update(&ctrl->c);
	nb_rx = (ctrl->c.hhp - shp) & mhp;
	if (nb_pkts < nb_rx)
		nb_rx = nb_pkts;

	// Ready packets for receive
	for (i = 0; i < nb_rx; ++i) {
		// Get one packet (can be fragmented)
		hdr = &hdrs[shp];
		shp = (shp + 1) & mhp;
		len_remain = hdr->frame_len;

		// Get the first fragment (head)
		head = ctrl->rx.xsk.xdp_ring[pbp];
		pbp = (pbp + 1) & mbp;
#ifdef CONFIG_HAVE_ONE_ARG_XSK_BUFF_DMA_SYNC
		xsk_buff_dma_sync_for_cpu(head);	
#else
		xsk_buff_dma_sync_for_cpu(head, ctrl->rx.xsk.pool);
#endif

#ifndef CONFIG_HAVE_XDP_SG
		if (false) {
#else
		// Process head
		if (len_remain > frame_size) { // Is fragmented
			xsk_buff_set_size(head, frame_size);
			len_remain -= frame_size;
			xdp_buff_set_frags_flag(head);
#endif
		} else { // Not fragmented
			xsk_buff_set_size(head, len_remain);
			len_remain -= len_remain;
		}

#ifdef CONFIG_HAVE_XDP_SG
		// Process frags
		while(len_remain) {
			frag = ctrl->rx.xsk.xdp_ring[pbp];
			pbp = (pbp + 1) & mbp;
#ifdef CONFIG_HAVE_ONE_ARG_XSK_BUFF_DMA_SYNC
			xsk_buff_dma_sync_for_cpu(frag);	
#else
			xsk_buff_dma_sync_for_cpu(frag, ctrl->rx.xsk.pool);
#endif // CONFIG_HAVE_ONE_ARG_XSK_BUFF_DMA_SYNC
			if (len_remain > frame_size) { // Not last fragment
				xsk_buff_set_size(frag, frame_size);
				len_remain -= frame_size;
			} else { // Last fragment
				xsk_buff_set_size(frag, len_remain);
				len_remain -= len_remain;
			}
#ifdef CONFIG_HAVE_ONE_ARG_XSK_BUFF_ADD_FRAG
			xsk_buff_add_frag(frag);
#else
			// Revert the behavior of xsk_buff_add_frag to not touch the shared info struct (pre 6.14)
			// This saves 384 bytes in each AF_XDP frame for AFAIK no sideeffects
			// This also allows to defer setup needed for the creation of skb to the last moment
			xsk_struct = container_of(frag, struct xdp_buff_xsk, xdp);
			list_add_tail(&xsk_struct->list_node, &xsk_struct->pool->xskb_list);
#endif // CONFIG_HAVE_ONE_ARG_XSK_BUFF_ADD_FRAG
		}
#endif // CONFIG_HAVE_XDP_SG

		nfb_xctrl_handle_xsk(ethdev->prog, head, rxq);
	}

	// update ctrl state
	ctrl->c.shp = shp;
	ctrl->rx.pbp = pbp;

	return nb_rx;
}

int nfb_xctrl_napi_poll_rx_xsk(struct napi_struct *napi, int budget)
{
	struct nfb_xdp_queue *rxq = container_of(napi, struct nfb_xdp_queue, napi);
	struct nfb_xdp_channel *channel = container_of(rxq, struct nfb_xdp_channel, rxq);
	struct xsk_buff_pool *pool = channel->pool;
	struct xctrl *ctrl = rxq->ctrl;
	
	struct net_device *netdev = napi->dev;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	unsigned received;

	// TODO: maybe look at splitting the reclaim for the XDP_TX action to move to rx napi and rest move to tx
	// The issue is that calling the xsk_buff_free from TX reclaim
	// while calling it from for an example XDP_PASS skb build corrupts the xsk_buff list
	// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	// The tx reclaim needs to be in rx napi because rx napi calls xsk_buff_free
	struct xctrl *txctrl = channel->txq.ctrl;
	if(spin_trylock(&txctrl->tx.tx_lock)) {
		// Free the completed tx buffers
		nfb_xctrl_tx_free_buffers(txctrl, false);
		xsk_tx_completed(pool, txctrl->tx.completed_xsk_tx);
		txctrl->tx.completed_xsk_tx = 0;
		spin_unlock(&txctrl->tx.tx_lock);
	}

	received = nfb_xctrl_rx_xsk(ctrl, budget, ethdev, rxq, napi);
	// Flush sdp and shp after software processing is done
	nc_ndp_ctrl_sp_flush(&ctrl->c);

	// Flushes redirect maps
	xdp_do_flush();

	// Work not done -> reschedule if budget remains
	if (received == budget)
		return budget;

	// Work done -> finish
	napi_complete_done(napi, received);
	return received;
}

int nfb_xctrl_napi_poll_tx_xsk(struct napi_struct *napi, int budget)
{
	struct nfb_xdp_queue *txq = container_of(napi, struct nfb_xdp_queue, napi);
	struct nfb_xdp_channel *channel = container_of(txq, struct nfb_xdp_channel, txq);
	struct xctrl *ctrl = txq->ctrl;
	struct xsk_buff_pool *pool = channel->pool;
	struct xdp_desc *buffs;
	u32 free_desc;
	u32 ready, i = 0;
	dma_addr_t dma;
	void *data;
	u32 len;

	u64 last_upper_addr;
	u32 sdp;
	u32 mdp;
	struct nc_ndp_desc *descs = ctrl->desc_buffer_virt;

	spin_lock(&ctrl->tx.tx_lock);
	{
		sdp = ctrl->c.sdp;
		mdp = ctrl->c.mdp;

		// Max 2 descriptors per frag will be used
		// One to update the addr and second with data
		free_desc = (ctrl->c.hdp - sdp - 1) & mdp;
		if(free_desc < budget * 2) {
			printk(KERN_WARNING "nfb: AF_XDP TX busy warning, skipped one poll\n");
			goto out;
		}

		ready = xsk_tx_peek_release_desc_batch(pool, budget);
		if (!ready) {
			goto out;
		}

		buffs = pool->tx_descs;
		for (i = 0; i < ready; i++) {
			data = xsk_buff_raw_get_data(pool, buffs[i].addr);
			dma = xsk_buff_raw_get_dma(pool, buffs[i].addr);
			len = buffs[i].len;

			if (len < ETH_ZLEN) { // Packet is too small
				memset(data + len, 0, ETH_ZLEN - len);
				len = ETH_ZLEN;
			}

			last_upper_addr = ctrl->c.last_upper_addr;
			if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
				last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
				ctrl->c.last_upper_addr = last_upper_addr;
				descs[sdp] = nc_ndp_tx_desc0(dma);
				ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
				sdp = (sdp + 1) & mdp;
			}

			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_XSK;
#ifndef CONFIG_HAVE_AF_XDP_SG
			if(true) {
#else
			if(xsk_is_eop_desc(&buffs[i])) { // Last part of the packet
#endif
				descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
			} else { // Another fragment incomming
				descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 1);
			}
			xsk_buff_raw_dma_sync_for_device(pool, dma, len);
			sdp = (sdp + 1) & mdp;
		}
		// Update ctrl
		ctrl->c.sdp = sdp;
out:
		// Flush counters when done (Maybe frames were enqueued by XDP_TX)
		nc_ndp_ctrl_sdp_flush(&ctrl->c);
	}
	spin_unlock(&ctrl->tx.tx_lock);

	// Work not done -> reschedule if budget remains
	if (i == budget)
		return budget;

	// Work done -> finish
	napi_complete_done(napi, i);
	return i;
}

static void nfb_xctrl_stop_xsk(struct xctrl *ctrl)
{
	int cnt, i = 0;
	int err;
	u32 count;
	u32 shp = ctrl->c.shp;
	u32 mhp = ctrl->c.mhp;
	struct xdp_buff *xdp;

	do {
		err = nc_ndp_ctrl_stop(&ctrl->c);
		if (err != -EAGAIN && err != -EINPROGRESS)
			break;

		// receive packets from card and try again
		nc_ndp_ctrl_hhp_update(&ctrl->c);
		count = (ctrl->c.hhp - shp) & mhp;
		for (i = 0; i < count; ++i) {
			xdp = ctrl->rx.xsk.xdp_ring[shp];
			xsk_buff_free(xdp);
			shp = (shp + 1) & mhp;
		}
		ctrl->c.shp = shp;
		nc_ndp_ctrl_sp_flush(&ctrl->c);
		mdelay(1);
	} while (cnt++ < 100);

	if (err) {
		err = nc_ndp_ctrl_stop_force(&ctrl->c);
		printk(KERN_WARNING "nfb: queue id %u didn't stop in 100 msecs; Force stopping dma ctrl; This might damage firmware.\n", ctrl->nfb_queue_id);
	}
}

struct xctrl *nfb_xctrl_alloc_xsk(struct net_device *netdev, u32 queue_id, struct xsk_buff_pool *pool, enum xdp_ctrl_type type)
{
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	struct nfb_xdp_channel *channel = &ethdev->channels[queue_id];
	struct nfb_device *nfb = ethdev->nfb;
	struct nfb_xdp_queue *queue;
	struct xctrl *ctrl;
	int fdt_offset;

	int err;
	// Finding the fdt offset
	switch (type) {
	case NFB_XCTRL_RX:
		queue = &channel->rxq;
		fdt_offset = nfb_comp_find(nfb, "netcope,dma_ctrl_ndp_rx", channel->nfb_index);
		break;
	case NFB_XCTRL_TX:
		queue = &channel->txq;
		fdt_offset = nfb_comp_find(nfb, "netcope,dma_ctrl_ndp_tx", channel->nfb_index);
		break;
	default:
		err = -EINVAL;
		goto fdt_offset_fail;
		break;
	}
	if (fdt_offset < 0) {
		err = -ENODEV;
		goto fdt_offset_fail;
	}

	// Allocating struct
	if (!(ctrl = kzalloc_node(sizeof(struct xctrl), GFP_KERNEL, channel->numa))) {
		err = -ENOMEM;
		goto ctrl_alloc_fail;
	}

	ctrl->type = type;
	ctrl->nfb_queue_id = channel->nfb_index;
	ctrl->netdev_queue_id = channel->index;
	ctrl->dma_dev = &nfb->pci->dev;
	ctrl->nb_desc = pool->heads_cnt * 2;

	// Allocating control buffers
	switch (type) {
	case NFB_XCTRL_RX:
		ctrl->rx.mbp = ctrl->nb_desc - 1;
		if (!(ctrl->rx.xsk.xdp_ring = kzalloc_node(sizeof(struct xdp_buff *) * ctrl->nb_desc, GFP_KERNEL, channel->numa))) {
			err = -ENOMEM;
			goto buff_alloc_fail;
		}
		break;
	case NFB_XCTRL_TX:
		spin_lock_init(&ctrl->tx.tx_lock);
		if (!(ctrl->tx.buffers = kzalloc_node(sizeof(struct xctrl_tx_buffer) * ctrl->nb_desc, GFP_KERNEL, channel->numa))) {
			err = -ENOMEM;
			goto buff_alloc_fail;
		}
		break;
	default:
		err = -EINVAL;
		goto buff_alloc_fail;
		break;
	}

	// Allocating DMA buffers
	if (!(ctrl->desc_buffer_virt = dma_alloc_coherent(ctrl->dma_dev, ctrl->nb_desc * sizeof(struct nc_ndp_desc), &ctrl->desc_buffer_dma, GFP_KERNEL))) {
		err = -ENOMEM;
		goto dma_data_fail;
	}
	if (!(ctrl->update_buffer_virt = dma_alloc_coherent(ctrl->dma_dev, sizeof(u32) * 2, &ctrl->update_buffer_dma, GFP_KERNEL))) {
		err = -ENOMEM;
		goto dma_update_fail;
	}

	if (type == NFB_XCTRL_RX) {
		ctrl->rx.nb_hdr = ctrl->nb_desc;
		if (!(ctrl->rx.hdr_buffer_cpu = dma_alloc_coherent(ctrl->dma_dev, ctrl->rx.nb_hdr * sizeof(struct nc_ndp_hdr), &ctrl->rx.hdr_buffer_dma, GFP_KERNEL))) {
			err = -ENOMEM;
			goto dma_hdr_fail;
		}
	}

	// creating info
	if (type == NFB_XCTRL_RX) {
		ctrl->rx.xsk.pool = pool;
		if ((err = xdp_rxq_info_reg(&ctrl->rx.rxq_info, netdev, channel->index, 0))) {
			printk(KERN_ERR "nfb: rx_info register fail with: %d\n", err);
			goto meminfo_reg_fail;
		}
		if ((err = xdp_rxq_info_reg_mem_model(&ctrl->rx.rxq_info, MEM_TYPE_XSK_BUFF_POOL, NULL))) {
			printk(KERN_ERR "nfb: mem_model register fail with: %d\n", err);
			goto meminfo_model_fail;
		}

		xsk_pool_set_rxq_info(pool, &ctrl->rx.rxq_info);
	}

	// Opening controller
	if ((err = nc_ndp_ctrl_open(nfb, fdt_offset, &ctrl->c))) {
		goto ndp_ctrl_open_fail;
	}

	return ctrl;

ndp_ctrl_open_fail:
	if (type == NFB_XCTRL_RX) {
		xdp_rxq_info_unreg_mem_model(&ctrl->rx.rxq_info);
	}
meminfo_model_fail:
	if (type == NFB_XCTRL_RX) {
		xdp_rxq_info_unreg(&ctrl->rx.rxq_info);
	}
meminfo_reg_fail:
	if (type == NFB_XCTRL_RX) {
		dma_free_coherent(ctrl->dma_dev, ctrl->rx.nb_hdr * sizeof(struct nc_ndp_hdr), ctrl->rx.hdr_buffer_cpu, ctrl->rx.hdr_buffer_dma);
	}
dma_hdr_fail:
	dma_free_coherent(ctrl->dma_dev, sizeof(u32) * 2, ctrl->update_buffer_virt, ctrl->update_buffer_dma);
dma_update_fail:
	dma_free_coherent(ctrl->dma_dev, ctrl->nb_desc * sizeof(struct nc_ndp_desc), ctrl->desc_buffer_virt, ctrl->desc_buffer_dma);
dma_data_fail:
	switch (type) {
	case NFB_XCTRL_RX:
		kfree(ctrl->rx.xsk.xdp_ring);
		break;
	case NFB_XCTRL_TX:
		kfree(ctrl->tx.buffers);
		break;
	default:
		break;
	}
buff_alloc_fail:
	kfree(ctrl);
ctrl_alloc_fail:
fdt_offset_fail:
	printk(KERN_ERR "nfb: Error opening dma ctrl on queue %d; %d", channel->nfb_index, err);
	return NULL;
}

void nfb_xctrl_destroy_xsk(struct xctrl *ctrl)
{
	if (test_bit(XCTRL_STATUS_IS_RUNNING, &ctrl->status)) {
		nfb_xctrl_stop_xsk(ctrl);
	}
	nc_ndp_ctrl_close(&ctrl->c);
	dma_free_coherent(ctrl->dma_dev, ctrl->nb_desc * sizeof(struct nc_ndp_desc), ctrl->desc_buffer_virt, ctrl->desc_buffer_dma);
	dma_free_coherent(ctrl->dma_dev, sizeof(u32) * 2, ctrl->update_buffer_virt, ctrl->update_buffer_dma);
	switch (ctrl->type) {
	case NFB_XCTRL_RX:
		dma_free_coherent(ctrl->dma_dev, ctrl->rx.nb_hdr * sizeof(struct nc_ndp_hdr), ctrl->rx.hdr_buffer_cpu, ctrl->rx.hdr_buffer_dma);
		xdp_rxq_info_unreg_mem_model(&ctrl->rx.rxq_info);
		xdp_rxq_info_unreg(&ctrl->rx.rxq_info);
		kfree(ctrl->rx.xsk.xdp_ring);
		break;
	case NFB_XCTRL_TX:
		// free all enqueued tx buffers
		ctrl->c.hdp = ctrl->c.sdp;
		nfb_xctrl_tx_free_buffers(ctrl, true);
		kfree(ctrl->tx.buffers);
		break;
	default:
		break;
	}
	kfree(ctrl);
	ctrl = NULL;
}
