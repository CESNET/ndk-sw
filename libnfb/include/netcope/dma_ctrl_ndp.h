/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network component library - DMA controller - NDP/v2 type, CALYPTE/v3 type
 *
 * Copyright (C) 2020-2023 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Vladislav Valek <valekv@cesnet.cz>
 */

#ifndef NETCOPE_DMA_CTRL_NDP_H
#define NETCOPE_DMA_CTRL_NDP_H

/* Compatible strings for Device Tree */
#define COMP_NC_DMA_CTRL_NDP_RX "netcope,dma_ctrl_ndp_rx"
#define COMP_NC_DMA_CTRL_NDP_TX "netcope,dma_ctrl_ndp_tx"
#define COMP_NC_DMA_CTRL_CALYPTE_RX "cesnet,dma_ctrl_calypte_rx"
#define COMP_NC_DMA_CTRL_CALYPTE_TX "cesnet,dma_ctrl_calypte_tx"

#define DMA_TYPE_MEDUSA  2
#define DMA_TYPE_CALYPTE 3

#define COMP_NC_DMA_CTRL_LOCK 1

// ---------------- NDP/Calypte common registers -------
#define NDP_CTRL_REG_CONTROL            0x00
        #define NDP_CTRL_REG_CONTROL_STOP       0x0
        #define NDP_CTRL_REG_CONTROL_START      0x1
#define NDP_CTRL_REG_STATUS             0x04
        #define NDP_CTRL_REG_STATUS_RUNNING     0x1
#define NDP_CTRL_REG_SDP                0x10
#define NDP_CTRL_REG_SHP                0x14
#define NDP_CTRL_REG_HDP                0x18
#define NDP_CTRL_REG_HHP                0x1C
#define NDP_CTRL_REG_DESC_BASE          0x40
#define NDP_CTRL_REG_HDR_BASE           0x48
#define NDP_CTRL_REG_UPDATE_BASE        0x50
#define NDP_CTRL_REG_MDP                0x58
#define NDP_CTRL_REG_MHP                0x5C

// --------------- NDP specific registers --------------
#define NDP_CTRL_REG_TIMEOUT            0x20

// -------------- NDP/Calypte Counters -----------------
// Processed packets on TX
#define NDP_CTRL_REG_CNTR_SENT          0x60
// Processed packets on RX
#define NDP_CTRL_REG_CNTR_RECV          0x60
// Discarded packets
#define NDP_CTRL_REG_CNTR_DISC          0x70

// -------------- Data transmission parameters ---------
#define NDP_CTRL_UPDATE_SIZE    4
#define NDP_PACKET_HEADER_SIZE  4

#define NDP_CALYPTE_METADATA_NOT_VALID      0x400
#define NDP_CALYPTE_METADATA_HDR_SIZE_MASK  0xff

#define NDP_TX_CALYPTE_BLOCK_SIZE (32u)
#define NDP_RX_CALYPTE_BLOCK_SIZE (128u)

#define NDP_CTRL_DESC_UPPER_ADDR(addr) (((uint64_t)addr) & 0xFFFFFFFFc0000000ull)

#define NDP_CTRL_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct nc_ndp_hdr {
	uint16_t frame_len;
	uint8_t hdr_len;
	unsigned meta : 4;
	/* TODO: layout! */
	unsigned reserved: 2;
	unsigned free_desc : 2;
} __attribute((packed));

struct nc_ndp_desc {
	union {
		struct __attribute__((__packed__)) {
			unsigned phys_lo : 32;
			unsigned phys_hi : 2;
			unsigned rsvd : 28;
			unsigned type : 2;
		} type0;
		struct __attribute__((__packed__)) {
			unsigned phys : 30;
			int int0 : 1;
			int rsvd0 : 1;
			unsigned len : 16;
			unsigned meta : 12;
			int shrd0 : 1;
			int next0 : 1;
			unsigned type : 2;
		} type2;
		struct __attribute__((__packed__)) {
			unsigned phys0 : 30;
			int int0 : 1;
			int int1 : 1;
			unsigned phys1 : 30;
			unsigned type : 2;
		} type3;
	} d;
} __attribute__((__packed__));

struct nc_calypte_hdr {
	uint16_t frame_len;
	uint16_t frame_ptr;
	unsigned valid : 1;
	unsigned reserved: 7;
	unsigned metadata : 24;
} __attribute__((__packed__));

struct nc_ndp_ctrl {
	/* public members */
	uint64_t last_upper_addr;
	uint32_t mdp;
	uint32_t mhp;
	uint32_t sdp;
	uint32_t hdp;
	uint32_t shp;
	uint32_t hhp;

	/* private members */
	struct nfb_comp *comp;
	uint32_t *update_buffer;
	uint32_t dir : 1;

	uint8_t type;
};

struct nc_ndp_ctrl_start_params {
	uint64_t desc_buffer;
	uint64_t data_buffer;
	uint64_t hdr_buffer;
	uint64_t update_buffer;
	uint32_t *update_buffer_virt;
	uint32_t nb_data;
	uint32_t nb_desc;
	uint32_t nb_hdr;
};

static inline struct nc_ndp_desc nc_ndp_rx_desc0(uint64_t phys)
{
	struct nc_ndp_desc desc;

	desc.d.type0.phys_lo = phys >> (30);
	desc.d.type0.phys_hi = phys >> (30 + 32);

	desc.d.type0.rsvd = 0;
	desc.d.type0.type = 0;

	return desc;
}

static inline struct nc_ndp_desc nc_ndp_rx_desc2(uint64_t phys, uint16_t len, int next)
{
	struct nc_ndp_desc desc;

	desc.d.type2.type = 2;
	desc.d.type2.phys = phys;
	desc.d.type2.rsvd0 = 0;
	desc.d.type2.int0 = 0;
	desc.d.type2.len = len;
	desc.d.type2.meta = 0;
	desc.d.type2.shrd0 = 0;
	desc.d.type2.next0 = next;

	return desc;
}

static inline struct nc_ndp_desc nc_ndp_rx_desc3(uint64_t phys0, uint64_t phys1)
{
        struct nc_ndp_desc desc;

        desc.d.type3.type = 3;
        desc.d.type3.phys0 = phys0;
        desc.d.type3.phys1 = phys1;
        desc.d.type3.int0 = 0;
        desc.d.type3.int1 = 0;

        return desc;
}

static inline struct nc_ndp_desc nc_ndp_tx_desc0(uint64_t phys)
{
	return nc_ndp_rx_desc0(phys);
}

static inline struct nc_ndp_desc nc_ndp_tx_desc2(uint64_t phys, uint16_t len, uint16_t meta, int next)
{
	struct nc_ndp_desc desc;

	desc.d.type2.type = 2;
	desc.d.type2.phys = phys;
	desc.d.type2.rsvd0 = 0;
	desc.d.type2.int0 = 0;
	desc.d.type2.len = len;
	desc.d.type2.meta = meta;
	desc.d.type2.shrd0 = 0;
	desc.d.type2.next0 = next;

	return desc;
}

static inline void nc_ndp_ctrl_hdp_update(struct nc_ndp_ctrl *ctrl)
{
	if (ctrl->type == DMA_TYPE_MEDUSA) {
#ifdef __KERNEL__
		rmb();
#else
#ifdef _RTE_CONFIG_H_
		rte_rmb();
		rte_wmb();
#endif
#endif
		ctrl->hdp = ((uint32_t*) ctrl->update_buffer)[0];
	} else if (ctrl->type == DMA_TYPE_CALYPTE) {
		ctrl->hdp = (nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_HDP)) & ctrl->mdp;
	}
}

static inline void nc_ndp_ctrl_hhp_update(struct nc_ndp_ctrl *ctrl)
{
	if (ctrl->type == DMA_TYPE_MEDUSA) {
#ifdef __KERNEL__
		rmb();
#else
#ifdef _RTE_CONFIG_H_
		rte_rmb();
		rte_wmb();
#endif
#endif
		ctrl->hhp = ((uint32_t*) ctrl->update_buffer)[1];
	} else if (ctrl->type == DMA_TYPE_CALYPTE) {
		ctrl->hhp = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_HHP);
	}
}

static inline void nc_ndp_ctrl_hp_update(struct nc_ndp_ctrl *ctrl)
{
	uint64_t hwpointers;

	hwpointers = nfb_comp_read64(ctrl->comp, NDP_CTRL_REG_HDP);
	ctrl->hdp = ((uint32_t)hwpointers) & ctrl->mdp;
	ctrl->hhp = ((uint32_t)(hwpointers >> 32)) & ctrl->mhp;
}

static inline void nc_ndp_ctrl_sp_flush(struct nc_ndp_ctrl *ctrl)
{
#ifdef __KERNEL__
	wmb();
#else
#ifdef _RTE_CONFIG_H_
	rte_wmb();
#endif
#endif
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP, ctrl->sdp | (((uint64_t) ctrl->shp) << 32));
}

static inline void nc_ndp_ctrl_sdp_flush(struct nc_ndp_ctrl *ctrl)
{
#ifdef __KERNEL__
	wmb();
#else
#ifdef _RTE_CONFIG_H_
	rte_wmb();
#endif
#endif
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_SDP, ctrl->sdp);
}

static inline int nc_ndp_ctrl_open(struct nfb_device* nfb, int fdt_offset, struct nc_ndp_ctrl *ctrl)
{
	int ret;
	unsigned int i;

	const char *compatible[] = {
		COMP_NC_DMA_CTRL_NDP_RX,
		COMP_NC_DMA_CTRL_NDP_TX,
		COMP_NC_DMA_CTRL_CALYPTE_RX,
		COMP_NC_DMA_CTRL_CALYPTE_TX
	};

	if (nfb == NULL)
		return -EINVAL;

	i = 0;
	do {
		ret = fdt_node_check_compatible(nfb_get_fdt(nfb), fdt_offset, compatible[i]);
		if (ret == 0)
			break;
	} while (++i < NDP_CTRL_ARRAY_SIZE(compatible));

	if (ret)
		return -EINVAL;

	if (i < 2)
		ctrl->type = DMA_TYPE_MEDUSA;
	else
		ctrl->type = DMA_TYPE_CALYPTE;

	ctrl->comp = nfb_comp_open(nfb, fdt_offset);
	if (ctrl->comp == NULL) {
		return -ENODEV;
	}

	ctrl->dir = i % 2;

	return 0;
}

static inline int nc_ndp_ctrl_start(struct nc_ndp_ctrl *ctrl, struct nc_ndp_ctrl_start_params *sp)
{
	int ret;
	uint32_t status;
	uint32_t nb_d = 0;
	uint64_t d_buffer = 0;

	if (ctrl->type == DMA_TYPE_MEDUSA) {
		nb_d = sp->nb_desc;
		d_buffer = sp->desc_buffer;
	} else if (ctrl->type == DMA_TYPE_CALYPTE) {
		nb_d = sp->nb_data;
		d_buffer = sp->data_buffer;
	} else {
		return -ENODEV;
	}

	/* Check for valid parameters: number of descs and hdrs must be pow2() */
	if ((nb_d & (nb_d - 1)) != 0)
		return -EINVAL;
	if (ctrl->dir == 0 && (sp->nb_hdr & (sp->nb_hdr - 1)) != 0)
		return -EINVAL;


	ret = nfb_comp_trylock(ctrl->comp, COMP_NC_DMA_CTRL_LOCK, 0);
	if (ret != 0) {
		if (ret == -EBUSY)
			ret = -EEXIST;
		goto err_comp_lock;
	}

	if (ctrl->type == DMA_TYPE_MEDUSA)
		ctrl->update_buffer = sp->update_buffer_virt;

	if (!(ctrl->type == DMA_TYPE_CALYPTE && ctrl->dir == 1)) {
		ctrl->mdp = nb_d - 1;
		/* INFO: kernel driver is currently using this value for TX */
		ctrl->mhp = sp->nb_hdr - 1;
	} else {
		ctrl->mdp = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_MDP);
		ctrl->mhp = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_MHP);
	}

	/* Set pointers to zero */
	ctrl->sdp = 0;
	ctrl->hdp = 0;
	ctrl->shp = 0;
	ctrl->hhp = 0;

	if (ctrl->type == DMA_TYPE_MEDUSA) {
		ctrl->update_buffer[0] = 0;
		if (ctrl->dir == 0)
			ctrl->update_buffer[1] = 0;
	}

	/* INFO: driver must ensure first descriptor type is desc0 */
	ctrl->last_upper_addr = 0xFFFFFFFFFFFFFFFFull;

	status = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_STATUS);
	if (status & NDP_CTRL_REG_STATUS_RUNNING) {
		/* TODO: try to stop */
		ret = -EALREADY;
		goto err_comp_running;
	}

	/* Set address of first descriptor */
	if (!(ctrl->type == DMA_TYPE_CALYPTE && ctrl->dir == 1))
		nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_DESC_BASE, d_buffer);

	/* Set address of RAM hwptr address */
	if (ctrl->type == DMA_TYPE_MEDUSA)
		nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_UPDATE_BASE, sp->update_buffer);

	if (ctrl->dir == 0)
		nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_HDR_BASE, sp->hdr_buffer);

	/* Set buffer size (mask) */
	if (!(ctrl->type == DMA_TYPE_CALYPTE && ctrl->dir == 1)) {
		nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_MDP, ctrl->mdp);
		nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_MHP, ctrl->mhp);
	}

	/* Zero both buffer ptrs */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP, 0);

	/* Timeout */
	/* TODO: let user to configure tihs value */
	if (ctrl->type == DMA_TYPE_MEDUSA)
		nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_TIMEOUT, 0x4000);

	/* Start controller */
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_START);
	return 0;

err_comp_running:
	nfb_comp_unlock(ctrl->comp, COMP_NC_DMA_CTRL_LOCK);
err_comp_lock:
	return ret;
}

static inline int _nc_ndp_ctrl_stop(struct nc_ndp_ctrl *ctrl, int force)
{
	int ret = 0;
	unsigned int counter = 0;
	uint32_t status;
	uint32_t hdp_new;

	if (ctrl->dir != 0) {
		hdp_new = ctrl->hdp;
		nc_ndp_ctrl_hdp_update(ctrl);
		if (ctrl->sdp != ctrl->hdp) {
			if (force) {
				ret = -EBUSY;
			} else {
				return hdp_new == ctrl->hdp ? -EAGAIN : -EINPROGRESS;
			}
		}
	}

	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_STOP);

	// Purpose: The RX DMA can pass some packets during the stop process. So HW pointers will surpass
	// the SW pointers even when software is no longer accepting data.
	if (ctrl->type == DMA_TYPE_CALYPTE && ctrl->dir == 0) {
		nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_SDP,
			nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_HDP));
		nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_SHP,
			nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_HHP));
	}

	if (ret == 0)
		ret = -EAGAIN;

	do {
		status = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_STATUS);
		if (!(status & NDP_CTRL_REG_STATUS_RUNNING)) {
			ret = 0;
			break;
		}
	} while (counter++ < 100);

	if (!force && ret != 0)
		goto err_busy;

	/* TODO: read hdp/hhp from register and wait for same value in update buffer */

	nfb_comp_unlock(ctrl->comp, COMP_NC_DMA_CTRL_LOCK);
	return ret;

err_busy:
	return ret;
}

static inline int nc_ndp_ctrl_stop_force(struct nc_ndp_ctrl *ctrl)
{
	return _nc_ndp_ctrl_stop(ctrl, 1);
}

static inline int nc_ndp_ctrl_stop(struct nc_ndp_ctrl *ctrl)
{
	return _nc_ndp_ctrl_stop(ctrl, 0);
}

static inline void nc_ndp_ctrl_close(struct nc_ndp_ctrl *ctrl)
{
	nfb_comp_close(ctrl->comp);
	ctrl->comp = NULL;
}

static inline int nc_ndp_ctrl_get_mtu(struct nc_ndp_ctrl *ctrl, unsigned int *min, unsigned int *max)
{
	int ret = 0;
	const void *fdt = nfb_get_fdt(nfb_comp_get_device(ctrl->comp));
	int ctrl_offset, ctrl_params_offset;
	const fdt32_t *prop;
	int proplen;

        ctrl_offset = fdt_path_offset(fdt, nfb_comp_path(ctrl->comp));
        ctrl_params_offset = fdt_node_offset_by_phandle_ref(fdt, ctrl_offset, "params");

	prop = (const fdt32_t*) fdt_getprop(fdt, ctrl_params_offset, "frame_size_min", &proplen);
	if (proplen == sizeof(*prop))
		*min = fdt32_to_cpu(*prop);
	else
		ret -= 1;

	prop = (const fdt32_t*) fdt_getprop(fdt, ctrl_params_offset, "frame_size_max", &proplen);
	if (proplen == sizeof(*prop))
		*max = fdt32_to_cpu(*prop);
	else
		ret -= 2;

	return ret;
}

#endif /* NETCOPE_DMA_CTRL_NDP_H*/
