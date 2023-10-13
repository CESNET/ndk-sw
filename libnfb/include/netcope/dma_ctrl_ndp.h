/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network component library - DMA controller - NDP/v2 type
 *
 * Copyright (C) 2020-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_DMA_CTRL_NDP_H
#define NETCOPE_DMA_CTRL_NDP_H

/* Compatible strings for Device Tree */
#define COMP_NC_DMA_CTRL_NDP_RX "netcope,dma_ctrl_ndp_rx"
#define COMP_NC_DMA_CTRL_NDP_TX "netcope,dma_ctrl_ndp_tx"

#define NDP_CTRL_DESC_UPPER_ADDR(addr) (((uint64_t)addr) & 0xFFFFFFFFc0000000ull)

#define NDP_CTRL_REG_CONTROL            0x00
        #define NDP_CTRL_REG_CONTROL_STOP       0x0
        #define NDP_CTRL_REG_CONTROL_START      0x1
#define NDP_CTRL_REG_STATUS             0x04
        #define NDP_CTRL_REG_STATUS_RUNNING     0x1
#define NDP_CTRL_REG_SDP                0x10
#define NDP_CTRL_REG_SHP                0x14
#define NDP_CTRL_REG_HDP                0x18
#define NDP_CTRL_REG_HHP                0x1c
#define NDP_CTRL_REG_TIMEOUT            0x20
#define NDP_CTRL_REG_DESC               0x40
#define NDP_CTRL_REG_HDR                0x48
#define NDP_CTRL_REG_UPDATE             0x50
#define NDP_CTRL_REG_MDP                0x58
#define NDP_CTRL_REG_MHP                0x5c

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
			uint64_t phys : 34;
			uint32_t rsvd : 28;
			unsigned type : 2;
		} type0;
		struct __attribute__((__packed__)) {
			uint64_t phys : 30;
			int int0 : 1;
			int rsvd0 : 1;
			uint16_t len : 16;
			uint16_t meta : 12;
			int shrd0 : 1;
			int next0 : 1;
			unsigned type : 2;
		} type2;
	};
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
};

struct nc_ndp_ctrl_start_params {
	uint64_t desc_buffer;
	uint64_t hdr_buffer;
	uint64_t update_buffer;
	uint32_t *update_buffer_virt;
	uint32_t nb_desc;
	uint32_t nb_hdr;
};

static inline struct nc_ndp_desc nc_ndp_rx_desc0(uint64_t phys)
{
	struct nc_ndp_desc desc;

	desc.type0.phys = phys >> 30;
	desc.type0.rsvd = 0;
	desc.type0.type = 0;

	return desc;
}

static inline struct nc_ndp_desc nc_ndp_rx_desc2(uint64_t phys, uint16_t len, int next)
{
	struct nc_ndp_desc desc;

	desc.type2.type = 2;
	desc.type2.phys = phys;
	desc.type2.rsvd0 = 0;
	desc.type2.int0 = 0;
	desc.type2.len = len;
	desc.type2.meta = 0;
	desc.type2.shrd0 = 0;
	desc.type2.next0 = next;

	return desc;
}

static inline struct nc_ndp_desc nc_ndp_tx_desc0(uint64_t phys)
{
	return nc_ndp_rx_desc0(phys);
}

static inline struct nc_ndp_desc nc_ndp_tx_desc2(uint64_t phys, uint16_t len, uint16_t meta, int next)
{
	struct nc_ndp_desc desc;

	desc.type2.type = 2;
	desc.type2.phys = phys;
	desc.type2.rsvd0 = 0;
	desc.type2.int0 = 0;
	desc.type2.len = len;
	desc.type2.meta = meta;
	desc.type2.shrd0 = 0;
	desc.type2.next0 = next;

	return desc;
}

static inline void nc_ndp_ctrl_hdp_update(struct nc_ndp_ctrl *ctrl)
{
#ifdef __KERNEL__
	rmb();
#else
#ifdef _RTE_CONFIG_H_
	rte_rmb();
	rte_wmb();
#endif
#endif
	ctrl->hdp = ((uint32_t*) ctrl->update_buffer)[0];
}

static inline void nc_ndp_ctrl_hhp_update(struct nc_ndp_ctrl *ctrl)
{
#ifdef __KERNEL__
	rmb();
#else
#ifdef _RTE_CONFIG_H_
	rte_rmb();
	rte_wmb();
#endif
#endif
	ctrl->hhp = ((uint32_t*) ctrl->update_buffer)[1];
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

	const char *compatible[] = {COMP_NC_DMA_CTRL_NDP_RX, COMP_NC_DMA_CTRL_NDP_TX};

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

	ctrl->comp = nfb_comp_open(nfb, fdt_offset);
	if (ctrl->comp == NULL) {
		return -ENODEV;
	}

	ctrl->dir = i;

	return 0;
}

static inline int nc_ndp_ctrl_start(struct nc_ndp_ctrl *ctrl, struct nc_ndp_ctrl_start_params *sp)
{
	int ret;
	uint32_t status;

	/* Check for valid parameters: number of descs and hdrs must be pow2() */
	if ((sp->nb_desc & (sp->nb_desc - 1)) != 0)
		return -EINVAL;
	if (ctrl->dir == 0 && (sp->nb_hdr & (sp->nb_hdr - 1)) != 0)
		return -EINVAL;

	if (!nfb_comp_lock(ctrl->comp, 1)) {
		ret = -EEXIST;
		goto err_comp_lock;
	}

	ctrl->update_buffer = sp->update_buffer_virt;

	ctrl->mdp = sp->nb_desc - 1;
	/* INFO: kernel driver is currently using this value for TX */
	ctrl->mhp = sp->nb_hdr - 1;

	/* Set pointers to zero */
	ctrl->sdp = 0;
	ctrl->hdp = 0;
	ctrl->shp = 0;
	ctrl->hhp = 0;

	ctrl->update_buffer[0] = 0;
	if (ctrl->dir == 0)
		ctrl->update_buffer[1] = 0;

	/* INFO: driver must ensure first descriptor type is desc0 */
	ctrl->last_upper_addr = 0xFFFFFFFFFFFFFFFFull;

	status = nfb_comp_read32(ctrl->comp, NDP_CTRL_REG_STATUS);
	if (status & NDP_CTRL_REG_STATUS_RUNNING) {
		/* TODO: try to stop */
		ret = -EALREADY;
		goto err_comp_running;
	}

	/* Set address of first descriptor */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_DESC, sp->desc_buffer);

	/* Set address of RAM hwptr address */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_UPDATE, sp->update_buffer);

	if (ctrl->dir == 0)
		nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_HDR, sp->hdr_buffer);

	/* Set buffer size (mask) */
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_MDP, ctrl->mdp);
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_MHP, ctrl->mhp);

	/* Zero both buffer ptrs */
	nfb_comp_write64(ctrl->comp, NDP_CTRL_REG_SDP, 0);

	/* Timeout */
	/* TODO: let user to configure tihs value */
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_TIMEOUT, 0x4000);

	/* Start controller */
	nfb_comp_write32(ctrl->comp, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_START);
	return 0;

err_comp_running:
	nfb_comp_unlock(ctrl->comp, 1);
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

	nfb_comp_unlock(ctrl->comp, 1);
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

	prop = fdt_getprop(fdt, ctrl_params_offset, "frame_size_min", &proplen);
	if (proplen == sizeof(*prop))
		*min = fdt32_to_cpu(*prop);
	else
		ret -= 1;

	prop = fdt_getprop(fdt, ctrl_params_offset, "frame_size_max", &proplen);
	if (proplen == sizeof(*prop))
		*max = fdt32_to_cpu(*prop);
	else
		ret -= 2;

	return ret;
}

#endif /* NETCOPE_DMA_CTRL_NDP_H*/
