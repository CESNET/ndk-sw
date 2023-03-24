/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - general MDIO access
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Peresini <xperes00@stud.fit.vutbr.cz>
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jiri Matousek <matousek@cesnet.cz>
 */

#ifndef NETCOPE_MDIO_H
#define NETCOPE_MDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ INCLUDES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#include "mdio_dmap.h"
#include "mdio_ctrl.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

enum mdio_pma_enc {
	MDIO_PMA_ENC_NRZ = 0,
	MDIO_PMA_ENC_PAM4 = 1,
};

struct nc_mdio {
	int (*mdio_read)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr);
	int (*mdio_write)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr, uint16_t val);
	int pcspma_is_e_tile;
	int pcspma_is_f_tile;
	int pma_lanes;                   /* Number of PMA lanes */
	enum mdio_pma_enc link_encoding; /* Line modulation */
	int speed;                       /* Speed in Gbps */
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_mdio *nc_mdio_open (const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam);
static inline void         nc_mdio_close(struct nc_mdio *mdio);
static inline int          nc_mdio_read (struct nc_mdio *mdio, int prtad, int devad, uint16_t addr);
static inline int          nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Get F-tile EHIP configuration and fill corresponding fields in the nc_mdio structure */
static inline void nc_mdio_ftile_config(struct nc_mdio *mdio)
{
	const int EHIP_CFG_REG = (0x100 >> 2);
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t reg;

	reg = nc_mdio_dmap_drp_read(comp, 0, 0, EHIP_CFG_REG);
	mdio->pma_lanes = ((reg >> 21) & 0xf);
	mdio->link_encoding = ((reg >> 9) & 0x1) ? MDIO_PMA_ENC_PAM4 : MDIO_PMA_ENC_NRZ;
	switch ((reg >> 5) & 0x7)
	{
		case 0: mdio->speed = 10; break;
		case 1: mdio->speed = 25; break;
		case 2: mdio->speed = 40; break;
		case 3: mdio->speed = 50; break;
		case 4: mdio->speed = 100; break;
		case 5: mdio->speed = 200; break;
		case 6: mdio->speed = 400; break;
		default: mdio->speed = 0; break;
	}
}

static inline struct nc_mdio *nc_mdio_open(const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam)
{
	struct nc_mdio *mdio;
	struct nfb_comp *comp;
	const struct fdt_property *prop;
	int e_tile = 0;
	int f_tile = 0;

	prop = fdt_get_property(nfb_get_fdt(dev), fdt_offset_ctrlparam, "ip-name", NULL);
	if (prop) {
		if (strcmp(prop->data, "E_TILE") == 0) {
			e_tile = 1;
		} else if (strcmp(prop->data, "F_TILE") == 0) {
			f_tile = 1;
		}
	}

	comp = nc_mdio_ctrl_open_ext(dev, fdt_offset, sizeof(struct nc_mdio));
	if (comp) {
		mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
		mdio->mdio_read = nc_mdio_ctrl_read;
		mdio->mdio_write = nc_mdio_ctrl_write;
		mdio->pcspma_is_e_tile = e_tile;
		mdio->pcspma_is_f_tile = f_tile;
		if (f_tile)
			nc_mdio_ftile_config(mdio);
		return mdio;
	}


	comp = nc_mdio_dmap_open_ext(dev, fdt_offset, sizeof(struct nc_mdio));
	if (comp) {
		mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
		mdio->mdio_read = nc_mdio_dmap_read;
		mdio->mdio_write = nc_mdio_dmap_write;
		mdio->pcspma_is_e_tile = e_tile;
		mdio->pcspma_is_f_tile = f_tile;
		if (f_tile)
			nc_mdio_ftile_config(mdio);
		return mdio;
	}

	return NULL;
}

static inline void nc_mdio_ftile_reset(struct nc_mdio *mdio, int prtad, int rx, int enable)
{
	const int EHIP_RESET_REG = (0x108 >> 2);
	const int EHIP_RACK_REG = (0x10C >> 2);
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t data = enable ? 1 : 0; // enable or disable reset?
	uint32_t rst_ack;
	int retries = 0;

	if (rx) {
		data <<= 2; // RX reset on bit 2
	} else {
		data <<= 1; // TX reset on bit 1
	}
	// see https://cdrdv2.intel.com/v1/dl/getContent/637401 for eth_reset register (0x108) details
	nc_mdio_dmap_drp_write(comp, prtad, 0, EHIP_RESET_REG, data);
	/* Wait until the reset is acked */
	do {
		rst_ack = (nc_mdio_dmap_drp_read(comp, prtad, 0, EHIP_RACK_REG) & 0x6);
	} while ((rst_ack != ((~data) & 0x6)) && (++retries < 1000000));
}

static inline void nc_mdio_ftile_fgt_attribute_write(struct nc_mdio *mdio, int prtad, uint16_t data, uint16_t lane, uint8_t opcode)
{
#define FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ (1 << 15)
#define FGT_ATTRIBUTE_ACCESS_OPTION_RESET       (1 << 14)
#define FGT_ATTRIBUTE_ACCESS_OPTION_SET         (1 << 13)
#define FGT_LANE_NUMBER_REG                     (0xffffc >> 2)

#define fgt_attribute_access(opcode, lane, options, data) ((((uint32_t) (data)) << 16) | (options) |  ((lane & 0x3) << 8) | ((opcode) & 0xFF))
	uint32_t LINK_MNG_SIDE_CPI_REGS;
	uint32_t PHY_SIDE_CPI_REGS;
	uint32_t CPI_BUSY_REGS;
	uint32_t reg;
	uint32_t page;
	uint32_t options;
	uint32_t service_req;
	uint32_t reset;
	uint32_t cpi_stat;

	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	int retries = 0;
	int phy_lane;

	/* FGT Attribute Access addresses according to Intel documentation:
 	 *    https://www.intel.com/content/www/us/en/docs/programmable/683872/22-3-4-2-0/fgt-attribute-access-method.html
 	 * Our designs currently implement page-based mapping, i.e., registers
 	 * for each channel are mapped to the same addresses, but on separate
 	 * pages.
 	 * If required, the page-based mapping can be reimplemented in an
 	 * Intel-like fashion.
 	 */
	int channel = 0; // mapping for each channel follows channel 0
	page = lane + 1; // page number corresponds to lane number + 1
	LINK_MNG_SIDE_CPI_REGS = 0x0009003c + (channel >> 2) * 0x00400000;
	PHY_SIDE_CPI_REGS      = 0x00090040 + (channel >> 2) * 0x00400000;
	CPI_BUSY_REGS          = 0x00090044 + (channel >> 2) * 0x00400000;

	/* F-Tile addresses are aligned to 4-byte boundary, thus have to be
	 * shifted to right by 2 bits
	 */
	LINK_MNG_SIDE_CPI_REGS >>= 2;
	PHY_SIDE_CPI_REGS >>= 2;
	CPI_BUSY_REGS >>= 2;

	/* Get physical index of the FGT channel */
	phy_lane = (nc_mdio_dmap_drp_read(comp, prtad, page, FGT_LANE_NUMBER_REG) & 0x3);

	do {
		cpi_stat = (nc_mdio_dmap_drp_read(comp, prtad, page, CPI_BUSY_REGS) & 0xffff);
	} while ((cpi_stat != 0xf) && (++retries < 1000));

	retries = 0;
	options = FGT_ATTRIBUTE_ACCESS_OPTION_SET | FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ;
	reg = fgt_attribute_access(opcode, phy_lane, options, data);
	nc_mdio_dmap_drp_write(comp, prtad, page, LINK_MNG_SIDE_CPI_REGS, reg);
	do {
		reg = nc_mdio_dmap_drp_read(comp, prtad, page, PHY_SIDE_CPI_REGS);
		service_req = reg & FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ;
		reset = reg & FGT_ATTRIBUTE_ACCESS_OPTION_RESET;
	} while ((service_req == 0 || reset != 0) && ++retries < 1000);

	retries = 0;
	options = FGT_ATTRIBUTE_ACCESS_OPTION_SET;
	reg = fgt_attribute_access(opcode, phy_lane, options, data);
	nc_mdio_dmap_drp_write(comp, prtad, page, LINK_MNG_SIDE_CPI_REGS, reg);
	do {
		reg = nc_mdio_dmap_drp_read(comp, prtad, page, PHY_SIDE_CPI_REGS);
		service_req = reg & FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ;
		reset = reg & FGT_ATTRIBUTE_ACCESS_OPTION_RESET;
	} while ((service_req != 0 || reset != 0) && ++retries < 1000);
}

static inline void nc_mdio_fixup_ftile_set_mode(struct nc_mdio *mdio, int prtad, uint16_t val)
{
	int i;
	uint16_t media_type;

	if (mdio->link_encoding == MDIO_PMA_ENC_NRZ) 
		return; /* Not necessary to change media mode for NRZ modes */
	if (
		(val == 0x5B || val == 0x5C) ||  /* 400GBASE-R8 */
		(val >= 0x52 && val <= 0x55) ||  /* 200GBASE-R4 */
		(val == 0x4A)                ||  /* 100GBASE-R2 */
		(val >= 0x42 && val <= 0x45))    /*  50GBASE-R1 */
	{
		media_type = 0x14; // optical
	/* all other media types */
	} else {
		media_type = 0x10; // default = -CR 
	}

	for (i = 0; i < mdio->pma_lanes; i++) {
		nc_mdio_ftile_fgt_attribute_write(mdio, prtad, media_type, i, 0x64);
	}

	nc_mdio_ftile_reset(mdio, prtad, 1, 1); // assert RX reset
	nc_mdio_ftile_reset(mdio, prtad, 1, 0); // deassert reset
}

static inline void nc_mdio_fixup_ftile_set_loopback(struct nc_mdio *mdio, int prtad, int enable)
{
	int i;
	uint16_t data = enable ? 0x6 : 0x0; // enable or disable loopback?

	nc_mdio_ftile_reset(mdio, prtad, 1, 1); // assert RX reset

	for (i = 0; i < mdio->pma_lanes; i++) {
		nc_mdio_ftile_fgt_attribute_write(mdio, prtad, data, i, 0x40);
	}
	nc_mdio_ftile_reset(mdio, prtad, 1, 0); // deassert RX reset
}

static inline void nc_mdio_etile_pma_attribute_write(struct nc_mdio *mdio, int prtad, uint16_t lane, uint16_t code_addr, uint16_t code_val)
{
	uint32_t page = lane + 1; // page number corresponds to lane number + 1

	/* PMA Avalon memory-mapped interface
	 *   See https://www.intel.com/content/www/us/en/docs/programmable/683723/current/pma-attribute-details.html
	 *       https://www.intel.com/content/www/us/en/docs/programmable/683723/current/reconfiguring-the-duplex-pma-using-the.html
	 */
	uint32_t PMA_ATTR_CODE_VAL_L        = 0x84;
	uint32_t PMA_ATTR_CODE_VAL_H        = 0x85;
	uint32_t PMA_ATTR_CODE_ADDR_L       = 0x86;
	uint32_t PMA_ATTR_CODE_ADDR_H       = 0x87;
	uint32_t PMA_ATTR_CODE_REQ_STATUS_L = 0x8A;
	uint32_t PMA_ATTR_CODE_REQ_STATUS_H = 0x8B;
	uint32_t PMA_ATTR_CODE_REQ          = 0x90;

	uint32_t code_val_l  =  code_val & 0xFF;
	uint32_t code_val_h  = (code_val >> 8) & 0xFF;
	uint32_t code_addr_l =  code_addr & 0xFF;
	uint32_t code_addr_h = (code_addr >> 8) & 0xFF;

	struct nfb_comp *comp = nfb_user_to_comp(mdio);

	uint32_t ret;
	int retries = 0;

	/* Set PMA attribute code address and data */
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_VAL_L, code_val_l);
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_VAL_H, code_val_h);
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_ADDR_L, code_addr_l);
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_ADDR_H, code_addr_h);
	/* Issue PMA attribute code request */
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_REQ, 1);
	/* Verify that PMA attribute code request has been sent */
	ret = nc_mdio_dmap_drp_read(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_L);
	if (((ret >> 7) & 0x01) != 1) {
		return;
	}
	/* Wait until PMA attribute code request is completed */
	do {
		ret = nc_mdio_dmap_drp_read(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_H);
	} while (((ret & 0x01) != 0) && (++retries < 1000));
	/* Clear PMA attribute code request sent flag */
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_L, 0x80);
}

static inline void nc_mdio_fixup_etile_set_loopback(struct nc_mdio *mdio, int prtad, int enable)
{
	int i;
	uint16_t data = enable ? 0x0301 : 0x0300; // enable or disable loopback?

	for (i = 0; i < 4; i++) {
		nc_mdio_etile_pma_attribute_write(mdio, prtad, i, 0x0008, data);
	}
}

static inline void nc_mdio_close(struct nc_mdio *m)
{
	nfb_comp_close(nfb_user_to_comp(m));
}

static inline int nc_mdio_read(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	return mdio->mdio_read(comp, prtad, devad, addr);
}

static inline int nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	void (*nc_mdio_fixup_set_mode)(struct nc_mdio *mdio, int prtad, uint16_t val) = NULL;
	void (*nc_mdio_fixup_set_loopback)(struct nc_mdio *mdio, int prtad, int enable) = NULL;

	/* Select correct set of FIXUP functions */
	if (mdio->pcspma_is_e_tile) {
		nc_mdio_fixup_set_mode = NULL;
		nc_mdio_fixup_set_loopback = nc_mdio_fixup_etile_set_loopback;
	} else if (mdio->pcspma_is_f_tile) {
		nc_mdio_fixup_set_mode = nc_mdio_fixup_ftile_set_mode;
		nc_mdio_fixup_set_loopback = nc_mdio_fixup_ftile_set_loopback;
	}

	/* If relevant, apply corresponding FIXUP function */
	if ((mdio->pcspma_is_e_tile || mdio->pcspma_is_f_tile) &&
	    (mdio->mdio_write == nc_mdio_dmap_write)) {
		/* 1.0.0: PMA/PMD control 1: PMA local loopback */
		if (devad == 1 && addr == 0) {
			uint16_t req_state = val & 1;
			uint16_t curr_state = mdio->mdio_read(comp, prtad, devad, addr) & 1;
			if (req_state != curr_state) {
				nc_mdio_fixup_set_loopback(mdio, prtad, req_state);
			}
		/* 1.7.6:0: PMA/PMD control 2: PMA/PMD type selection */
		} else if (devad == 1 && addr == 7) {
			nc_mdio_fixup_set_mode(mdio, prtad, val & 0x7F);
		}
	}

	/* Apply standard function */
	return mdio->mdio_write(comp, prtad, devad, addr, val);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MDIO_H */
