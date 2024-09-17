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

#define FTILE_RSFEC_BASE_25G  0x6000
#define FTILE_RSFEC_BASE_50G  0x6200
#define FTILE_RSFEC_BASE_100G 0x6600
#define FTILE_RSFEC_BASE_200G 0x6E00
#define FTILE_RSFEC_BASE_400G 0x7E00

#define FTILE_PCS_BASE_10_25G	0x1000
#define FTILE_PCS_BASE_50G	0x2000
#define FTILE_PCS_BASE_40_100G	0x3000
#define FTILE_PCS_BASE_200G	0x4000
#define FTILE_PCS_BASE_400G	0x5000

#define ETILE_RSFEC_PAGE 9

/* MDIO component lock features */
#define DRP_IFC  (1 << 0)
#define ATTR_IFC (1 << 2)
#define PCS_IFC  (1 << 3)


/* ~~~~[ INCLUDES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#include "mdio_dmap.h"
#include "mdio_ctrl.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

enum mdio_pma_enc {
	MDIO_PMA_ENC_NRZ = 0,
	MDIO_PMA_ENC_PAM4 = 1,
};

enum mdio_fec_mode {
	MDIO_FEC_NONE     = 0, // No FEC
	MDIO_FEC_FIRECODE = 1, // Firecode (CL 74)
	MDIO_FEC_CL91     = 2, // RS(528,514) (Clause 91)
	MDIO_FEC_CL134    = 3, // RS(544,514) (Clause 134)
	MDIO_FEC_ETC      = 4  // Ethernet Technology Consortium RS(272,258)
};

struct nc_mdio {
	int (*mdio_read)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr);
	int (*mdio_write)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr, uint16_t val);
	int pcspma_is_e_tile;
	int pcspma_is_f_tile;
	int rsfec_supported;
	int pma_lanes;                   /* Number of PMA lanes */
	enum mdio_pma_enc link_encoding; /* Line modulation */
	enum mdio_fec_mode fec_mode;     /* Firecode/Clause91/Clause134/ETC */
	int speed;                       /* Speed in Gbps */
};

/* Structure holds IEEE -> FTILE registers mapping */
struct nc_ftile_reg_map {
	int devad;
	int ieee_reg;
	uint32_t ftile_reg;
};

/* Register mapping table for FTILE RS-FEC registers (200 & 400GE) */
static const struct nc_ftile_reg_map ftile_fec_map[] = {
	{1, 202, 0x0000 + 0x184}, /* PMA FEC corrected codewords low */
	{1, 203, 0x0000 + 0x186}, /* PMA FEC corrected codewords high */
	{1, 204, 0x0000 + 0x18c}, /* PMA FEC uncorrected codewords low */
	{1, 205, 0x0000 + 0x18e}, /* PMA FEC uncorrected codewords high */
	{1, 210, 0x0000 + 0x194}, /* PMA FEC symbol errors, lane 0 low  */
	{1, 211, 0x0000 + 0x196}, /* PMA FEC symbol errors, lane 0 high */
	{1, 212, 0x0200 + 0x194},
	{1, 213, 0x0200 + 0x196},
	{1, 214, 0x0400 + 0x194},
	{1, 215, 0x0400 + 0x196},
	{1, 216, 0x0600 + 0x194}, /* PMA FEC symbol errors, lane 3 low  */
	{1, 217, 0x0600 + 0x196}, /* PMA FEC symbol errors, lane 3 high */
	{3, 802, 0x0000 + 0x184}, /* Corrected codewords low */
	{3, 803, 0x0000 + 0x186}, /* Corrected codewords high */
	{3, 804, 0x0000 + 0x18c}, /* Uncorrected codewords */
	{3, 805, 0x0000 + 0x18e}, /* Uncorrected codewords */
	{3, 600, 0x0000 + 0x194}, /* PCS FEC symbol errors, lane 0 low */
	{3, 601, 0x0000 + 0x196}, /* PCS FEC symbol errors, lane 0 high */
	{3, 602, 0x0200 + 0x194},
	{3, 603, 0x0200 + 0x196},
	{3, 604, 0x0400 + 0x194},
	{3, 605, 0x0400 + 0x196},
	{3, 606, 0x0600 + 0x194},
	{3, 607, 0x0600 + 0x196},
	{3, 608, 0x0800 + 0x194},
	{3, 609, 0x0800 + 0x196},
	{3, 610, 0x0a00 + 0x194},
	{3, 611, 0x0a00 + 0x196},
	{3, 612, 0x0c00 + 0x194},
	{3, 613, 0x0c00 + 0x196},
	{3, 614, 0x0e00 + 0x194},
	{3, 615, 0x0e00 + 0x196},
	{3, 616, 0x1000 + 0x194},
	{3, 617, 0x1000 + 0x196},
	{3, 618, 0x1200 + 0x194},
	{3, 619, 0x1200 + 0x196},
	{3, 620, 0x1400 + 0x194},
	{3, 621, 0x1400 + 0x196},
	{3, 622, 0x1600 + 0x194},
	{3, 623, 0x1600 + 0x196},
	{3, 624, 0x1800 + 0x194},
	{3, 625, 0x1800 + 0x196},
	{3, 626, 0x1a00 + 0x194},
	{3, 627, 0x1a00 + 0x196},
	{3, 628, 0x1c00 + 0x194},
	{3, 629, 0x1c00 + 0x196},
	{3, 630, 0x1e00 + 0x194}, /* PCS FEC symbol errors, lane 16 low */
	{3, 631, 0x1e00 + 0x196}, /* PCS FEC symbol errors, lane 16 high */
	{3, 400, 0x0000 + 0x16c}, /* PCS lane mapping, lane 0 */
	{3, 401, 0x0200 + 0x16c}, /* PCS lane mapping, lane 1 */
	{3, 402, 0x0400 + 0x16c}, /* PCS lane mapping, lane 2 */
	{3, 403, 0x0600 + 0x16c}, /* PCS lane mapping, lane 3 */
	{3, 404, 0x0800 + 0x16c}, /* PCS lane mapping, lane 4 */
	{3, 405, 0x0a00 + 0x16c}, /* PCS lane mapping, lane 5 */
	{3, 406, 0x0c00 + 0x16c}, /* PCS lane mapping, lane 6 */
	{3, 407, 0x0e00 + 0x16c}, /* PCS lane mapping, lane 7 */
	{3, 408, 0x1000 + 0x16c}, /* PCS lane mapping, lane 8 */
	{3, 409, 0x1200 + 0x16c}, /* PCS lane mapping, lane 9 */
	{3, 410, 0x1400 + 0x16c}, /* PCS lane mapping, lane 10 */
	{3, 411, 0x1600 + 0x16c}, /* PCS lane mapping, lane 11 */
	{3, 412, 0x1800 + 0x16c}, /* PCS lane mapping, lane 12 */
	{3, 413, 0x1a00 + 0x16c}, /* PCS lane mapping, lane 13 */
	{3, 414, 0x1c00 + 0x16c}, /* PCS lane mapping, lane 14 */
	{3, 415, 0x1e00 + 0x16c}, /* PCS lane mapping, lane 15 */
	{0, 0, 0}
};

/* Register mapping table for FTILE PCS */
static const struct nc_ftile_reg_map ftile_pcs_map[] = {
	{3, 200, 0xa4},	  /* BIP counter, lane 0 */
	{3, 201, 0xa8},	  /* BIP counter, lane 1 */
	{3, 202, 0xac},	  /* BIP counter, lane 2 */
	{3, 203, 0xb0},	  /* BIP counter, lane 3 */
	{3, 204, 0xb4},	  /* BIP counter, lane 4 */
	{3, 205, 0xb8},	  /* BIP counter, lane 5 */
	{3, 206, 0xbc},	  /* BIP counter, lane 6 */
	{3, 207, 0xc0},	  /* BIP counter, lane 7 */
	{3, 208, 0xc4},	  /* BIP counter, lane 8 */
	{3, 209, 0xc8},	  /* BIP counter, lane 9 */
	{3, 210, 0xcc},	  /* BIP counter, lane 10 */
	{3, 211, 0xd0},	  /* BIP counter, lane 11 */
	{3, 212, 0xd4},	  /* BIP counter, lane 12 */
	{3, 213, 0xd8},	  /* BIP counter, lane 13 */
	{3, 214, 0xdc},	  /* BIP counter, lane 14 */
	{3, 215, 0xe0},	  /* BIP counter, lane 15 */
	{3, 216, 0xe4},	  /* BIP counter, lane 16 */
	{3, 217, 0xe8},	  /* BIP counter, lane 17 */
	{3, 218, 0xec},	  /* BIP counter, lane 18 */
	{3, 219, 0xf0},	  /* BIP counter, lane 19 */
	{0, 0, 0}
};

/* Register mapping table for ETILE PCS */
static const struct nc_ftile_reg_map etile_pcs_map[] = {
	{3, 200, 0x361},  /* BIP counter, lane 0 */
	{3, 201, 0x362},  /* BIP counter, lane 1 */
	{3, 202, 0x363},  /* BIP counter, lane 2 */
	{3, 203, 0x364},  /* BIP counter, lane 3 */
	{3, 204, 0x365},  /* BIP counter, lane 4 */
	{3, 205, 0x366},  /* BIP counter, lane 5 */
	{3, 206, 0x367},  /* BIP counter, lane 6 */
	{3, 207, 0x368},  /* BIP counter, lane 7 */
	{3, 208, 0x369},  /* BIP counter, lane 8 */
	{3, 209, 0x36a},  /* BIP counter, lane 9 */
	{3, 210, 0x36b},  /* BIP counter, lane 10 */
	{3, 211, 0x36c},  /* BIP counter, lane 11 */
	{3, 212, 0x36d},  /* BIP counter, lane 12 */
	{3, 213, 0x36e},  /* BIP counter, lane 13 */
	{3, 214, 0x36f},  /* BIP counter, lane 14 */
	{3, 215, 0x370},  /* BIP counter, lane 15 */
	{3, 216, 0x371},  /* BIP counter, lane 16 */
	{3, 217, 0x372},  /* BIP counter, lane 17 */
	{3, 218, 0x373},  /* BIP counter, lane 18 */
	{3, 219, 0x374},  /* BIP counter, lane 19 */
	{0, 0, 0}
};

static inline uint32_t _find_ftile_reg(uint16_t devad, uint16_t ieee_reg, const struct nc_ftile_reg_map table[])
{
	int i = 0;

	while (table[i].devad) {
		if (
			(ieee_reg == table[i].ieee_reg) &&
			(devad == table[i].devad)
		)
			return (table[i].ftile_reg);
		i++;
	}
	return 0;
}

static inline uint32_t _ftile_rsfec_base(int speed)
{
	switch (speed) {
	case 25: return FTILE_RSFEC_BASE_25G;
	case 50: return FTILE_RSFEC_BASE_50G;
	case 100: return FTILE_RSFEC_BASE_100G;
	case 200: return FTILE_RSFEC_BASE_200G;
	case 400: return FTILE_RSFEC_BASE_400G;
	default: return 0;
	}
}

static inline uint32_t _ftile_pcs_base(int speed)
{
	switch (speed) {
	case 10: return FTILE_PCS_BASE_10_25G;
	case 25: return FTILE_PCS_BASE_10_25G;
	case 50: return FTILE_PCS_BASE_50G;
	case 40: return FTILE_PCS_BASE_40_100G;
	case 100: return FTILE_PCS_BASE_40_100G;
	case 200: return FTILE_PCS_BASE_200G;
	case 400: return FTILE_PCS_BASE_400G;
	default: return 0;
	}
}

#define ftile_rsfec_addr(speed, lane, reg) ( ( _ftile_rsfec_base(speed) + (lane)*0x200 + (reg)) >> 2)
#define ftile_pcs_addr(speed, reg) ((_ftile_pcs_base(speed) + (reg)) >> 2)

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_mdio *nc_mdio_open (const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam);
static inline struct nc_mdio *nc_mdio_open_no_init(const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam);
static inline void         nc_mdio_init(struct nc_mdio *mdio);
static inline void         nc_mdio_close(struct nc_mdio *mdio);
static inline int          nc_mdio_read (struct nc_mdio *mdio, int prtad, int devad, uint16_t addr);
static inline int          nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val);
static inline int          nc_mdio_pcs_lane_map_valid(struct nc_mdio *mdio);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Get PCS lane map validity */
static inline int nc_mdio_pcs_lane_map_valid(struct nc_mdio *mdio)
{
	/* The PCS lane map is not valid on Intel E-Tile and F-Tile when the FEC is active */
	if (mdio->pcspma_is_e_tile || mdio->pcspma_is_f_tile) {
		return (mdio->fec_mode == MDIO_FEC_NONE);
	} else {
		return 1;
	}
}

/* Get F-tile EHIP configuration and fill corresponding fields in the nc_mdio structure */
static inline void nc_mdio_ftile_config(struct nc_mdio *mdio)
{
	const int EHIP_CFG_REG = (0x100 >> 2);
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t reg;

	reg = nc_mdio_dmap_drp_read(comp, 0, 0, EHIP_CFG_REG);
	mdio->pma_lanes = ((reg >> 21) & 0xf);
	mdio->link_encoding = ((reg >> 9) & 0x1) ? MDIO_PMA_ENC_PAM4 : MDIO_PMA_ENC_NRZ;
	mdio->fec_mode = ((reg >> 10) & 0x7);
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
	mdio->rsfec_supported = (((mdio->speed == 10) || (mdio->speed == 40)) ?  0 : 1);
}

/* Get E-tile Ethernet configuration and fill corresponding fields in the nc_mdio structure */
static inline void nc_mdio_etile_config(struct nc_mdio *mdio)
{
	/* Get FEC mode: The E-Tile supports RS-FEC according clause 91 only. The FEC is active */
	/* when bit 0 of the rsfec_top_rx_cfg register (0x14) is set */
	#define get_fec_mode	((nc_mdio_dmap_drp_read(comp, 0, ETILE_RSFEC_PAGE, 0x14) & 0x1) ? MDIO_FEC_CL91 : MDIO_FEC_NONE)

	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t val;

	/* The E-Tile EHIP doesn't contains configuration registers, thus  */
	/* the number of lanes&speed are determined by the MDIO register 1.7 */
	val = mdio->mdio_read(comp, 0, 1, 7);
	/* TBD: number of lanes can be read from PMA address 0x40010 */
	if (val <= 0x1f) { /* 10GBASE */
		mdio->pma_lanes = 1;
		mdio->speed = 10;
		mdio->rsfec_supported = 0;
		mdio->fec_mode = MDIO_FEC_NONE;
	} else if (val <= 0x26) { /* 40GBASE */
		mdio->pma_lanes = 4;
		mdio->speed = 40;
		mdio->rsfec_supported = 0;
		mdio->fec_mode = MDIO_FEC_NONE;
	} else if (val <= 0x2f) { /* 100GBASE */
		mdio->pma_lanes = 4;
		mdio->speed = 100;
		mdio->rsfec_supported = 1;
		mdio->fec_mode = get_fec_mode;
	} else if (val <= 0x3a) { /* 25GBASE */
		mdio->pma_lanes = 1;
		mdio->speed = 25;
		mdio->rsfec_supported = 1;
		mdio->fec_mode = get_fec_mode;
	} else { /* other modes are not supported on E-Tile now */
		mdio->pma_lanes = 0;
		mdio->speed = 0;
		mdio->rsfec_supported = 0;
		mdio->fec_mode = MDIO_FEC_NONE;
	}
}

static inline struct nc_mdio *nc_mdio_open_no_init(const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam)
{
	struct nc_mdio mdio_tmp = {0};
	struct nc_mdio *mdio;
	struct nfb_comp *comp;
	const char *prop;
	int proplen = 0;

	if (0) {
	} else if ((comp = nc_mdio_ctrl_open_ext(dev, fdt_offset, sizeof(struct nc_mdio)))) {
		mdio_tmp.mdio_read = nc_mdio_ctrl_read;
		mdio_tmp.mdio_write = nc_mdio_ctrl_write;
	} else if ((comp = nc_mdio_dmap_open_ext(dev, fdt_offset, sizeof(struct nc_mdio)))) {
		mdio_tmp.mdio_read = nc_mdio_dmap_read;
		mdio_tmp.mdio_write = nc_mdio_dmap_write;
	} else {
		return NULL;
	}

	mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
	*mdio = mdio_tmp;

	prop = (const char*) fdt_getprop(nfb_get_fdt(dev), fdt_offset_ctrlparam, "ip-name", &proplen);
	if (proplen > 0) {
		if (strcmp(prop, "E_TILE") == 0) {
			mdio->pcspma_is_e_tile = 1;
		} else if (strcmp(prop, "F_TILE") == 0) {
			mdio->pcspma_is_f_tile = 1;
		}
	}

	return mdio;
}

static inline void nc_mdio_init(struct nc_mdio *mdio)
{
	if (mdio->pcspma_is_f_tile)
		nc_mdio_ftile_config(mdio);
	if (mdio->pcspma_is_e_tile)
		nc_mdio_etile_config(mdio);
}

static inline struct nc_mdio *nc_mdio_open(const struct nfb_device *dev, int fdt_offset, int fdt_offset_ctrlparam)
{
	struct nc_mdio *mdio;
	mdio = nc_mdio_open_no_init(dev, fdt_offset, fdt_offset_ctrlparam);
	if (mdio == NULL)
		return NULL;

	nc_mdio_init(mdio);
	return mdio;
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
		(val == 0x5B || val == 0x5C) ||  /* 400GBASE-R8  */
		(val == 0x5F)                ||  /* 400GBASE-SR8 */
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

static inline void _ftile_rsfec_snapshot(struct nfb_comp *comp, int prtad, int fec_lane, int speed)
{
	/* make stats snapshot */
	nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_rsfec_addr(speed, fec_lane, 0x1e0), 1);
	nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_rsfec_addr(speed, fec_lane, 0x1e0), 0);
}

/* Construct ieee register 1.201 (RSFEC status) */
static inline uint16_t _get_ftile_r1201(struct nfb_comp *comp, int prtad, int fec_lanes, int speed)
{
	uint32_t val;
	uint16_t tmp;
	int lanes = (fec_lanes <= 4) ? fec_lanes : 4;
	int i;

	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_rsfec_addr(speed, 0, 0x158));
	tmp =  ((val & 0x0010) >> 2);  /* High SER */
	tmp |= (((~val & 0x0002) >> 1) << 8);  /* Lane0 AM locked */
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_rsfec_addr(speed, 0, 0x164));
	tmp |= ((~val & 0x0001) << 14); /* RSFEC lanes aligned */
	for (i = 1; i<lanes; i++) {
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_rsfec_addr(speed, i, 0x158));
		tmp |= (((~val & 0x2) >> 1) << (8+i)); /* Lane AM lock for lanes 1..3 */
	}
	/* Read TX align status from EHIP reg 0x118 */
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x118 >> 2);
	tmp |= (((val & 0x0004) >> 2) << 15);
	/* Clear flags */
	nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_rsfec_addr(speed, 0, 0x164), 0xffffffff);
	for (i = 0; i<lanes; i++)
		nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_rsfec_addr(speed, i, 0x158), 0xffffffff);
	return tmp;
}

/* (Unaligned) read F-TILE FSFEC register */
static inline uint16_t _get_ftile_rsfec_reg(struct nfb_comp *comp, int prtad, uint32_t addr, int speed)
{
	uint32_t val;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_rsfec_addr(speed, 0, (addr & 0xfffffffc)));
	if (addr & 0x2) /* Unaligned 16bit read */
		return (val >> 16);    /* Higher 16 bits */
	else
		return (val & 0xffff); /* lower 16 bits */
}

/* (Unaligned) read F-TILE PCS register */
static inline uint16_t _get_ftile_pcs_reg(struct nfb_comp *comp, int prtad, uint32_t reg, int speed)
{
	uint32_t val;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, reg));
	return (val & 0xffff);
}

/* Construct ieee register 1.206 (RSFEC lane map) */
static inline uint16_t _get_ftile_r1206(struct nfb_comp *comp, int prtad, int fec_lanes, int speed)
{
	uint32_t val;
	uint16_t tmp = 0;
	int lanes = (fec_lanes <= 4) ? fec_lanes : 4;
	int i;

	for (i = 0; i<lanes; i++) {
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_rsfec_addr(speed, i, 0x16c));
		tmp |= ((val & 0x3) << (2*i));
	}
	return tmp;
}

/* Construct ieee register 3.801 (RSFEC status) */
static inline uint16_t _get_ftile_r3801(struct nfb_comp *comp, int prtad, int speed)
{
	uint32_t val;
	uint16_t tmp = 0;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_rsfec_addr(speed, 0, 0x158));
	tmp = (1 << 3);                     /* Degraded SER ability */
	tmp |= (((val >>  4) & 0x1) << 2);  /* High SER */
	tmp |= (((val >> 24) & 0x1) << 4);  /* Degraded SER */
	tmp |= (((val >> 27) & 0x1) << 5);  /* Remote degraded SER recieved */
	tmp |= (((val >> 26) & 0x1) << 6);  /* Local degraded SER received */
	/* Clear the flags */
	nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_rsfec_addr(speed, 0, 0x158), 0xffffffff);
	return tmp;
}

static inline uint16_t nc_mdio_fixup_ftile_rsfec_read(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	const int speed = mdio->speed;
	const int fec_lanes = speed / 25;
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t ftile_reg;

	/* Try to find the <addr> register in table first */
	if ( (ftile_reg = _find_ftile_reg(devad, addr, ftile_fec_map)) ) {
		_ftile_rsfec_snapshot(comp, prtad, 0, speed);
		return _get_ftile_rsfec_reg(comp, prtad, ftile_reg, speed);
	}
	/* Register not found in the table */
	if (devad == 1) { /* PMA & PMA RSFEC registers */
		switch (addr) {
		case 201: return _get_ftile_r1201(comp, prtad, fec_lanes, speed);
		case 206: return _get_ftile_r1206(comp, prtad, fec_lanes, speed);
		default: break;
		}
	}
	if (devad == 3) { /* PCS &  PCS-FEC registers */
		switch (addr) {
		case 801: return _get_ftile_r3801(comp, prtad, speed);
		default: break;
		}
	}
	return mdio->mdio_read(comp, prtad, devad, addr); /* Unknown address: try to read the MGMT */
}

static inline void _ftile_pcs_snapshot(struct nfb_comp *comp, int prtad, int speed)
{
	/* make PCS stats snapshot */
	nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_pcs_addr(speed, 0x0), 3);
	nc_mdio_dmap_drp_write(comp, prtad, 0, ftile_pcs_addr(speed, 0x0), 0);
}

/* Construct ieee register 3.44 (BER counter high) */
static inline uint16_t _get_ftile_r3044(struct nfb_comp *comp, int prtad, int speed)
{
	uint32_t val;
	uint16_t tmp = 0;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, 0x90)); // BER count reg */
	tmp = ((val >> 6) & 0xffff);
	return tmp;
}

/* Construct ieee register 3.45 (Err block counter high) */
static inline uint16_t _get_ftile_r3045(struct nfb_comp *comp, int prtad, int speed)
{
	uint32_t val;
	uint16_t tmp = 0;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, 0xf4)); // BlkErr count reg */
	tmp = ((val >> 8) & 0xffff) | 0x8000;
	return tmp;
}

/* Construct ieee register 3.33 (BASE-R status 2) */
static inline uint16_t _get_ftile_r3033(struct nc_mdio *mdio, struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0;
	/* Bits [15:14] read from mgmt using standard MDIO */
	val = mdio->mdio_read(comp, prtad, 3, 33);
	tmp = (val & 0xc000);
	/* Others bits (counters) read using DRP */
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(mdio->speed, 0xf4)); /* BlkErr count reg */
	tmp |= (val & 0xff);
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(mdio->speed, 0x90)); /* BER count reg */
	tmp |= ((val & 0x3f) << 8);
	return tmp;
}

/* Construct ieee register 3.400-419 (PCS lane mapping) */
static inline uint16_t _get_ftile_r3400(struct nfb_comp *comp, int prtad, int speed, int lane)
{
	uint32_t val;
	uint16_t tmp = 0;
	if (lane <= 5)
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, 0x94));
	else if (lane <= 11)
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, 0x98));
	else if (lane <= 17)
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, 0x9c));
	else
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, ftile_pcs_addr(speed, 0xa0));

	tmp = ((val >> ((lane % 6) * 5)) & 0x1f);
	return tmp;
}

static inline uint16_t nc_mdio_fixup_ftile_pcs_read(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	const int speed = mdio->speed;
	const int fec_lanes = speed / 25;
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t ftile_reg;

	/* Try to find the <addr> register in PCS table first */
	if ( (ftile_reg = _find_ftile_reg(devad, addr, ftile_pcs_map)) ) {
		 _ftile_pcs_snapshot(comp, prtad, speed);
                 return  _get_ftile_pcs_reg(comp, prtad, ftile_reg, speed);
	}
	/* Also try to find the <addr> register in RSFEC table */
	if ( (addr < 400) && (ftile_reg = _find_ftile_reg(devad, addr, ftile_fec_map)) )  {
		_ftile_rsfec_snapshot(comp, prtad, 0, speed);
		return _get_ftile_rsfec_reg(comp, prtad, ftile_reg, speed);
	}
	/* Register not found in tables */
	if (devad == 1) { /* PMA & RSFEC registers */
		switch (addr) {
		case 201: return _get_ftile_r1201(comp, prtad, fec_lanes, speed);
		case 206: return _get_ftile_r1206(comp, prtad, fec_lanes, speed);
		default: break;
		}
	}
	if (devad == 3) { /* PCS registers */
		if (addr == 33)
			return _get_ftile_r3033(mdio, comp, prtad);
		if (addr == 44)
			return _get_ftile_r3044(comp, prtad, speed);
		if (addr == 45)
			return _get_ftile_r3045(comp, prtad, speed);
		if ((addr >= 400) && (addr <= 419)) {
			int lane = (addr - 400);
			return _get_ftile_r3400(comp, prtad, speed, lane);
		}
	}
	return mdio->mdio_read(comp, prtad, devad, addr); /* Unknown address: try to read the MGMT */
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

/* ~~~~[ Intel E-Tile  ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Adaptation modes */
#define ETILE_ADAPT_MODE_INITIAL	0x1
#define ETILE_ADAPT_MODE_ONESHOT	0x2
#define ETILE_ADAPT_MODE_CONTINUOUS	0x6

/* 32-bit read operation from the RS-FEC registers */
static inline uint32_t nc_mdio_etile_rsfec_read(struct nfb_comp *comp, int prtad, uint32_t addr)
{
	int i;
	uint32_t val = 0;

	for (i = 0; i < 4; i++)
		val |= ((nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, addr+i) & 0xff) << i*8);
	return val;
}

static inline uint32_t nc_mdio_etile_rsfec_write(struct nfb_comp *comp, int prtad, uint32_t addr, uint32_t val)
{
	int i;

	for (i = 0; i < 4; i++)
		nc_mdio_dmap_drp_write(comp, prtad, ETILE_RSFEC_PAGE, addr+i, ((val >> i*8) & 0xff));
	return val;
}

/* Make PCS stats snapshot */
static inline void _etile_pcs_snapshot(struct nfb_comp *comp, int prtad)
{
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x945, 0x04); /* RX stats snapshot */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x845, 0x04); /* TX stats snapshot */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x945, 0x00);
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x845, 0x00);
}

/* Make RSFEC stats snapshot */
static inline void _etile_rsfec_snapshot(struct nfb_comp *comp, int prtad)
{
	nc_mdio_dmap_drp_write(comp, prtad, ETILE_RSFEC_PAGE, 0x108, 0x0f);
	nc_mdio_dmap_drp_write(comp, prtad, ETILE_RSFEC_PAGE, 0x108, 0x00);
}

/* Clear RSFEC counters */
static inline void _etile_rsfec_clear_stats(struct nfb_comp *comp, int prtad)
{
	nc_mdio_dmap_drp_write(comp, prtad, ETILE_RSFEC_PAGE, 0x108, 0xf0);
	nc_mdio_dmap_drp_write(comp, prtad, ETILE_RSFEC_PAGE, 0x108, 0x00);
}

/* Construct ieee register 3.33 (BASE-R status 2) */
static inline uint16_t _get_etile_r3033(struct nc_mdio *mdio, struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0;
	/* Bits [15:14] read from mgmt using standard MDIO */
	val = mdio->mdio_read(comp, prtad, 3, 33);
	tmp = (val & 0xc000);
	/* Others bits (counters) read using DRP */
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x37c); /* BlkErr count reg */
	tmp |= (val & 0xff);
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x32a); /* BER count reg */
	tmp |= ((val & 0x3f) << 8);
	return tmp;
}

/* Construct ieee register 3.44 (BER counter high) */
static inline uint16_t _get_etile_r3044(struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x32a); // BER count reg */
	tmp = ((val >> 6) & 0xffff);
	return tmp;
}

/* Construct ieee register 3.45 (Err block counter high) */
static inline uint16_t _get_etile_r3045(struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0;
	val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x37c); // BlkErr count reg */
	tmp = ((val >> 8) & 0xffff) | 0x8000;
	return tmp;
}

/* Construct ieee register 3.400-419 (PCS lane mapping) */
static inline uint16_t _get_etile_r3400(struct nfb_comp *comp, int prtad, int lane)
{
	uint32_t val;
	uint16_t tmp = 0;

	if (lane <= 5)
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x330);
	else if (lane <= 11)
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x331);
	else if (lane <= 17)
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x332);
	else
		val = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x333);
	tmp = ((val >> ((lane % 6) * 5)) & 0x1f);
	return tmp;
}

/* Construct ieee register 1.200 (RSFEC control register) */
static inline uint16_t _get_etile_r1200(struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0;

	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x40); /* rsfec lane cfg */
	tmp |= ((val & 0x0004) >> 3) << 1; /* Bypass error indication flag */
	tmp |= ((val & 0x0008) >> 4) << 2; /* Clause 108 RSFEC enabled */
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x30); /* rsfec core cfg */
	tmp |= ((~val & 0x0001) >> 0) << 3; /* Four lane PMD */

	return tmp;
}

/* Construct ieee register 1.201 (RSFEC status register) */
static inline uint16_t _get_etile_r1201(struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0x2; /* FEC bypass ability enabled by default*/

	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x150); /* rsfec_lane_rx_stat */
	tmp |= ((val & 0x0010) >> 4) << 2; /* High SER */
	tmp |= ((~val & 0x0002) >> 1) << 8; /* Lane 0 locked */
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x154); /* rsfec_lane_rx_stat */
	tmp |= ((~val & 0x0002) >> 1) << 9; /* Lane 1 locked */
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x158); /* rsfec_lane_rx_stat */
	tmp |= ((~val & 0x0002) >> 1) << 10; /* Lane 2 locked */
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x15c); /* rsfec_lane_rx_stat */
	tmp |= ((~val & 0x0002) >> 1) << 11; /* Lane 3 locked */
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x180); /* rsfec_lane_rx_stat */
	tmp |= ((~val & 0x0001) >> 0) << 14; /* Align status */
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x104); /* rsfec_lane_rx_stat */
	tmp |= ((~val & 0x0001) >> 0) << 15; /* PCS align status */
	return tmp;
}

/* Construct ieee register 1.206 (RSFEC lane mapping) */
static inline uint16_t _get_etile_r1206(struct nfb_comp *comp, int prtad)
{
	uint32_t val;
	uint16_t tmp = 0;

	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x1a0+4*0);
	tmp |= ((val & 0x3) << 0);
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x1a0+4*1);
	tmp |= ((val & 0x3) << 2);
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x1a0+4*2);
	tmp |= ((val & 0x3) << 4);
	val = nc_mdio_dmap_drp_read(comp, prtad, ETILE_RSFEC_PAGE, 0x1a0+4*3);
	tmp |= ((val & 0x3) << 6);
	return tmp;
}

/* Sum 2 32-bit registers to compute total number of errors */
static inline uint32_t _get_etile_stats(struct nfb_comp *comp, int prtad, uint16_t reg, int lane)
{
	uint64_t tmp;
	_etile_rsfec_snapshot(comp, prtad);
	tmp = nc_mdio_etile_rsfec_read(comp, prtad, reg+lane*8);
	tmp += (((uint64_t)(nc_mdio_etile_rsfec_read(comp, prtad, reg+lane*8+4))) << 32);
	if (tmp >= 0x100000000)
		return 0xffffffff;
	else
		return (tmp & 0xffffffff);
}

static inline uint16_t nc_mdio_fixup_etile_pcs_read(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	const int fec_lane = 0; /* TBD: lane must be set for speeds < 100 */
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint32_t etile_reg;

	/* Try to find the <addr> register in PCS table first */
	if ( (etile_reg = _find_ftile_reg(devad, addr, etile_pcs_map)) ) {
		 _etile_pcs_snapshot(comp, prtad);
		return nc_mdio_dmap_drp_read(comp, prtad, 0, etile_reg);
	}
	/* Register not found in tables */
	if ((devad == 1) && (mdio->rsfec_supported)) { /* PMA & RSFEC registers */
		switch (addr) {
		case 200: return _get_etile_r1200(comp, prtad);
		case 201: return _get_etile_r1201(comp, prtad);
		case 202: return (_get_etile_stats(comp, prtad, 0x200, fec_lane) & 0xffff); /* CCW low   */
		case 203: return (_get_etile_stats(comp, prtad, 0x200, fec_lane) >> 16);    /* CCW high  */
		case 204: return (_get_etile_stats(comp, prtad, 0x220, fec_lane) & 0xffff); /* UCCW low  */
		case 205: return (_get_etile_stats(comp, prtad, 0x220, fec_lane) >> 16);    /* UCCW high */
		case 206: return _get_etile_r1206(comp, prtad); /* lane mapping */
		case 210: return (_get_etile_stats(comp, prtad, 0x240, 0) & 0xffff); /* Lane 0 symbol errors low   */
		case 211: return (_get_etile_stats(comp, prtad, 0x240, 0) >> 16);    /* Lane 0 symbol errors high  */
		case 212: return (_get_etile_stats(comp, prtad, 0x240, 1) & 0xffff); /* Lane 1 symbol errors low   */
		case 213: return (_get_etile_stats(comp, prtad, 0x240, 1) >> 16);    /* Lane 1 symbol errors high  */
		case 214: return (_get_etile_stats(comp, prtad, 0x240, 2) & 0xffff); /* Lane 2 symbol errors low   */
		case 215: return (_get_etile_stats(comp, prtad, 0x240, 2) >> 16);    /* Lane 2 symbol errors high  */
		case 216: return (_get_etile_stats(comp, prtad, 0x240, 3) & 0xffff); /* Lane 3 symbol errors low   */
		case 217: return (_get_etile_stats(comp, prtad, 0x240, 3) >> 16);    /* Lane 3 symbol errors high  */
		default: break;
		}
	}
	if (devad == 3) { /* PCS registers */
		if (addr == 33)
			return _get_etile_r3033(mdio, comp, prtad);
		if (addr == 44)
			return _get_etile_r3044(comp, prtad);
		if (addr == 45)
			return _get_etile_r3045(comp, prtad);
		if ((addr >= 400) && (addr <= 419)) {
			int lane = (addr - 400);
			return _get_etile_r3400(comp, prtad, lane);
		}
	}
	return mdio->mdio_read(comp, prtad, devad, addr); /* Unknown address: try to read the MGMT */
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
	int locked = 0;
	int sent = 0;
	/* Lock the attribute access interface */
	while (!locked) {
		locked = nfb_comp_lock(comp, ATTR_IFC);
	}
	/* Clear PMA attribute code request sent flag */
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_L, 0x80);
	while (!sent) {
		/* Set PMA attribute code address and data */
		nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_VAL_L, code_val_l);
		nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_VAL_H, code_val_h);
		nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_ADDR_L, code_addr_l);
		nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_ADDR_H, code_addr_h);
		/* Issue PMA attribute code request */
		nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_REQ, 1);
		/* Verify that PMA attribute code request has been sent */
		sent = ((nc_mdio_dmap_drp_read(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_L) >> 7) & 0x01);
	}
	/* Wait until PMA attribute code request is completed */
	retries = 0;
	do {
		ret = nc_mdio_dmap_drp_read(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_H);
	} while (((ret & 0x01) != 0) && (++retries < 10000));
	/* Clear PMA attribute code request sent flag */
	nc_mdio_dmap_drp_write(comp, prtad, page, PMA_ATTR_CODE_REQ_STATUS_L, 0x80);

	nfb_comp_unlock(comp, ATTR_IFC);
}


static inline uint16_t nc_mdio_etile_pma_attribute_read(struct nc_mdio *mdio, int prtad, uint16_t lane)
{
	uint32_t page = lane + 1; // page number corresponds to lane number + 1
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint16_t rcode;

	rcode = (nc_mdio_dmap_drp_read(comp, prtad, page, 0x88) & 0xff);
	rcode |= ((nc_mdio_dmap_drp_read(comp, prtad, page, 0x89) & 0xff) << 8);
	return rcode;
}

/* Perform RX+TX reset of E-tile Ethernet PHY */
static inline void nc_mdio_etile_reset(struct nfb_comp *comp, int prtad)
{
	/* see https://www.intel.com/content/www/us/en/docs/programmable/683468/23-2/phy-configuration.html */
	/* see https://www.intel.com/content/www/us/en/docs/programmable/683468/23-2/reset.html */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x6); // soft rx rst + soft tx rst
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x0);
}

static inline void nc_mdio_etile_rsfec_off(struct nfb_comp *comp, int prtad, int lanes)
{
	int i;
	uint32_t reg;

	/* Intel e-tile RSFEC. See following docs for more details:
	 *   https://www.intel.com/content/www/us/en/docs/programmable/683860/22-3/steps-to-disable-fec.html
	 *   https://www.intel.com/content/www/us/en/docs/programmable/683860/22-3/configuring-for-100g-nrz-with-rsfec-09491.html
	 */
	nc_mdio_etile_rsfec_write(comp, prtad, 0x14, 0x00);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x04, 0x00);
	for (i = 0; i < lanes; i++) {
		/* Configure transceiver channels 0-3 */
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x04, 0xcb);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x05, 0x4c);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x06, 0x0f);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x07, 0xa6);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xa4, 0xa5);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xa8, 0xa5);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xb0, 0x55);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xe8, 0x07);
	}
	/* Ethernet configuration */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x37a, 0x312c7);
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x40b, 0x9ffd8028);
	reg = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x30e);
	reg |= 0x208;
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x30e, reg);
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x313, 0x20);
	/* RX+TX reset */
	nc_mdio_etile_reset(comp, prtad);
}

static inline void nc_mdio_etile_rsfec_on(struct nfb_comp *comp, int prtad, int lanes)
{
	int i;
	uint32_t reg;

	/* Intel e-tile RSFEC. See following docs for more details:
	 *   https://www.intel.com/content/www/us/en/docs/programmable/683860/22-3/steps-to-disable-fec.html
	 *   https://www.intel.com/content/www/us/en/docs/programmable/683860/22-3/configuring-for-100g-nrz-with-rsfec-09491.html
	 */
	for (i=0; i<lanes; i++) {
		/* Configure transceiver channels 0-3*/
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x04, 0xc7);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x05, 0x2c);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x06, 0x0f);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0x07, 0x86);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xa4, 0xa5);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xa8, 0xa5);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xb0, 0x55);
		nc_mdio_dmap_drp_write(comp, prtad, i+1, 0xe8, 0x07);
	}
	/* Ethernet configuration */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x37a, 0x312c7);
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x40b, 0x9ffd8028);
	reg = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x30e);
	reg &= 0xfffffdf7;
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x30e, reg);
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x313, 0x00);
	/* RS-FEC configuration */
	nc_mdio_etile_rsfec_write(comp, prtad, 0x04, 0x0f00);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x10, 0x0000);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x14, 0x1111);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x30, 0x0080);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x40, 0x0000);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x44, 0x0000);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x48, 0x0000);
	nc_mdio_etile_rsfec_write(comp, prtad, 0x4C, 0x0000);
	_etile_rsfec_clear_stats(comp, prtad);
	/* RX+TX reset */
	nc_mdio_etile_reset(comp, prtad);
}

static inline void nc_mdio_fixup_etile_set_mode(struct nc_mdio *mdio, int prtad, uint16_t val)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	if ((val == 0x2a) || (val == 0x2b)) {
		/* 100GBASE-LR4/ER4 - turn the FEC off */
		nc_mdio_etile_rsfec_off(comp, prtad, 4);
		mdio->fec_mode = MDIO_FEC_NONE;
	} else if ((val == 0x2f) || (val == 0x2e)) {
		/* 100GBASE-SR4/CR4 - turn the FEC on */
		nc_mdio_etile_rsfec_on(comp, prtad, 4);
		mdio->fec_mode = MDIO_FEC_CL91;
	}
	/* Update the mode in HW Eth mgmt too */
	mdio->mdio_write(comp, prtad, 1, 7, val);
}

/* Starts PMA adaptation on a lane. */
/* Adaptation mode: 0x1 - initial; 0x2 = One shot; 0x6 = continuous (mission) */
static inline void nc_mdio_etile_adapt_start(struct nc_mdio *mdio, int prtad, int lane, uint16_t mode)
{
	/* Set Adaptation Effort Level to full effort */
	nc_mdio_etile_pma_attribute_write(mdio, prtad, lane, 0x002c, 0x0118); /*  */
	nc_mdio_etile_pma_attribute_write(mdio, prtad, lane, 0x006c, 0x0001); /*  */
	nc_mdio_etile_pma_attribute_write(mdio, prtad, lane, 0x000a, mode); /* Run the adaptation */
}

static inline void nc_mdio_etile_adapt_wait(struct nc_mdio *mdio, int prtad, int lane, uint8_t result)
{
	uint16_t ret;
	int retries = 0;

	/* Wait until the adaptation finishes */
	do {
		nc_mdio_etile_pma_attribute_write(mdio, prtad, lane, 0x0126, 0x0b00);
		ret = nc_mdio_etile_pma_attribute_read(mdio, prtad, lane);
	} while (((ret & 0xff) != result) && (++retries < 100000));
}

/* Perform PMA analog reset on selected lane and load initial configuration */
static inline void nc_mdio_etile_areset(struct nfb_comp *comp, int prtad, int lane)
{
	const uint32_t DRP_PAGE = lane + 1;
	uint32_t ret;
	int retries = 0;

	/* See https://www.intel.com/content/www/us/en/docs/programmable/683723/current/pma-analog-reset-92001.html */
	/* See https://www.intel.com/content/www/us/en/docs/programmable/683723/current/reconfiguring-pma-settings.html */
	nc_mdio_dmap_drp_write(comp, prtad, DRP_PAGE, 0x200, 0x00); 	/* Write 0x200[7:0] = 0x00. */
	nc_mdio_dmap_drp_write(comp, prtad, DRP_PAGE, 0x201, 0x00); 	/* Write 0x201[7:0] = 0x00. */
	nc_mdio_dmap_drp_write(comp, prtad, DRP_PAGE, 0x202, 0x00); 	/* Write 0x202[7:0] = 0x00. */
	nc_mdio_dmap_drp_write(comp, prtad, DRP_PAGE, 0x203, 0x81); 	/* Write 0x203[7:0] = 0x81. */
	/* Read 0x207 until it becomes 0x80. This indicates that the operation completed successfully. */
	do {
		ret = nc_mdio_dmap_drp_read(comp, prtad, DRP_PAGE, 0x207); /* Wait until the PMA is loaded */
	} while (((ret & 0xff) != 0x80) && (++retries < 100000));
	/* Reload PMA settings (call PMA attribute sequencer) on all lanes */
	retries = 0;
	nc_mdio_dmap_drp_write(comp, prtad, DRP_PAGE, 0x91, 0x01); /* Load initial PMA configuration */
	do {
		ret = nc_mdio_dmap_drp_read(comp, prtad, DRP_PAGE, 0x91); /* Wait until the PMA is loaded */
	} while (((ret & 0x01) != 0x0) && (++retries < 100000));
}

static inline void nc_mdio_fixup_etile_set_loopback(struct nc_mdio *mdio, int prtad, int enable)
{
	/* See https://www.intel.com/content/www/us/en/docs/programmable/683468/23-2/ethernet-adaptation-flow-with-non-external.html */
	int i;
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	int locked = 0;
	int retries = 0;
	uint32_t ret;

	while (!locked) {
		locked = nfb_comp_lock(comp, PCS_IFC); /* Lock access to PCS registers (reset active) */
	}
	/* 1. Assert RX/TX reset ports of the EHIP */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x6); // soft rx rst + soft tx rst
	/* 2 + 3. Trigger PMA analog reset and reload PMA settings */
	for (i = 0; i < mdio->pma_lanes; i++) {
		nc_mdio_etile_areset(comp, prtad, i);
	}
	/* 4. Apply CSR reset */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x7); // eio_sys_rst set
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x6); // eio_sys_rst clear
	/* 5a. Deassert TX reset */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x4); // soft rx rst only
	/* 5b. Wait for TX ready */
	do {
		ret = nc_mdio_dmap_drp_read(comp, prtad, 0, 0x322);
	} while (((ret & 0x01) != 0x1) && (++retries < 100000));
	/* 6. skipped (PMA configuration not used) */
	/* 7a. Enable loopback and start the initial adaptation on all channels */
	for (i = 0; i < mdio->pma_lanes; i++) {
		nc_mdio_etile_pma_attribute_write(mdio, prtad, i, 0x0008, 0x0301); /* Enable the loopback */
		nc_mdio_etile_adapt_start(mdio, prtad, i, ETILE_ADAPT_MODE_INITIAL);
	}
	/* 7b. Check adaptation status on all channels */
	for (i = 0; i < mdio->pma_lanes; i++) {
		nc_mdio_etile_adapt_wait(mdio, prtad, i, 0x80);
	}
	/* 8-11. In mission mode, run the initial and continuous equalization */
	if (!enable) {
		for (i = 0; i < mdio->pma_lanes; i++) {
			nc_mdio_etile_pma_attribute_write(mdio, prtad, i, 0x0008, 0x0300); /* Disable the loopback */
			nc_mdio_etile_adapt_start(mdio, prtad, i, ETILE_ADAPT_MODE_INITIAL); /* Start the initial adaptation */
		}
		for (i = 0; i < mdio->pma_lanes; i++) {
			nc_mdio_etile_adapt_wait(mdio, prtad, i, 0x80);  /* Check initial adaptation status */
			nc_mdio_etile_adapt_start(mdio, prtad, i, ETILE_ADAPT_MODE_CONTINUOUS);  /* Start the continuous adaptation */
		}
		for (i = 0; i < mdio->pma_lanes; i++) {
			nc_mdio_etile_adapt_wait(mdio, prtad, i, 0xE2);  /* Check continuous adaptation status */
		}
	}
	/* 12. Deassert RX reset ports of the EHIP (using mgmt) */
	nc_mdio_dmap_drp_write(comp, prtad, 0, 0x310, 0x0);
	/* Get current PMA mode from mgmt */
	ret = mdio->mdio_read(comp, prtad, 1, 7);
	/* Apply the mode (turn the RS-FEC on or off) */
	nc_mdio_fixup_etile_set_mode(mdio, prtad, ret);
	nfb_comp_unlock(comp, PCS_IFC);
}

static inline void nc_mdio_close(struct nc_mdio *m)
{
	nfb_comp_close(nfb_user_to_comp(m));
}

static inline int nc_mdio_read(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	uint16_t (*nc_mdio_fixup_rsfec_read)(struct nc_mdio *mdio, int devad, int prtad, uint16_t reg) = NULL;
	uint16_t val;
	int locked = 0;

	/* Select correct set of FIXUP functions */
	if (mdio->pcspma_is_e_tile) {
		nc_mdio_fixup_rsfec_read = nc_mdio_fixup_etile_pcs_read;
	} else if ((mdio->pcspma_is_f_tile) && (mdio->speed > 100)) {
		nc_mdio_fixup_rsfec_read = nc_mdio_fixup_ftile_rsfec_read;
	} else if (mdio->pcspma_is_f_tile) {
		nc_mdio_fixup_rsfec_read = nc_mdio_fixup_ftile_pcs_read;
	}

	if (mdio->pcspma_is_f_tile) {
		if (
				(addr >= 200) ||
				(addr == 33)  ||
				(addr == 44)  ||
				(addr == 45)) {
			return nc_mdio_fixup_rsfec_read(mdio, prtad,  devad, addr);
		}
	}

	if (mdio->pcspma_is_e_tile) {
		if (
				(addr >= 200) ||
				(addr == 33)  ||
				(addr == 44)  ||
				(addr == 45)) {
			while (!locked) {
				locked = nfb_comp_lock(comp, (PCS_IFC | ATTR_IFC) ); /* Lock access to PCS regs */
			}
			val = nc_mdio_fixup_rsfec_read(mdio, prtad,  devad, addr);
			nfb_comp_unlock(comp, (PCS_IFC | ATTR_IFC)); /* Unlock access to PCS regs */
			return val;
		}
	}
	return mdio->mdio_read(comp, prtad, devad, addr);

}

static inline int nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	void (*nc_mdio_fixup_set_mode)(struct nc_mdio *mdio, int prtad, uint16_t val) = NULL;
	void (*nc_mdio_fixup_set_loopback)(struct nc_mdio *mdio, int prtad, int enable) = NULL;

	/* Select correct set of FIXUP functions */
	if (mdio->pcspma_is_e_tile) {
		nc_mdio_fixup_set_mode = nc_mdio_fixup_etile_set_mode;
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
