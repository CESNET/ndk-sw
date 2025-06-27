/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - common ctrl module
 *	contains functions common to both XDP modes
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include "ctrl_xdp_common.h"
#include <linux/skbuff.h>
#include <linux/pci.h>

int nfb_xctrl_start(struct xctrl *ctrl)
{
	int ret;
	// NOTE: I decided to duplicate these values in struct xctrl
	//	 just for the sake of readability.
	//	 This can be changed if desired.
	struct nc_ndp_ctrl_start_params sp = {
		.desc_buffer = ctrl->desc_buffer_dma,
		.update_buffer = ctrl->update_buffer_dma,
		.update_buffer_virt = ctrl->update_buffer_virt,
		.nb_desc = ctrl->nb_desc,
	};
	// rx only params
	if (ctrl->type == NFB_XCTRL_RX) {
		sp.hdr_buffer = ctrl->rx.hdr_buffer_dma;
		sp.nb_hdr = ctrl->rx.nb_hdr;
	}
	ret = nc_ndp_ctrl_start(&ctrl->c, &sp);
	if (!ret) {
		set_bit(XCTRL_STATUS_IS_RUNNING, &ctrl->status);
	}
	return ret;
}

netdev_tx_t nfb_xctrl_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	struct nfb_xdp_channel *channel = &ethdev->channels[skb->queue_mapping];
	struct nfb_xdp_queue *txq = &channel->txq;
	struct xctrl *ctrl = txq->ctrl;

	dma_addr_t dma;
	u32 len;
	u32 sdp;
	u32 mdp;

	struct nc_ndp_desc *descs;
	u64 last_upper_addr;
	u32 free_desc;
	u32 ret;

	skb_frag_t *frag;
	u32 nr_frags = skb_shinfo(skb)->nr_frags;
	u32 i, j;
	struct xctrl_tx_buffer *tx_buff;

	spin_lock(&ctrl->tx.tx_lock);
	{
		sdp = ctrl->c.sdp;
		mdp = ctrl->c.mdp;
		descs = ctrl->desc_buffer_virt;

		// Max 2 descriptors per frag and head will be used
		// One to update the addr and second with data
		free_desc = (ctrl->c.hdp - sdp - 1) & mdp;
		if(free_desc < (nr_frags + 1) * 2) {
			printk(KERN_WARNING "nfb: %s TX busy warning, packet dropped\n", __func__);
			goto free_locked;
		}

		if (skb->len < ETH_ZLEN) {
			skb_padto(skb, ETH_ZLEN);
			len = ETH_ZLEN;
		} else {
			len = skb_headlen(skb);
		}

		dma = dma_map_single(ctrl->dma_dev, skb->data, len, DMA_TO_DEVICE);
		if ((ret = dma_mapping_error(ctrl->dma_dev, dma))) {
			printk(KERN_ERR "nfb: %s failed to dma map skb. queue: %u err: %d\n", __func__, channel->nfb_index, ret);
			goto free_locked;
		}

		last_upper_addr = ctrl->c.last_upper_addr;
		if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
			last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
			ctrl->c.last_upper_addr = last_upper_addr;
			descs[sdp] = nc_ndp_tx_desc0(dma);
			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
			sdp = (sdp + 1) & mdp;
		}

		ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_SKB;
		ctrl->tx.buffers[sdp].skb = skb;
		ctrl->tx.buffers[sdp].dma = dma;
		ctrl->tx.buffers[sdp].len = len;
	
		if (!nr_frags) { // Single buffer
			descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
		} else { // Multi buffer
			descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 1);
		} 
		sdp = (sdp + 1) & mdp;

		// FRAG PROCESSING
		for (i = 0; i < nr_frags; ++i) {
			frag = &skb_shinfo(skb)->frags[i];
			len = skb_frag_size(frag);
			dma = skb_frag_dma_map(ctrl->dma_dev, frag, 0, len, DMA_TO_DEVICE);
			if ((ret = dma_mapping_error(ctrl->dma_dev, dma))) {
				printk(KERN_ERR "nfb: %s failed to dma map skb. queue: %u err: %d\n", __func__, channel->nfb_index, ret);
				goto free_locked_unmap;
			}

			last_upper_addr = ctrl->c.last_upper_addr;
			if (unlikely(NDP_CTRL_DESC_UPPER_ADDR(dma) != last_upper_addr)) {
				last_upper_addr = NDP_CTRL_DESC_UPPER_ADDR(dma);
				ctrl->c.last_upper_addr = last_upper_addr;
				descs[sdp] = nc_ndp_tx_desc0(dma);
				ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_DESC_TYPE0;
				sdp = (sdp + 1) & mdp;
			}

			ctrl->tx.buffers[sdp].type = NFB_XCTRL_BUFF_SKB;
			ctrl->tx.buffers[sdp].skb = NULL;
			ctrl->tx.buffers[sdp].dma = dma;
			ctrl->tx.buffers[sdp].len = len;

			if (i == nr_frags - 1) { // Last frag
				descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 0);
			} else { // Middle frag
				descs[sdp] = nc_ndp_tx_desc2(dma, len, 0, 1);
			}
			sdp = (sdp + 1) & mdp;
		}

		// Update counters
		ctrl->c.sdp = sdp;

		// Only flush when finnished
		if(!netdev_xmit_more()) {
			nc_ndp_ctrl_sdp_flush(&ctrl->c);
		}
	}
	spin_unlock(&ctrl->tx.tx_lock);

	return NETDEV_TX_OK;

free_locked_unmap:
	// Reset sdp and unmap everything that was mapped (i frags + 1 head)
	sdp = ctrl->c.sdp;
	for (j = 0; j < i + 1; j++) {
		tx_buff = &ctrl->tx.buffers[sdp];
		if (tx_buff->type == NFB_XCTRL_BUFF_SKB)
			dma_unmap_single(ctrl->dma_dev, tx_buff->dma, tx_buff->len, DMA_TO_DEVICE);

		sdp = (sdp + 1) & mdp;
	}
free_locked:
	dev_kfree_skb(skb);
	spin_unlock(&ctrl->tx.tx_lock);
	// NOTE: It is possible to return NETDEV_TX_BUSY
	// Kernel then tries to xmit packet again
	// It is slow and it is possible to completely
	// freeze system this way on error.
	return NETDEV_TX_OK;
}

int nfb_xctrl_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **xdp, u32 flags)
{
	struct nfb_ethdev *ethdev = netdev_priv(dev);
	u32 qid;
	struct nfb_xdp_channel *channel;
	struct nfb_xdp_queue *txq;
	struct xctrl *ctrl;
	struct xdp_frame *frame;
	int cnt = 0;

	// There doesn't seem to be a good way to decide which queue to use for tx other than semi random
	// NOTE: If the interrupts are implemented use the affinity logic. 
	qid = smp_processor_id() % ethdev->channel_count;
	channel = &ethdev->channels[qid];
	txq = &channel->txq;
	ctrl = txq->ctrl;
	spin_lock(&ctrl->tx.tx_lock);
	{
		// Process tx
		for (cnt = 0; cnt < n; cnt++) {
			frame = xdp[cnt];
			if (unlikely(nfb_xctrl_tx_submit_frame_needs_lock(ctrl, frame, false))) {
				// On error caller frees the frames (see ndo_xdp_xmit docs)
				break;
			}
		}
		// Flush
		if (flags & XDP_XMIT_FLUSH)
			nc_ndp_ctrl_sdp_flush(&ctrl->c);
	}
	spin_unlock(&ctrl->tx.tx_lock);
	if (cnt != n) {
		printk(KERN_ERR "%s Didn't manage to tx all packets; %d packets dropped, busy warning.\n", __func__, n - cnt);
	}
	return cnt;
}

// TODO: currently the prog pointer is held in the netdev_priv.
//	Maybe for performance there should be pointer inside each queue struct.
/**
 * @brief Replaces pointer to xdp prog.
 * 
 * @param netdev 
 * @param prog 
 * @return int 
 */
static int nfb_xdp_setup_prog(struct net_device *netdev, struct bpf_prog *prog)
{
	struct nfb_ethdev *ethdev = netdev_priv(netdev);
	struct bpf_prog *old_prog;

	// Swap program pointer
	spin_lock(&ethdev->prog_lock);
	{
		old_prog = rcu_replace_pointer(ethdev->prog, prog, lockdep_is_held(&ethdev->prog_lock));
	}
	spin_unlock(&ethdev->prog_lock);
	synchronize_rcu();

	if (old_prog) {
		bpf_prog_put(old_prog);
	}
	printk(KERN_INFO "nfb: XDP program swaped\n");
	return 0;
}

/**
 * @brief This is called when xsocket closes. Function must reallocate queue and restart ctrl on the fly.
 * 	This is also called on driver detach and AFTER close function. Meaning there can be race conditions.
 * 	That is why mutex is used.
 * 
 * @param dev 
 * @param pool 
 * @param qid 
 * @return int 
 */
static int nfb_setup_xsk_pool(struct net_device *dev, struct xsk_buff_pool *pool, u16 qid)
{
	struct nfb_ethdev *ethdev = netdev_priv(dev);
	struct nfb_xdp_channel *channel = &ethdev->channels[qid];
	u32 nfb_queue_id = channel->nfb_index;
	int ret = 0;

	if ((ret = xsk_pool_dma_map(pool, &ethdev->nfb->pci->dev, DMA_ATTR_SKIP_CPU_SYNC))) {
		printk(KERN_ERR "nfb: Failed to switch queue %d pool could't be mapped err: %d\n", nfb_queue_id, ret);
		goto exit;
	}

	channel_stop(channel);
	channel->pool = pool;
	if ((ret = channel_start_xsk(channel))) {
		printk(KERN_WARNING "nfb: Failed to start channel %d, channel unusable\n", nfb_queue_id);
		goto unmap;
	}
	printk("nfb: channel %d switched to AF_XDP operation\n", nfb_queue_id);

	return ret;

unmap:
	xsk_pool_dma_unmap(pool, DMA_ATTR_SKIP_CPU_SYNC);
exit:
	return ret;
}

/**
 * @brief This is called when xsocket closes. Function must reallocate queue and restart ctrl on the fly.
 * 	This is also called on driver detach and AFTER close function. Meaning there can be race conditions.
 * 	That is why mutex is used inside channel stop.
 * 
 * @param dev 
 * @param pool 
 * @param qid 
 * @return int 
 */
static int nfb_teardown_xsk_pool(struct net_device *dev, struct xsk_buff_pool *pool, u16 qid)
{
	struct nfb_ethdev *ethdev = netdev_priv(dev);
	struct nfb_xdp_channel *channel = &ethdev->channels[qid];
	struct xsk_buff_pool *old_pool = NULL;
	u32 nfb_queue_id = channel->nfb_index;
	u32 ret = 0;

	channel_stop(channel);
	if ((ret = channel_start_pp(channel))) {
		printk(KERN_WARNING "nfb: Failed to start channel %d, channel unusable\n", nfb_queue_id);
		goto unmap;
	}

	printk("nfb: channel %d switched to XDP operation\n", nfb_queue_id);
unmap:
	old_pool = xchg(&channel->pool, NULL);
	if (!old_pool) {
		xsk_pool_dma_unmap(old_pool, DMA_ATTR_SKIP_CPU_SYNC);
		channel->pool = NULL;
	}
	return ret;
}

/* int (*ndo_bpf)(struct net_device *dev, struct netdev_bpf *bpf);
 *	This function is used to set or query state related to XDP on the
 *	netdevice and manage BPF offload. See definition of
 *	enum bpf_netdev_command for details.
 */
int nfb_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	int ret;
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return nfb_xdp_setup_prog(dev, xdp->prog);
	case XDP_SETUP_XSK_POOL:
		if (xdp->xsk.pool) {
			ret = nfb_setup_xsk_pool(dev, xdp->xsk.pool, xdp->xsk.queue_id);
		} else {
			ret = nfb_teardown_xsk_pool(dev, xdp->xsk.pool, xdp->xsk.queue_id);
		}
		return ret;
	default:
		printk(KERN_ERR "nfb: either bad or unsupported XDP command: %d\n", xdp->command);
		return -EINVAL;
	}
}

/* int (*ndo_xsk_wakeup)(struct net_device *dev, u32 queue_id, u32 flags);
 *      This function is used to wake up the softirq, ksoftirqd or kthread
 *	responsible for sending and/or receiving packets on a specific
 *	queue id bound to an AF_XDP socket. The flags field specifies if
 *	only RX, only Tx, or both should be woken up using the flags
 *	XDP_WAKEUP_RX and XDP_WAKEUP_TX.
 */
int nfb_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags)
{
	struct nfb_ethdev *ethdev = netdev_priv(dev);
	struct nfb_xdp_channel *channel = &ethdev->channels[queue_id];
	struct nfb_xdp_queue *txq = &channel->txq;
	struct nfb_xdp_queue *rxq = &channel->rxq;

	if (flags & XDP_WAKEUP_TX) {
		local_bh_disable();
		if (!napi_if_scheduled_mark_missed(&txq->napi))
			napi_schedule(&txq->napi);
		local_bh_enable();
	}
	if (flags & XDP_WAKEUP_RX) {
		local_bh_disable();
		if (!napi_if_scheduled_mark_missed(&rxq->napi))
			napi_schedule(&rxq->napi);
		local_bh_enable();
	}

	return 0;
}
