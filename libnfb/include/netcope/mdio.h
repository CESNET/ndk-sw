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
struct nc_mdio {
	int (*mdio_read)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr);
	int (*mdio_write)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr, uint16_t val);
	int pcspma_is_f_tile;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_mdio *nc_mdio_open (const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam);
static inline void         nc_mdio_close(struct nc_mdio *mdio);
static inline int          nc_mdio_read (struct nc_mdio *mdio, int prtad, int devad, uint16_t addr);
static inline int          nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_mdio *nc_mdio_open(const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam)
{
	struct nc_mdio *mdio;
	struct nfb_comp *comp;
	const struct fdt_property *prop;
        int f_tile = 0;

	prop = fdt_get_property(nfb_get_fdt(dev), fdt_offset_ctrlparam, "ip-name", NULL);
	if (prop && (strcmp(prop->data, "F_TILE") == 0)) {
		f_tile = 1;
	}

	comp = nc_mdio_ctrl_open_ext(dev, fdt_offset, sizeof(struct nc_mdio));
	if (comp) {
		mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
		mdio->mdio_read = nc_mdio_ctrl_read;
		mdio->mdio_write = nc_mdio_ctrl_write;
		mdio->pcspma_is_f_tile = f_tile;
		return mdio;
	}

	comp = nc_mdio_dmap_open_ext(dev, fdt_offset, sizeof(struct nc_mdio));
	if (comp) {
		mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
		mdio->mdio_read = nc_mdio_dmap_read;
		mdio->mdio_write = nc_mdio_dmap_write;
		mdio->pcspma_is_f_tile = f_tile;
		return mdio;
	}

	return NULL;
}

static inline void nc_mdio_ftile_reset(struct nc_mdio *mdio, int prtad, int rx, int enable)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t data = enable ? 1 : 0; // enable or disable reset?

	if (rx) {
		data <<= 2; // RX reset on bit 2
	} else {
		data <<= 1; // TX reset on bit 1
	}

	// see https://cdrdv2.intel.com/v1/dl/getContent/637401 for eth_reset register (0x108) details
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x108, data);
}

static inline void nc_mdio_ftile_fgt_attribute_write(struct nc_mdio *mdio, int prtad, uint16_t data, uint16_t lane, uint8_t opcode)
{
#define FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ (1 << 15)
#define FGT_ATTRIBUTE_ACCESS_OPTION_RESET       (1 << 14)
#define FGT_ATTRIBUTE_ACCESS_OPTION_SET         (1 << 13)

#define fgt_attribute_access(opcode, lane, options, data) ((((uint32_t) (data)) << 16) | (options) | (((3 - lane) & 0x3) << 8) | ((opcode) & 0xFF))
	uint32_t LINK_MNG_SIDE_CPI_REGS;
	uint32_t PHY_SIDE_CPI_REGS;
	uint32_t reg;
	uint32_t page;
	uint32_t options;
	uint32_t service_req;
	uint32_t reset;

	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	int retries = 0;

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

	options = FGT_ATTRIBUTE_ACCESS_OPTION_SET | FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ;
	reg = fgt_attribute_access(opcode, lane, options, data);
	nc_mdio_dmap_drp_write(comp, prtad, page, LINK_MNG_SIDE_CPI_REGS, reg);
	do {
		reg = nc_mdio_dmap_drp_read(comp, prtad, page, PHY_SIDE_CPI_REGS);
		service_req = reg & FGT_ATTRIBUTE_ACCESS_OPTION_SERVICE_REQ;
		reset = reg & FGT_ATTRIBUTE_ACCESS_OPTION_RESET;
	} while ((service_req == 0 || reset != 0) && ++retries < 1000);

	options = FGT_ATTRIBUTE_ACCESS_OPTION_SET;
	reg = fgt_attribute_access(opcode, lane, options, data);
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

	/* 0x5B == 400GBASE-FR8, 0x5C == 400GBASE-LR8 */
	if (val == 0x5B || val == 0x5C) {
		media_type = 0x14; // optical
	/* all other media types */
	} else {
		media_type = 0x10; // default
	}

	for (i = 0; i < 8; i++) {
		nc_mdio_ftile_fgt_attribute_write(mdio, prtad, media_type, i, 0x64);
	}

	nc_mdio_ftile_reset(mdio, prtad, 1, 1); // assert RX reset
	nc_mdio_ftile_reset(mdio, prtad, 1, 0); // deassert RX reset
}

static inline void nc_mdio_fixup_ftile_set_loopback(struct nc_mdio *mdio, int prtad, int enable)
{
	int i;
	uint16_t data = enable ? 0x6 : 0x0; // enable or disable loopback?

	nc_mdio_ftile_reset(mdio, prtad, 1, 1); // assert RX reset

	for (i = 0; i < 8; i++) {
		nc_mdio_ftile_fgt_attribute_write(mdio, prtad, data, i, 0x40);
	}

	nc_mdio_ftile_reset(mdio, prtad, 1, 0); // deassert RX reset
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

	if ((mdio->pcspma_is_f_tile) && (mdio->mdio_write == nc_mdio_dmap_write)) {
		/* 1.0.0: PMA/PMD control 1: PMA local loopback */
		if (devad == 1 && addr == 0) {
			uint16_t req_state = val & 1;
			uint16_t curr_state = mdio->mdio_read(comp, prtad, devad, addr) & 1;
			if (req_state != curr_state) {
				nc_mdio_fixup_ftile_set_loopback(mdio, prtad, req_state);
			}
		/* 1.7.6:0: PMA/PMD control 2: PMA/PMD type selection */
		} else if (devad == 1 && addr == 7) {
			nc_mdio_fixup_ftile_set_mode(mdio, prtad, val & 0x7F);
		}
	}
	return mdio->mdio_write(comp, prtad, devad, addr, val);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MDIO_H */
