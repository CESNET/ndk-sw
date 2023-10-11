/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - RX DMA controller
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.ct>
 */

#ifndef NETCOPE_RXQUEUE_H
#define NETCOPE_RXQUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "queue.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_rxqueue {
	enum queue_type type;
};

struct nc_rxqueue_status {
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

	unsigned ctrl_running  : 1;
	unsigned ctrl_discard  : 1;

	unsigned stat_running  : 1;
	unsigned stat_desc_rdy : 1;
	unsigned stat_data_rdy : 1;
	unsigned stat_ring_rdy : 1;
	unsigned have_dp : 1;
};

struct nc_rxqueue_counters {
	unsigned long long received;
	unsigned long long discarded;
	unsigned long long received_bytes;
	unsigned long long discarded_bytes;

	unsigned have_bytes : 1;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_rxqueue *nc_rxqueue_open(struct nfb_device *dev, int fdt_offset);
static inline struct nc_rxqueue *nc_rxqueue_open_index(struct nfb_device *dev, unsigned index, enum queue_type type);
static inline void               nc_rxqueue_close(struct nc_rxqueue *rxqueue);
static inline void               nc_rxqueue_reset_counters(struct nc_rxqueue *rxqueue);
static inline void               nc_rxqueue_read_counters(struct nc_rxqueue *rxqueue, struct nc_rxqueue_counters *counters);
static inline void               nc_rxqueue_read_status(struct nc_rxqueue *rxqueue, struct nc_rxqueue_status *status);

/* ~~~~[ MACROS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define COMP_NETCOPE_RXQUEUE_SZE  "netcope,dma_ctrl_sze_rx"
#define COMP_NETCOPE_RXQUEUE_NDP  "netcope,dma_ctrl_ndp_rx"

#define RXQUEUE_COMP_LOCK (1 << 0)

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_rxqueue *nc_rxqueue_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_rxqueue *rxqueue;
	struct nfb_comp *comp;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_RXQUEUE_SZE) &&
			fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_RXQUEUE_NDP))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_rxqueue));
	if (!comp)
		return NULL;

	rxqueue = (struct nc_rxqueue *) nfb_comp_to_user(comp);
	rxqueue->type = fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_RXQUEUE_SZE) ? QUEUE_TYPE_NDP : QUEUE_TYPE_SZE;
	return rxqueue;
}

static inline struct nc_rxqueue *nc_rxqueue_open_index(struct nfb_device *dev, unsigned index,
		enum queue_type type)
{
	int fdt_offset;
	if (type == QUEUE_TYPE_UNDEF) {
		if (nfb_comp_count(dev, COMP_NETCOPE_RXQUEUE_SZE) > 0)
			type = QUEUE_TYPE_SZE;
		else
			type = QUEUE_TYPE_NDP;
	}
	fdt_offset = nfb_comp_find(dev, (type == QUEUE_TYPE_SZE) ? COMP_NETCOPE_RXQUEUE_SZE : COMP_NETCOPE_RXQUEUE_NDP, index);
	return nc_rxqueue_open(dev, fdt_offset);
}

static inline void nc_rxqueue_close(struct nc_rxqueue *rxqueue)
{
	nfb_comp_close(nfb_user_to_comp(rxqueue));
}

static inline void nc_rxqueue_reset_counters(struct nc_rxqueue *rxqueue)
{
	if (rxqueue->type == QUEUE_TYPE_NDP) {
		nfb_comp_write32(nfb_user_to_comp(rxqueue), NDP_CTRL_REG_CNTR_RECV, CNTR_CMD_RST);
	} else {
		nfb_comp_write32(nfb_user_to_comp(rxqueue), SZE_CTRL_REG_CNTR_RECV, CNTR_CMD_STRB);
	}
}

static inline void _nc_rxqueue_read_counters(struct nc_rxqueue *rxqueue, struct nc_rxqueue_counters *c, int cmd)
{
	struct nfb_comp *comp = nfb_user_to_comp(rxqueue);
	if (rxqueue->type == QUEUE_TYPE_NDP) {
		nfb_comp_write32(nfb_user_to_comp(rxqueue), NDP_CTRL_REG_CNTR_RECV, cmd);
		c->received             = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_RECV);
		c->received_bytes       = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_RECV+8);
		c->discarded            = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_DISC);
		c->discarded_bytes      = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_DISC+8);
		c->have_bytes           = 1;
	} else {
		c->received             = nfb_comp_read64(comp, SZE_CTRL_REG_CNTR_RECV);
		c->received_bytes       = 0;
		c->discarded            = nfb_comp_read64(comp, SZE_CTRL_REG_CNTR_DISC);
		c->discarded_bytes      = 0;
		c->have_bytes           = 0;
	}
}

static inline int nc_rxqueue_read_and_reset_counters(struct nc_rxqueue *queue, struct nc_rxqueue_counters *c)
{
	if (queue->type == QUEUE_TYPE_NDP) {
		_nc_rxqueue_read_counters(queue, c, CNTR_CMD_STRB_RST);
	} else {
		return -ENXIO;
	}
	return 0;
}

static inline void nc_rxqueue_read_counters(struct nc_rxqueue *queue, struct nc_rxqueue_counters *c)
{
	_nc_rxqueue_read_counters(queue, c, CNTR_CMD_STRB);
}

static inline void nc_rxqueue_read_status(struct nc_rxqueue *rxqueue, struct nc_rxqueue_status *s)
{
	struct nfb_comp *comp = nfb_user_to_comp(rxqueue);

	memset(s, 0, sizeof(*s));
	if (rxqueue->type == QUEUE_TYPE_NDP) {
		s->_ctrl_raw         = nfb_comp_read32(comp, NDP_CTRL_REG_CONTROL);
		s->_stat_raw         = nfb_comp_read32(comp, NDP_CTRL_REG_STATUS);
		s->sw_pointer        = nfb_comp_read32(comp, NDP_CTRL_REG_SHP);
		s->hw_pointer        = nfb_comp_read32(comp, NDP_CTRL_REG_HHP);
		s->pointer_mask      = nfb_comp_read32(comp, NDP_CTRL_REG_MHP);
		s->sd_pointer        = nfb_comp_read32(comp, NDP_CTRL_REG_SDP);
		s->hd_pointer        = nfb_comp_read32(comp, NDP_CTRL_REG_HDP);
		s->desc_pointer_mask = nfb_comp_read32(comp, NDP_CTRL_REG_MDP);
		s->timeout           = nfb_comp_read32(comp, NDP_CTRL_REG_TIMEOUT);
		s->max_request       = 0;
		s->desc_base         = nfb_comp_read64(comp, NDP_CTRL_REG_DESC_BASE);
		s->pointer_base      = nfb_comp_read64(comp, NDP_CTRL_REG_UPDATE_BASE);
		s->ctrl_running      = (s->_ctrl_raw & 1) ? 1 : 0;
		s->stat_running      = (s->_stat_raw & 1) ? 1 : 0;
	} else {
		s->_ctrl_raw        = nfb_comp_read32(comp, SZE_CTRL_REG_CONTROL);
		s->_stat_raw        = nfb_comp_read32(comp, SZE_CTRL_REG_STATUS);
		s->sw_pointer       = nfb_comp_read32(comp, SZE_CTRL_REG_SW_POINTER);
		s->hw_pointer       = nfb_comp_read32(comp, SZE_CTRL_REG_HW_POINTER);
		s->pointer_mask     = nfb_comp_read32(comp, SZE_CTRL_REG_BUFFER_SIZE);
		s->timeout          = nfb_comp_read32(comp, SZE_CTRL_REG_TIMEOUT);
		s->max_request      = nfb_comp_read16(comp, SZE_CTRL_REG_MAX_REQUEST);
		s->desc_base        = nfb_comp_read64(comp, SZE_CTRL_REG_DESC_BASE);
		s->pointer_base     = nfb_comp_read64(comp, SZE_CTRL_REG_UPDATE_BASE);
		s->ctrl_running     = (s->_ctrl_raw & 1) ? 1 : 0;
		s->ctrl_discard     = (s->_ctrl_raw & 2) ? 1 : 0;
		//s->ctrl_vfid        = (reg >> 24) & 0xFF
		s->stat_running     = (s->_stat_raw & 1) ? 1 : 0;
		s->stat_desc_rdy    = (s->_stat_raw & 2) ? 1 : 0;
		s->stat_data_rdy    = (s->_stat_raw & 4) ? 1 : 0;
		s->stat_ring_rdy    = (s->_stat_raw & 8) ? 1 : 0;
	}
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_RXQUEUE_H */
