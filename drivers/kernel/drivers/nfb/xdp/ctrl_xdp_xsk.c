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

// NOTE: This is not zero copy operation, the correct way to rexmit a frame
//		 in XSK mode is through the userspace from RX ring to TX ring.
/**
 * @brief Tries to rexmit xsk buffer. Frees buffer on fail via xdp_return_frame();
 * 
 * @param ctrl 
 * @param xdp 
 * @return 0 on success
 */
static inline int nfb_xctrl_rexmit_xsk(struct xctrl *ctrl, struct xdp_buff *xdp)
{
	int ret = 0;
	struct xdp_frame *frame;
	// xdp_convert_buff_to_frame frees the xsk on success and doesn't free on failure
	// this is af_xdp slow fallback
	if (unlikely(!(frame = xdp_convert_buff_to_frame(xdp)))) {
		xdp_return_frame(frame);
		ret = -ENOMEM;
		goto exit;
	}
	spin_lock(&ctrl->tx.tx_lock);
	{
		// reclaim tx buffers
		nc_ndp_ctrl_hdp_update(&ctrl->c);
		nfb_xctrl_tx_free_buffers(ctrl);
		// process tx
		ret = nfb_xctrl_tx_submit_frame_needs_lock(ctrl, frame);
		if (unlikely(ret)) {
			xdp_return_frame(frame);
		}
		// flush
		nc_ndp_ctrl_sdp_flush(&ctrl->c);
	}
	spin_unlock(&ctrl->tx.tx_lock);
exit:
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

	// ctrl vars
	u64 last_upper_addr = ctrl->c.last_upper_addr;
	u32 mdp = ctrl->c.mdp;
	u32 sdp = ctrl->c.sdp;
	u32 mhp = ctrl->c.mhp;
	u32 php = ctrl->rx.php;

	// helper vars
	u32 frame_len;
	struct xsk_buff_pool *pool = ctrl->rx.xsk.pool;
	struct xdp_buff *buffs[NFB_XDP_CTRL_PACKET_BURST];
	dma_addr_t dma;
	struct nc_ndp_desc *descs = ctrl->desc_buffer_virt;
	u32 free_desc, free_hdrs;
	u32 real_count, i;

	// Check if refill needed
	nc_ndp_ctrl_hdp_update(&ctrl->c);
	free_hdrs = (ctrl->c.shp - php - 1) & mhp;
	free_desc = (ctrl->c.hdp - sdp - 1) & mdp;
	if (free_hdrs < batch_size || free_desc < batch_size)
		return 0;

	// Alloc xsk buffers
	frame_len = xsk_pool_get_rx_frame_size(pool); // internaly calculates with XDP_PACKET_HEADROOM
	real_count = xsk_buff_alloc_batch(pool, buffs, batch_size);
	for (i = 0; i < real_count; i++) {
		dma = xsk_buff_xdp_get_dma(buffs[i]); // Takes XDP_PACKET_HEADROOM into account

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
		ctrl->rx.xsk.xdp_ring[php] = buffs[i];
		descs[sdp] = nc_ndp_rx_desc2(dma, frame_len, 0);
		sdp = (sdp + 1) & mdp;
		php = (php + 1) & mhp;
		free_desc--;
	}

	// if the loop quits because there was too little free descriptors
	// the remaining buffers have to be freed
	for (; i < real_count; i++) {
		xsk_buff_free(buffs[i]);
	}

	// update ctrl state
	ctrl->rx.php = php;
	ctrl->c.sdp = sdp;
	return i;
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
	struct xdp_frame *frame;
	struct sk_buff *skb;
	struct bpf_prog *xdp_prog;
	struct nfb_xdp_channel *channel = container_of(rxq, struct nfb_xdp_channel, rxq);
	struct nfb_ethdev *ethdev = channel->ethdev;

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
		frame = xdp_convert_buff_to_frame(xdp);
		if (unlikely(!frame)) {
			printk(KERN_DEBUG "SKB build failed\n");
			goto aborted;
		}
		// NOTE: This function does a lot of things internally, check it's implementation
		// if you ever want to add support for fragmented packets
		skb = xdp_build_skb_from_frame(frame, ethdev->netdev);
		if (unlikely(IS_ERR_OR_NULL(skb))) {
			printk(KERN_DEBUG "SKB build failed\n");
			goto aborted;
		}
		// receive packet onto queue it arriverd on
		skb_record_rx_queue(skb, channel->index);
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
		// returned on tx reclaim;
		nfb_xctrl_rexmit_xsk(channel->txq.ctrl, xdp);
		break;
	case XDP_REDIRECT:
		// either redirected to userspace or returned internally
		ret = xdp_do_redirect(ethdev->netdev, xdp, xdp_prog);
		if (unlikely(ret)) {
			printk(KERN_ERR "nfb: xdp_do_redirect error ret: %d\n", ret);
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

static inline u16 nfb_xctrl_rx_xsk(struct xctrl *ctrl, struct xdp_buff **buffs, u16 nb_pkts)
{
	struct xdp_buff *buff;
	struct nc_ndp_hdr *hdrs = ctrl->rx.hdr_buffer_cpu;
	struct nc_ndp_hdr *hdr;
	u32 i;
	u16 nb_rx;
	u32 shp = ctrl->c.shp;
	u32 mhp = ctrl->c.mhp;

	// fill the card with empty buffers
	while (nfb_xctrl_rx_fill_xsk(ctrl))
		;
	nc_ndp_ctrl_sdp_flush(&ctrl->c);

	// get the amount of packets ready to be processed
	nc_ndp_ctrl_hhp_update(&ctrl->c);
	nb_rx = (ctrl->c.hhp - shp) & mhp;
	if (nb_pkts < nb_rx)
		nb_rx = nb_pkts;

	// ready packets for receive
	for (i = 0; i < nb_rx; ++i) {
		buff = ctrl->rx.xsk.xdp_ring[shp];
		hdr = &hdrs[shp];
		xsk_buff_set_size(buff, hdr->frame_len); // sets the actual data size after receive
		xsk_buff_dma_sync_for_cpu(buff, ctrl->rx.xsk.pool);
		buffs[i] = buff;
		shp = (shp + 1) & mhp;
	}

	// update ctrl state
	ctrl->c.shp = shp;
	return nb_rx;
}

int nfb_xctrl_napi_poll_rx_xsk(struct napi_struct *napi, int budget)
{
	struct nfb_xdp_queue *rxq = container_of(napi, struct nfb_xdp_queue, napi_xsk);
	struct xctrl *ctrl = rxq->ctrl;
	struct net_device *netdev = napi->dev;
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	unsigned received, i = 0;
	struct xdp_buff *xdp[NAPI_POLL_WEIGHT];

	if (unlikely(budget > NAPI_POLL_WEIGHT)) {
		printk(KERN_ERR "nfb: NAPI budget is bigger than weight. This is a driver bug.\n");
		BUG();
	}

	received = nfb_xctrl_rx_xsk(ctrl, xdp, budget);
	for (i = 0; i < received; i++) {
		if (unlikely(PAGE_SIZE < xdp[i]->data_end - xdp[i]->data_hard_start)) {
			printk(KERN_ERR "nfb: XSK got packet too large, current max size: %lu\n", PAGE_SIZE - (xdp[i]->data - xdp[i]->data_hard_start));
			xsk_buff_free(xdp[i]);
			continue;
		}
		nfb_xctrl_handle_xsk(ethdev->prog, xdp[i], rxq);
	}
	// flush sdp and shp after software processing is done
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
	struct nfb_xdp_queue *txq = container_of(napi, struct nfb_xdp_queue, napi_xsk);
	struct nfb_xdp_channel *channel = container_of(txq, struct nfb_xdp_channel, txq);
	struct xctrl *ctrl = txq->ctrl;
	struct xsk_buff_pool *pool = channel->pool;
	struct xdp_desc *buffs;
	u32 free_desc;
	u32 ready, i = 0;
	dma_addr_t dma;
	void *data;
	u32 len;
	u32 min_len = ETH_ZLEN;

	u64 last_upper_addr = ctrl->c.last_upper_addr;
	u32 sdp = ctrl->c.sdp;
	u32 mdp = ctrl->c.mdp;
	struct nc_ndp_desc *descs = ctrl->desc_buffer_virt;

	spin_lock(&ctrl->tx.tx_lock);
	{
		// free the completed buffers
		nc_ndp_ctrl_hdp_update(&ctrl->c);
		nfb_xctrl_tx_free_buffers(ctrl);
		xsk_tx_completed(pool, ctrl->tx.completed_xsk_tx);
		ctrl->tx.completed_xsk_tx = 0;

		free_desc = (ctrl->tx.fdp - sdp - 1) & mdp;

		ready = xsk_tx_peek_release_desc_batch(pool, budget);
		if (!ready) {
			goto out;
		}

		buffs = pool->tx_descs;
		for (i = 0; i < ready; i++) {
			data = xsk_buff_raw_get_data(pool, buffs[i].addr);
			dma = xsk_buff_raw_get_dma(pool, buffs[i].addr);
			len = buffs[i].len;

			// TODO: There should be a len check. using pool.frame_len somewhere when setting up the xsk queue
			if (len < min_len) { // Usable frame space is smaller than min_len.
				// printk(KERN_DEBUG "TX: %s memsetting len: %d to %d\n", __func__, len, min_len);
				memset(data + len, 0, min_len - len);
				len = min_len;
			}

			if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
				if (unlikely(free_desc < 2)) {
					printk(KERN_ERR "nfb: %s busy warning\n", __func__);
					goto out;
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
				goto out;
			}
			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_XSK;
			ctrl->tx.buffers[sdp].num_of_xsk_completions = 1 + ctrl->tx.last_napi_xsk_drops;
			ctrl->tx.last_napi_xsk_drops = 0;
			ctrl->tx.buffers[sdp].dma = dma;
			ctrl->tx.buffers[sdp].len = len;
			descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
			xsk_buff_raw_dma_sync_for_device(pool, dma, len);
			free_desc--;
			sdp = (sdp + 1) & mdp;
		}
out:
		// update ctrl
		ctrl->tx.last_napi_xsk_drops += ready - i;
		ctrl->c.sdp = sdp;

		// flush counters when done
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
		nfb_xctrl_tx_free_buffers(ctrl);
		kfree(ctrl->tx.buffers);
		break;
	default:
		break;
	}
	kfree(ctrl);
	ctrl = NULL;
}
