/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - TX DMA controller
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.cz>
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
	const char *name;
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
};

struct nc_txqueue_counters {
	unsigned long long sent;
	unsigned long long sent_bytes;
	unsigned long long discarded;
	unsigned long long discarded_bytes;

	unsigned have_bytes : 1;
	unsigned have_tx_discard : 1;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txqueue *nc_txqueue_open(struct nfb_device *dev, int fdt_offset);
static inline struct nc_txqueue *nc_txqueue_open_index(struct nfb_device *dev, unsigned index, enum queue_type type);
static inline void               nc_txqueue_close(struct nc_txqueue *txqueue);
static inline void               nc_txqueue_reset_counters(struct nc_txqueue *txqueue);
static inline void               nc_txqueue_read_counters(struct nc_txqueue *txqueue, struct nc_txqueue_counters *counters);
static inline int                nc_txqueue_read_status(struct nc_txqueue *txqueue, struct nc_txqueue_status *status);

/* ~~~~[ MACROS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

#define COMP_NETCOPE_TXQUEUE_SZE        "netcope,dma_ctrl_sze_tx"
#define COMP_NETCOPE_TXQUEUE_NDP        "netcope,dma_ctrl_ndp_tx"
#define COMP_NETCOPE_TXQUEUE_CALYPTE    "cesnet,dma_ctrl_calypte_tx"

#define TXQUEUE_COMP_LOCK (1 << 0)

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txqueue *nc_txqueue_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_txqueue *txqueue;
	struct nfb_comp *comp;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_SZE) &&
			fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_NDP) &&
			fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_CALYPTE))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_txqueue));
	if (!comp)
		return NULL;

	txqueue = (struct nc_txqueue *) nfb_comp_to_user(comp);
	if (!fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_SZE)) {
		txqueue->type = QUEUE_TYPE_SZE;
		txqueue->name = "SZE";
	} else if (!fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_NDP)) {
		txqueue->type = QUEUE_TYPE_NDP;
		txqueue->name = "NDP";
	} else if (!fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXQUEUE_CALYPTE)) {
		txqueue->type = QUEUE_TYPE_CALYPTE;
		txqueue->name = "CALYPTE";
	} else {
		txqueue->type = QUEUE_TYPE_UNDEF;
		txqueue->name = "UNDEFINED";
	}

	return txqueue;
}

static inline struct nc_txqueue *nc_txqueue_open_index(struct nfb_device *dev, unsigned index,
		enum queue_type type)
{
	int fdt_offset = -1;

	if (type == QUEUE_TYPE_UNDEF) {
		if (nfb_comp_count(dev, COMP_NETCOPE_TXQUEUE_SZE) > 0) {
			type = QUEUE_TYPE_SZE;
			fdt_offset = nfb_comp_find(dev, COMP_NETCOPE_TXQUEUE_SZE, index);
		} else if (nfb_comp_count(dev, COMP_NETCOPE_TXQUEUE_CALYPTE) > 0) {
			type = QUEUE_TYPE_CALYPTE;
			fdt_offset = nfb_comp_find(dev, COMP_NETCOPE_TXQUEUE_CALYPTE, index);
		} else {
			type = QUEUE_TYPE_NDP;
			fdt_offset = nfb_comp_find(dev, COMP_NETCOPE_TXQUEUE_NDP, index);
		}
	}

	return nc_txqueue_open(dev, fdt_offset);
}

static inline void nc_txqueue_close(struct nc_txqueue *txqueue)
{
	nfb_comp_close(nfb_user_to_comp(txqueue));
}

static inline void nc_txqueue_reset_counters(struct nc_txqueue *txqueue)
{
	if (txqueue->type == QUEUE_TYPE_NDP || txqueue->type == QUEUE_TYPE_CALYPTE) {
		nfb_comp_write32(nfb_user_to_comp(txqueue), NDP_CTRL_REG_CNTR_SENT, CNTR_CMD_RST);
	} else {
		nfb_comp_write32(nfb_user_to_comp(txqueue), SZE_CTRL_REG_CNTR_SENT, CNTR_CMD_STRB);
	}
}

static inline void _nc_txqueue_read_counters(struct nc_txqueue *txqueue, struct nc_txqueue_counters *c, int cmd)
{
	struct nfb_comp *comp = nfb_user_to_comp(txqueue);

	if (txqueue->type == QUEUE_TYPE_NDP) {
		nfb_comp_write32(nfb_user_to_comp(txqueue), NDP_CTRL_REG_CNTR_SENT, cmd);
		c->sent            = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_SENT);
		c->sent_bytes      = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_SENT+8);
		c->discarded       = 0;
		c->discarded_bytes = 0;
		c->have_bytes      = 1;
		c->have_tx_discard = 0;
	} else if (txqueue->type == QUEUE_TYPE_CALYPTE) {
		nfb_comp_write32(nfb_user_to_comp(txqueue), NDP_CTRL_REG_CNTR_SENT, cmd);
		c->sent            = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_SENT);
		c->sent_bytes      = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_SENT+8);
		c->discarded       = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_DISC);
		c->discarded_bytes = nfb_comp_read64(comp, NDP_CTRL_REG_CNTR_DISC+8);
		c->have_bytes      = 1;
		c->have_tx_discard = 1;
	} else {
		c->sent            = nfb_comp_read64(comp, SZE_CTRL_REG_CNTR_SENT);
		c->sent_bytes      = 0;
		c->discarded       = 0;
		c->discarded_bytes = 0;
		c->have_bytes      = 0;
		c->have_tx_discard = 0;
	}
}

static inline int nc_txqueue_read_and_reset_counters(struct nc_txqueue *queue, struct nc_txqueue_counters *c)
{
	if (queue->type == QUEUE_TYPE_NDP || queue->type == QUEUE_TYPE_CALYPTE) {
		_nc_txqueue_read_counters(queue, c, CNTR_CMD_STRB_RST);
	} else {
		return -ENXIO;
	}
	return 0;
}

static inline void nc_txqueue_read_counters(struct nc_txqueue *queue, struct nc_txqueue_counters *c)
{
	_nc_txqueue_read_counters(queue, c, CNTR_CMD_STRB);
}

static inline int nc_txqueue_read_status(struct nc_txqueue *txqueue, struct nc_txqueue_status *s)
{
	struct nfb_comp *comp = nfb_user_to_comp(txqueue);

	memset(s, 0, sizeof(*s));
	if (txqueue->type == QUEUE_TYPE_SZE) {
		s->_ctrl_raw     = nfb_comp_read32(comp, SZE_CTRL_REG_CONTROL);
		s->_stat_raw     = nfb_comp_read32(comp, SZE_CTRL_REG_STATUS);
		s->sw_pointer    = nfb_comp_read32(comp, SZE_CTRL_REG_SW_POINTER);
		s->hw_pointer    = nfb_comp_read32(comp, SZE_CTRL_REG_HW_POINTER);
		s->pointer_mask  = nfb_comp_read32(comp, SZE_CTRL_REG_BUFFER_SIZE);
		s->timeout       = nfb_comp_read32(comp, SZE_CTRL_REG_TIMEOUT);
		s->max_request   = nfb_comp_read16(comp, SZE_CTRL_REG_MAX_REQUEST);
		s->desc_base     = nfb_comp_read64(comp, SZE_CTRL_REG_DESC_BASE);
		s->pointer_base  = nfb_comp_read64(comp, SZE_CTRL_REG_UPDATE_BASE);

		s->ctrl_running  = (s->_ctrl_raw & 1) ? 1 : 0;
		s->stat_running  = (s->_stat_raw & 1) ? 1 : 0;
	} else if (txqueue->type == QUEUE_TYPE_NDP) {
		s->_ctrl_raw     = nfb_comp_read32(comp, NDP_CTRL_REG_CONTROL);
		s->_stat_raw     = nfb_comp_read32(comp, NDP_CTRL_REG_STATUS);
		s->sw_pointer    = nfb_comp_read32(comp, NDP_CTRL_REG_SDP);
		s->hw_pointer    = nfb_comp_read32(comp, NDP_CTRL_REG_HDP);
		s->pointer_mask  = nfb_comp_read32(comp, NDP_CTRL_REG_MDP);
		s->sd_pointer    = nfb_comp_read32(comp, NDP_CTRL_REG_SDP);
		s->hd_pointer    = nfb_comp_read32(comp, NDP_CTRL_REG_HDP);
		s->desc_pointer_mask = nfb_comp_read32(comp, NDP_CTRL_REG_MDP);
		s->timeout       = nfb_comp_read32(comp, NDP_CTRL_REG_TIMEOUT);
		s->max_request   = 0;
		s->desc_base     = nfb_comp_read64(comp, NDP_CTRL_REG_DESC_BASE);
		s->pointer_base  = nfb_comp_read64(comp, NDP_CTRL_REG_UPDATE_BASE);

		s->ctrl_running  = (s->_ctrl_raw & 1) ? 1 : 0;
		s->stat_running  = (s->_stat_raw & 1) ? 1 : 0;
	} else if (txqueue->type == QUEUE_TYPE_CALYPTE) {
		s->_ctrl_raw        = nfb_comp_read32(comp, NDP_CTRL_REG_CONTROL);
		s->_stat_raw        = nfb_comp_read32(comp, NDP_CTRL_REG_STATUS);
		s->sw_pointer       = nfb_comp_read32(comp, NDP_CTRL_REG_SHP);
		s->hw_pointer       = nfb_comp_read32(comp, NDP_CTRL_REG_HHP);
		s->pointer_mask     = nfb_comp_read32(comp, NDP_CTRL_REG_MHP);
		s->sd_pointer       = nfb_comp_read32(comp, NDP_CTRL_REG_SDP);
		s->hd_pointer       = nfb_comp_read32(comp, NDP_CTRL_REG_HDP);
		s->desc_pointer_mask = nfb_comp_read32(comp, NDP_CTRL_REG_MDP);
		s->timeout          = nfb_comp_read32(comp, NDP_CTRL_REG_TIMEOUT);
		s->pointer_base     = nfb_comp_read64(comp, NDP_CTRL_REG_UPDATE_BASE);

		s->ctrl_running     = (s->_ctrl_raw & 1) ? 1 : 0;
		s->stat_running     = (s->_stat_raw & 1) ? 1 : 0;
	}
	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_TXQUEUE_H */
