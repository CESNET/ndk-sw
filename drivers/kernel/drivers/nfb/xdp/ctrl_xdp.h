/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * XDP driver of the NFB platform - ctrl header
 *
 * Copyright (C) 2017-2025 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#ifndef CTRL_XDP_H
#define CTRL_XDP_H
#include <linux/types.h>
#include <net/xdp_sock_drv.h>

#include "../nfb.h"
#include "../../../../../libnfb/include/netcope/dma_ctrl_ndp.h"

// Values taken from the ndk-app-minimal fw
#define NFB_XDP_MTU_MIN 64
#define NFB_XDP_MTU_MAX 16383
// Default AF_XDP value, can be made larger or smaller
#define NFB_MAX_AF_XDP_FRAGS MAX_SKB_FRAGS + 1

#define NFB_XDP_CTRL_PACKET_BURST 64
#define NFB_PP_MAX_FRAME_LEN  PAGE_SIZE - XDP_PACKET_HEADROOM - SKB_DATA_ALIGN(sizeof(struct skb_shared_info))

enum xdp_ctrl_type {
	NFB_XCTRL_RX,
	NFB_XCTRL_TX,
};

enum xdp_ctrl_tx_buff_type {
	NFB_XCTRL_BUFF_DESC_TYPE0 = 0,	// used with desc type0; no freeing required
	NFB_XCTRL_BUFF_FRAME_PP, 	// used with page pool, no dma unmap
	NFB_XCTRL_BUFF_FRAME,		// used with frames which needs to be unmapped
	NFB_XCTRL_BUFF_SKB,			// used for linux netdev tx ndo
	NFB_XCTRL_BUFF_XSK,			// used for counting the xsk frames
	NFB_XCTRL_BUFF_XSK_REXMIT,	// used for RX xsks which were retransmited 
	NFB_XCTRL_BUFF_BUG,			// used for debugging
};

// Used for freeing tx buffers after tx completes
struct xctrl_tx_buffer {
	enum xdp_ctrl_tx_buff_type type;
	union {
		struct sk_buff *skb;
		struct xdp_frame *frame;
		struct xdp_buff *xsk;
	};
	dma_addr_t dma;
	u32 len;
};

// TODO: clean up
#define XCTRL_STATUS_IS_RUNNING BIT(0)
struct xctrl {
	union {
		struct {
			/** RX - Driver handles allocation of pages for rx through page_pool
			 *  Array of pages that is 1:1 with hardware header buffer.
			 *  => if controller gets 24 headers at index i then page_ring[i]
			 *  is the start of 24 pages that hold data ready to be used
			 */
			union {
				// for page_pool operation
				struct {
					struct xdp_buff **xdp_ring;
					struct page_pool *pool;
				} pp;
				// for xsk operation
				struct {
					struct xdp_buff **xdp_ring;
					struct xsk_buff_pool *pool;
				} xsk;
			};
			// u32 php; // RX - processed header pointers
			// hdr_buff is only used on rx
			u32 mbp; // Mask buffer pointer
			u32 pbp; // Processed buffer pointer
			u32 fbp; // Filled buffer pointer 
			u32 nb_hdr;
			void *hdr_buffer_cpu;
			dma_addr_t hdr_buffer_dma;
			struct xdp_rxq_info rxq_info;
		} rx;
		struct {
			/** TX - Driver does't handle allocation on tx
			 *  Array of either xdp frames or skbs
			 *  There are limited number of ways packet can end up on tx.
			 *  1. from linux kernel stack
			 * 		=> dma map skb -> send to nic -> return from nic -> dma unmap -> dev_kfree_skb(skb);
			 * 	2. from XDP_TX - packet is on rx page_pool page
			 * 		=> get page dma -> send to nic -> return from nic -> recycle to page pool.
			 * 	3. from XDP_REDIRECT - we get xdp_frame from XDP core. XDP core handles the deallocation.
			 * 		=> dma map frame -> send to nic -> return from nic -> dma unmap -> xdp_return_frame()
			 * 		TODO: checkout xdp_return_frame_bulk()
			 */
			struct xctrl_tx_buffer *buffers;
			spinlock_t tx_lock; // TODO: checkout __netif_tx_lock()
			// used for counting completed tx frames
			// in XSK mode driver needs to return the frames
			// in the same order as it got them
			// So we count num of frames we can return from tx_buffer
			u32 completed_xsk_tx; // num of frames ready to be returned to userspace on tx_buffer_free
		} tx;
	};
	enum xdp_ctrl_type type;
	// common control buffers
	void *update_buffer_virt;
	dma_addr_t update_buffer_dma;

	u32 nb_desc;
	void *desc_buffer_virt;
	dma_addr_t desc_buffer_dma;

	unsigned long status; // status of xctrl, used to check if underliing controller is running
	struct nc_ndp_ctrl c; // underlying controller
	struct device *dma_dev; // device used for dma allocation

	// queue id in context of nfb device
	u32 nfb_queue_id;
	// queue id in context of a port
	u32 netdev_queue_id;
	u32 tu_min;
	u32 tu_max;
};

/**
 * @brief Allocates struct xdp_ctrl for basic XDP operation.
 * Use nfb_xdp_ctrl_destroy() for cleanup
 * 
 * @param netdev 
 * @param nfb_queue_id 
 * @param desc_cnt 
 * @param type 
 * @return struct xdp_ctrl* 
 */
struct xctrl *nfb_xctrl_alloc_pp(struct net_device *netdev, u32 queue_id, u32 desc_cnt, enum xdp_ctrl_type type);

/**
 * @brief Cleans up struct xdp_ctrl
 * 
 * @param ctrl 
 */
void nfb_xctrl_destroy_pp(struct xctrl *ctrl);

/**
 * @brief Allocates struct xdp_ctrl for AF_XDP operation.
 * Use nfb_xdp_ctrl_destroy() for cleanup
 * 
 * @param netdev 
 * @param nfb_queue_id 
 * @param desc_cnt 
 * @param type 
 * @return struct xdp_ctrl* 
 */
struct xctrl *nfb_xctrl_alloc_xsk(struct net_device *netdev, u32 nfb_queue_id, struct xsk_buff_pool *pool, enum xdp_ctrl_type type);

/**
 * @brief Cleans up struct xdp_ctrl
 * 
 * @param ctrl 
 */
void nfb_xctrl_destroy_xsk(struct xctrl *ctrl);

/**
 * @brief Starts the DMA
 * 
 * @param ctrl 
 * @param sp 
 * @return int 
 */
int nfb_xctrl_start(struct xctrl *ctrl);

/* netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb,
 *                               struct net_device *dev);
 *	Called when a packet needs to be transmitted.
 *	Returns NETDEV_TX_OK.  Can return NETDEV_TX_BUSY, but you should stop
 *	the queue before that can happen; it's for obsolete devices and weird
 *	corner cases, but the stack really does a non-trivial amount
 *	of useless work if you return NETDEV_TX_BUSY.
 *	Required; cannot be NULL.
 */
netdev_tx_t nfb_xctrl_start_xmit(struct sk_buff *skb, struct net_device *netdev);

/* int (*ndo_xdp_xmit)(struct net_device *dev, int n, struct xdp_frame **xdp,
 *			u32 flags);
 *	This function is used to submit @n XDP packets for transmit on a
 *	netdevice. Returns number of frames successfully transmitted, frames
 *	that got dropped are freed/returned via xdp_return_frame().
 *	Returns negative number, means general error invoking ndo, meaning
 *	no frames were xmit'ed and core-caller will free all frames.
 */
int nfb_xctrl_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **xdp, u32 flags);

/* int (*ndo_bpf)(struct net_device *dev, struct netdev_bpf *bpf);
 *	This function is used to set or query state related to XDP on the
 *	netdevice and manage BPF offload. See definition of
 *	enum bpf_netdev_command for details.
 */
int nfb_xdp(struct net_device *dev, struct netdev_bpf *xdp);

/* int (*ndo_xsk_wakeup)(struct net_device *dev, u32 queue_id, u32 flags);
 *      This function is used to wake up the softirq, ksoftirqd or kthread
 *	responsible for sending and/or receiving packets on a specific
 *	queue id bound to an AF_XDP socket. The flags field specifies if
 *	only RX, only Tx, or both should be woken up using the flags
 *	XDP_WAKEUP_RX and XDP_WAKEUP_TX.
 */
int nfb_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags);

// Napi poll functions
int nfb_xctrl_napi_poll_pp(struct napi_struct *napi, int budget);
int nfb_xctrl_napi_poll_rx_xsk(struct napi_struct *napi, int budget);
int nfb_xctrl_napi_poll_tx_xsk(struct napi_struct *napi, int budget);

#endif // CTRL_XDP_H
