/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - TX DMA controller
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_TXQUEUE_H
#define NETCOPE_TXQUEUE_H

#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_txqueue {
	enum queue_type type;
};

struct nc_txqueue_status {
	unsigned _ctrl_raw;
	unsigned _stat_raw;
	unsigned long sw_pointer;
	unsigned long hw_pointer;
	unsigned long pointer_mask;
	unsigned long sd_pointer;
	unsigned long hd_pointer;
	unsigned long desc_pointer_mask;
	unsigned long timeout;
	unsigned long max_request;

	unsigned long long desc_base;
	unsigned long long pointer_base;

	unsigned ctrl_running : 1;
	unsigned stat_running : 1;
	unsigned have_dp : 1;
};

struct nc_txqueue_counters {
	unsigned long long sent;
	unsigned long long sent_bytes;
	unsigned have_bytes : 1;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txqueue *nc_txqueue_open(struct nfb_device *dev, int fdt_offset);
static inline struct nc_txqueue *nc_txqueue_open_index(struct nfb_device *dev, unsigned index, enum queue_type type);
static inline void               nc_txqueue_close(struct nc_txqueue *txqueue);
static inline void               nc_txqueue_reset_counters(struct nc_txqueue *txqueue);
static inline void               nc_txqueue_read_counters(struct nc_txqueue *txqueue, struct nc_txqueue_counters *counters);
static inline int                nc_txqueue_read_status(struct nc_txqueue *txqueue, struct nc_txqueue_status *status);

/* ~~~~[ MACROS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define TXQUEUE_REG_CONTROL      0x00
#define TXQUEUE_REG_STATUS       0x04
#define TXQUEUE_REG_SW_POINTER   0x08
#define TXQUEUE_REG_HW_POINTER   0x0C
#define TXQUEUE_REG_BUFFER_SIZE  0x10
#define TXQUEUE_REG_TIMEOUT      0x18
#define TXQUEUE_REG_MAX_REQUEST  0x1C
#define TXQUEUE_REG_DESC_BASE    0x20
#define TXQUEUE_REG_POINTER_BASE 0x28

#define TXQUEUE_REG_CNT_SENT     0x30

#define DMA_CTRL_NDP_TX_REG_SENT 0x60

#define COMP_NETCOPE_TXQUEUE_SZE  "netcope,dma_ctrl_sze_tx"
#define COMP_NETCOPE_TXQUEUE_NDP  "netcope,dma_ctrl_ndp_tx"

#define TXQUEUE_COMP_LOCK (1 << 0)

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txqueue *nc_txqueue_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_txqueue *txqueue;
	struct nfb_comp *comp;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_SZE) &&
			fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_NDP))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_txqueue));
	if (!comp)
		return NULL;

	txqueue = (struct nc_txqueue *) nfb_comp_to_user(comp);
	txqueue->type = fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_SZE) ? QUEUE_TYPE_NDP : QUEUE_TYPE_SZE;
	return txqueue;
}

static inline struct nc_txqueue *nc_txqueue_open_index(struct nfb_device *dev, unsigned index,
		enum queue_type type)
{
	int fdt_offset;
	if (type == QUEUE_TYPE_UNDEF) {
		if (nfb_comp_count(dev, COMP_NETCOPE_TXQUEUE_SZE) > 0)
			type = QUEUE_TYPE_SZE;
		else
			type = QUEUE_TYPE_NDP;
	}
	fdt_offset = nfb_comp_find(dev, (type == QUEUE_TYPE_SZE) ? COMP_NETCOPE_TXQUEUE_SZE : COMP_NETCOPE_TXQUEUE_NDP, index);
	return nc_txqueue_open(dev, fdt_offset);
}

static inline void nc_txqueue_close(struct nc_txqueue *txqueue)
{
	nfb_comp_close(nfb_user_to_comp(txqueue));
}

static inline void nc_txqueue_reset_counters(struct nc_txqueue *txqueue)
{
	if (txqueue->type == QUEUE_TYPE_NDP) {
		nfb_comp_write32(nfb_user_to_comp(txqueue), DMA_CTRL_NDP_TX_REG_SENT, 0);
	} else {
		nfb_comp_write32(nfb_user_to_comp(txqueue), TXQUEUE_REG_CNT_SENT, 1);
	}
}

static inline void _nc_txqueue_read_counters(struct nc_txqueue *txqueue, struct nc_txqueue_counters *c, int cmd)
{
	struct nfb_comp *comp = nfb_user_to_comp(txqueue);

	if (txqueue->type == QUEUE_TYPE_NDP) {
		nfb_comp_write32(nfb_user_to_comp(txqueue), DMA_CTRL_NDP_TX_REG_SENT, cmd);
		c->sent       = nfb_comp_read64(comp, DMA_CTRL_NDP_TX_REG_SENT);
		c->sent_bytes = nfb_comp_read64(comp, DMA_CTRL_NDP_TX_REG_SENT+8);
		c->have_bytes = 1;
	} else {
		c->sent       = nfb_comp_read64(comp, TXQUEUE_REG_CNT_SENT);
		c->sent_bytes = 0;
		c->have_bytes = 0;
	}
}

static inline int nc_txqueue_read_and_reset_counters(struct nc_txqueue *queue, struct nc_txqueue_counters *c)
{
	if (queue->type == QUEUE_TYPE_NDP) {
		_nc_txqueue_read_counters(queue, c, DMA_CTRL_NDP_CNTR_CMD_STRB_RST);
	} else {
		return -ENXIO;
	}
	return 0;
}

static inline void nc_txqueue_read_counters(struct nc_txqueue *queue, struct nc_txqueue_counters *c)
{
	_nc_txqueue_read_counters(queue, c, DMA_CTRL_NDP_CNTR_CMD_STRB);
}

static inline int nc_txqueue_read_status(struct nc_txqueue *txqueue, struct nc_txqueue_status *s)
{
	struct nfb_comp *comp = nfb_user_to_comp(txqueue);

	memset(s, 0, sizeof(*s));
	if (txqueue->type == QUEUE_TYPE_NDP) {
		s->_ctrl_raw     = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_CONTROL);
		s->_stat_raw     = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_STATUS);
		s->sw_pointer    = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_SDP);
		s->hw_pointer    = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_HDP);
		s->pointer_mask  = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_MDP);
		s->sd_pointer    = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_SDP);
		s->hd_pointer    = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_HDP);
		s->desc_pointer_mask = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_MDP);
		s->timeout       = nfb_comp_read32(comp, DMA_CTRL_NDP_REG_TIMEOUT);
		s->max_request   = 0;
		s->desc_base     = nfb_comp_read64(comp, DMA_CTRL_NDP_REG_DESC_BASE);
		s->pointer_base  = nfb_comp_read64(comp, DMA_CTRL_NDP_REG_UPDATE_BASE);
		s->ctrl_running  = (s->_ctrl_raw & 1) ? 1 : 0;
		s->stat_running  = (s->_stat_raw & 1) ? 1 : 0;
		s->have_dp        = 1;
	} else {
		s->_ctrl_raw     = nfb_comp_read32(comp, TXQUEUE_REG_CONTROL);
		s->_stat_raw     = nfb_comp_read32(comp, TXQUEUE_REG_STATUS);
		s->sw_pointer    = nfb_comp_read32(comp, TXQUEUE_REG_SW_POINTER);
		s->hw_pointer    = nfb_comp_read32(comp, TXQUEUE_REG_HW_POINTER);
		s->pointer_mask  = nfb_comp_read32(comp, TXQUEUE_REG_BUFFER_SIZE);
		s->timeout       = nfb_comp_read32(comp, TXQUEUE_REG_TIMEOUT);
		s->max_request   = nfb_comp_read16(comp, TXQUEUE_REG_MAX_REQUEST);
		s->desc_base     = nfb_comp_read64(comp, TXQUEUE_REG_DESC_BASE);
		s->pointer_base  = nfb_comp_read64(comp, TXQUEUE_REG_POINTER_BASE);

		s->ctrl_running  = (s->_ctrl_raw & 1) ? 1 : 0;
		s->stat_running  = (s->_stat_raw & 1) ? 1 : 0;
	}

	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_TXQUEUE_H */

