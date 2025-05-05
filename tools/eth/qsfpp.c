/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Ethernet interface configuration tool - QSFP+ control
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <stdio.h>
#include <math.h>
#include <arpa/inet.h>


#include "eth.h"

/* I2C registers addresses */
#define SFF8636_IDENTIFIER        0
#define SFF8636_REV_COMPLIANCE    1
#define SFF8636_STATUS            2
#define SFF8636_TEMPERATURE      22
#define SFF8636_RX_INPUT_POWER   34
#define SFF8636_STXDISABLE       86
#define SFF8636_CONNECTOR       130
#define SFF8636_COMPLIANCE      131
#define SFF8636_VENDOR_NAME     148
#define SFF8636_VENDOR_PN       168
#define SFF8636_REVISION        184
#define SFF8636_WAVELENGTH      186
#define SFF8636_WAVELENGTH_TOL  188
#define SFF8636_LINK_CODES      192
#define SFF8636_VENDOR_SN       196

#define CMIS_REVISION             1
#define CMIS_GLOBAL_STATUS        3
#define CMIS_TEMPERATURE         14
#define CMIS_MEDIA_TYPE          85
#define CMIS_HOST_LANE_COUNT     88

#define CMIS_BANK_SELECT        126
#define CMIS_PAGE_SELECT        127

/* PAGE 0x00 */
#define CMIS_VENDOR_NAME        129
#define CMIS_VENDOR_PN          148
#define CMIS_VENDOR_SN          166
#define CMIS_MEDIA_INTERFACE_T  212

/* PAGE 0x11 */
#define CMIS_OPTICAL_POWER_RX   186

static inline uint16_t qsfp_i2c_read16(struct nc_i2c_ctrl *ctrl, uint8_t reg)
{
	uint16_t reg16 = 0;

	/* INFO: Some modules doesn't supports continuous reads, disable burst mode */
#if 0
	nc_i2c_read_reg(ctrl, reg, (uint8_t*) &reg16, 2);
#else
	nc_i2c_read_reg(ctrl, reg, (uint8_t*) &reg16, 1);
	nc_i2c_read_reg(ctrl, reg + 1, ((uint8_t*) &reg16) + 1, 1);
#endif
	return ntohs(reg16);
}

/**
 * \brief Print compliance
 *
 * @param reg Register
 */
const char *qsfpp_get_compliance(uint32_t reg)
{
	if (reg & 0x1) {
		return "40G Active Cable (XLPPI)";
	} else if (reg & 0x2) {
		return "40GBASE-LR4";
	} else if (reg & 0x4) {
		return "40GBASE-SR4";
	} else if (reg & 0x8) {
		return "40GBASE-CR4";
	} else if (reg & 0x10) {
		return "10GBASE-SR";
	} else if (reg & 0x20) {
		return "10GBASE-LR";
	} else if (reg & 0x40) {
		return "10GBASE-LRM";
	} else {
		return "Reserved";
	}
}

const char *sff8024_get_ext_compliance(uint8_t reg)
{
	switch (reg) {
	case 0x00: return "Unspecified";
	case 0x01: return "100G AOC or 25GAUI C2M AOC";
	case 0x02: return "100GBASE-SR4 or 25GBASE-SR";
	case 0x03: return "100GBASE-LR4 or 25GBASE-LR";
	case 0x04: return "100GBASE-ER4 or 25GBASE-ER";
	case 0x05: return "100GBASE-SR10";
	case 0x06: return "100G CWDM4";
	case 0x07: return "100G PSM4 Parallel SMF";
	case 0x08: return "100G ACC or 25GAUI C2M ACC";
	case 0x09: return "Obsolete";
	case 0x0B: return "100GBASE-CR4 or 25GBASE-CR CA-L";
	case 0x0C: return "25GBASE-CR CA-S";
	case 0x0D: return "25GBASE-CR CA-N";
	case 0x10: return "40GBASE-ER4";
	case 0x11: return "4 x 10GBASE-SR";
	case 0x12: return "40G PSM4 Parallel SMF";
	case 0x13: return "G959.1 profile P1I1-2D1";
	case 0x14: return "G959.1 profile P1S1-2D2";
	case 0x15: return "G959.1 profile P1L1-2D2";
	case 0x16: return "10GBASE-T with SFI electrical interface";
	case 0x17: return "100G CLR4";
	case 0x18: return "100G AOC or 25GAUI C2M AOC";
	case 0x19: return "100G ACC or 25GAUI C2M ACC";
	case 0x1A: return "100GE-DWDM2";
	case 0x1B: return "100G 1550nm WDM";
	case 0x1C: return "10GBASE-T Short Reach";
	case 0x1D: return "5GBASE-T";
	case 0x1E: return "2.5GBASE-T";
	case 0x1F: return "40G SWDM4";
	case 0x20: return "100G SWDM4";
	case 0x21: return "100G PAM4 BiDi";
	case 0x37: return "10GBASE-BR (Clause 158)";
	case 0x38: return "25GBASE-BR (Clause 159)";
	case 0x39: return "50GBASE-BR (Clause 160)";
	case 0x22: return "4WDM-10 MSA (10km version of 100G CWDM4 with same RS(528,514) FEC in host system)";
	case 0x23: return "4WDM-20 MSA (20km version of 100GBASE-LR4 with RS(528,514) FEC in host system)";
	case 0x24: return "4WDM-40 MSA (40km reach with APD receiver and RS(528,514) FEC in host system)";
	case 0x25: return "100GBASE-DR (Clause 140), CAUI-4 (no FEC)";
	case 0x26: return "100G-FR or 100GBASE-FR1 (Clause 140), CAUI-4 (no FEC)";
	case 0x27: return "100G-LR or 100GBASE-LR1 (Clause 140), CAUI-4 (no FEC)";
	case 0x28: return "100GBASE-SR (P802.3db, Clause 167), CAUI-4 (no FEC)";
	case 0x3A: return "100GBASE-VR (P802.3db, Clause 167), CAUI-4 (no FEC)";
	case 0x29: return "100GBASE-SR, 200GBASE-SR2 or 400GBASE-SR4 (P802.3db, Clause 167)";
	case 0x36: return "100GBASE-VR, 200GBASE-VR2 or 400GBASE-VR4 (P802.3db, Clause 167)";
	case 0x2A: return "100GBASE-FR1 (P802.3cu, Clause 140)";
	case 0x2B: return "100GBASE-LR1 (P802.3cu, Clause 140)";
	case 0x2C: return "100G-LR1-20 MSA, CAUI-4 (no FEC)";
	case 0x2D: return "100G-ER1-30 MSA, CAUI-4 (no FEC)";
	case 0x2E: return "100G-ER1-40 MSA, CAUI-4 (no FEC)";
	case 0x2F: return "100G-LR1-20 MSA";
	case 0x34: return "100G-ER1-30 MSA";
	case 0x35: return "100G-ER1-40 MSA";
	case 0x30: return "Active Copper Cable with 50GAUI, 100GAUI-2 or 200GAUI-4 C2M. Providing a worst BER of 10-6 or below";
	case 0x31: return "Active Optical Cable with 50GAUI, 100GAUI-2 or 200GAUI-4 C2M. Providing a worst BER f 10-6 or below";
	case 0x32: return "Active Copper Cable with 50GAUI, 100GAUI-2 or 200GAUI-4 C2M. Providing a worst BER of 2.6x10-4 for ACC, 10-5 for AUI, or below";
	case 0x33: return "Active Optical Cable with 50GAUI, 100GAUI-2 or 200GAUI-4 C2M. Providing a worst BER f 2.6x10-4 for AOC, 10-5 for AUI, or below";
	case 0x3F: return "100GBASE-CR1, 200GBASE-CR2 or 400GBASE-CR4 (P802.3ck, Clause 162)";
	case 0x40: return "50GBASE-CR, 100GBASE-CR2, or 200GBASE-CR4";
	case 0x41: return "50GBASE-SR, 100GBASE-SR2, or 200GBASE-SR4";
	case 0x42: return "50GBASE-FR or 200GBASE-DR4";
	case 0x4A: return "50GBASE-ER (IEEE 802.3cn, Clause 139)";
	case 0x43: return "200GBASE-FR4";
	case 0x44: return "200G 1550 nm PSM4";
	case 0x45: return "50GBASE-LR";
	case 0x46: return "200GBASE-LR4";
	case 0x47: return "400GBASE-DR4 (802.3, Clause 124), 100GAUI-1 C2M (Annex 120G)";
	case 0x48: return "400GBASE-FR4 (802.3cu, Clause 151)";
	case 0x49: return "400GBASE-LR4-6 (802.3cu, Clause 151)";
	case 0x4B: return "400G-LR4-10";
	case 0x4C: return "400GBASE-ZR (802.3cw, Clause 156)";
	case 0x7F: return "256GFC-SW4 (FC-PI-7P)";
	case 0x80: return "64GFC (FC-PI-7)";
	case 0x81: return "128GFC (FC-PI-8)";

	default: return "Reserved";
	}
}

/**
 * \brief Get connector type
 *
 * @param reg Regiter
 */
const char *sff8024_get_connector(uint8_t reg)
{
	switch (reg) {
	case 0x01: return "SC";
	case 0x02: return "FC Style 1 copper connector";
	case 0x03: return "FC Style 2 copper connector";
	case 0x04: return "BNC/TNC";
	case 0x05: return "FC coax headers";
	case 0x06: return "Fiberjack";
	case 0x07: return "LC";
	case 0x08: return "MT-RJ";
	case 0x09: return "MU";
	case 0x0A: return "SG";
	case 0x0B: return "Optical Pigtail";
	case 0x0C: return "MPO 1x12";
	case 0x0D: return "MPO 2x16";
	case 0x20: return "HSSDC II";
	case 0x21: return "Copper pigtail";
	case 0x22: return "RJ45";
	case 0x23: return "No separable connector";
	case 0x24: return "MXC 2x16";
	case 0x25: return "CS optical connector";
	case 0x26: return "SN optical connector";
	case 0x27: return "MPO 2x12";
	case 0x28: return "MPO 1x16";
	default:   return "Unknown or unspecified";
	}
}

const char *qsfp_get_identifier(uint32_t reg)
{
	switch (reg) {
	case 0x00: return "Unknown or unspecified";
	case 0x01: return "GBIC";
	case 0x02: return "Module/connector soldered to motherboard";
	case 0x03: return "SFP/SFP+/SFP28";
	case 0x04: return "300 pin XBI";
	case 0x05: return "XENPAK";
	case 0x06: return "XFP";
	case 0x07: return "XFF";
	case 0x08: return "XFP-E";
	case 0x09: return "XPAK";
	case 0x0A: return "X2";
	case 0x0B: return "DWDM-SFP/SFP+";
	case 0x0C: return "QSFP";
	case 0x0D: return "QSFP+";
	case 0x0E: return "CXP";
	case 0x0F: return "Shielded Mini Multilane HD 4X";
	case 0x10: return "Shielded Mini Multilane HD 8X";
	case 0x11: return "QSFP28";
	case 0x12: return "CXP2";
	case 0x13: return "CDFP (Style 1/Style2)";
	case 0x14: return "Shielded Mini Multilane HD 4X Fanout Cable";
	case 0x15: return "Shielded Mini Multilane HD 8X Fanout Cable";
	case 0x16: return "CDFP (Style 3)";
	case 0x17: return "microQSFP";
	case 0x18: return "QSFP-DD";
	default:   return "Unknown or unspecified";
	}
}

const char *cmis_module_state(uint8_t reg)
{
	switch (reg) {
	case 1: return "ModuleLowPwr";
	case 2: return "ModulePwrUp";
	case 3: return "ModuleReady";
	case 4: return "ModulePwrDn";
	case 5: return "ModuleFault";
	default: return "Unknown";
	}
}

const char *cmis_mtf(uint8_t reg)
{
	switch (reg) {
	case 0x00: return "Undefined";
	case 0x01: return "Optical Interfaces: MMF";
	case 0x02: return "Optical Interfaces: SMF";
	case 0x03: return "Passive Copper Cables";
	case 0x04: return "Active Cables";
	case 0x05: return "BASE-T";
	default: return "Reserved";
	}
}

const char *cmis_mit(uint8_t reg)
{
	switch (reg) {
	case 0x00: return "850 nm VCSEL";
	case 0x01: return "1310 nm VCSEL";
	case 0x02: return "1550 nm VCSEL";
	case 0x03: return "1310 nm FP";
	case 0x04: return "1310 nm DFB";
	case 0x05: return "1550 nm DFB";
	case 0x06: return "1310 nm EML";
	case 0x07: return "1550 nm EML";
	case 0x08: return "Others";
	case 0x09: return "1490 nm DFB";
	case 0x0A: return "Copper cable unequalized";
	case 0x0B: return "Copper cable passive equalized";
	case 0x0C: return "Copper cable, near and far end limiting active equalizers";
	case 0x0D: return "Copper cable, far end limiting active equalizers";
	case 0x0E: return "Copper cable, near end limiting active equalizers";
	case 0x0F: return "Copper cable, linear active equalizers";
	case 0x10: return "C-band tunable laser";
	case 0x11: return "L-band tunable laser";
	default:   return "Reserved";
	}
}

/**
 * \brief Print ASCII text stored in QSFP registers
 *
 * @param i2c I2C controller
 * @param reg Register offset
 * @param count Maximal number of characters
 */

int qsfp_i2c_text_print(FILE *fout, struct nc_i2c_ctrl *i2c, uint8_t reg, unsigned count)
{
	/* INFO: Some modules doesn't supports continuous reads, disable burst mode */
	const unsigned BURST = 1;
	char text[BURST + 1];

	int ret;
	int rcnt = 0;
	unsigned cnt;
	unsigned i = 0;
	int j;
	int spaces = 0;

	while (count) {
		cnt = count < BURST ? count : BURST;
		ret = nc_i2c_read_reg(i2c, reg + i, (uint8_t*)text, cnt);

		if (ret < 0)
			break;
		text[ret] = 0;

		for (j = 0; j < ret; j++) {
			if (text[j] == ' ') {
				spaces++;
			} else {
				rcnt += fprintf(fout, "%*s%c", spaces, "", text[j]);
				spaces = 0;
			}
		}

		count -= cnt;
		i += cnt;

		if (strlen(text) < cnt)
			break;
	}
	return rcnt;
}

int _print_qsfp_i2c_text(FILE*out, struct nc_i2c_ctrl*ctrl, int item, int json)
{
	int ret = 0;
	int base = 0;
	int size = 0;
	switch (item) {
	case NI_SFF8636_VNDR_NAME:      size = 16; base = SFF8636_VENDOR_NAME; break;
	case NI_SFF8636_VNDR_SN:        size = 16; base = SFF8636_VENDOR_SN; break;
	case NI_SFF8636_VNDR_PN:        size = 16; base = SFF8636_VENDOR_PN; break;
	case NI_SFF8636_REVISION:       size =  2; base = SFF8636_REVISION; break;
	case NI_TRN_CMIS_VNDR_NAME:     size = 16; base = CMIS_VENDOR_NAME; break;
	case NI_TRN_CMIS_VNDR_SN:       size = 16; base = CMIS_VENDOR_SN; break;
	case NI_TRN_CMIS_VNDR_PN:       size = 16; base = CMIS_VENDOR_PN; break;
	}
	if (size == 0)
		return 0;
	ret += fprintf(out, json ? "\"": "");
	ret += qsfp_i2c_text_print(out, ctrl, base, size);
	ret += fprintf(out, json ? "\"": "");
	return ret;
}

int print_json_qsfp_i2c_text(void *priv, int item, struct nc_i2c_ctrl *ctrl)
{
	struct ni_json_cbp *p = priv;
	return _print_qsfp_i2c_text(p->f, ctrl, item, 1);
}

int print_user_qsfp_i2c_text(void *priv, int item, struct nc_i2c_ctrl *ctrl)
{
	struct ni_user_cbp *p = priv;
	return _print_qsfp_i2c_text(p->f, ctrl, item, 0);
}

#if 0
/**
 * \brief Fallback mode - determine plugged transceiver by reading first register
 *
 * @param i2c I2C controller
 * @return transceiver is present(plugged) = 1, else 0
 */
int qsfp_present(struct nc_i2c_ctrl *i2c, int mdev __attribute__((unused)) )
{
	uint8_t reg = 0xFF;
	/* FIXME: get correct i2c address and check for SFF8636 type! */
	nc_i2c_set_addr(i2c, 0xA0);
	nc_i2c_read_reg(i2c, SFF8636_IDENTIFIER, &reg, 1);
	if (reg == 0xFF)
		return 0;
	return 1;
}
#endif

void sff8636_print(struct ni_context *ctx, struct nc_i2c_ctrl *ctrl);
void cmis_print(struct ni_context *ctx, struct nc_i2c_ctrl *ctrl);

struct nc_i2c_ctrl *qsfpp_i2c_open(struct nfb_device *dev, int nodeoffset, int node_params)
{
	uint32_t i2c_addr;
	struct nc_i2c_ctrl *ctrl;

	const void *fdt;
	const fdt32_t *prop32;
	int proplen;
	int node_ctrl;

	fdt = nfb_get_fdt(dev);

	prop32 = fdt_getprop(fdt, nodeoffset, "control", &proplen);
	node_ctrl = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop32));

	i2c_addr = fdt_getprop_u32(fdt, node_params, "i2c-addr", &proplen);
	if (proplen != sizeof i2c_addr)
		i2c_addr = 0xA0;

	ctrl = nc_i2c_open(dev, node_ctrl);
	if (ctrl == NULL) {
		warnx("Cannot open I2C ctrl for transceiver");
		return NULL;
	}

	nc_i2c_set_addr(ctrl, i2c_addr);
	return ctrl;
}

/**
 * \brief Print informations about transceiver
 *
 * @param ifc Interface device
 * @param space Interface space
 */
void qsfpp_print(struct ni_context *ctx, struct nfb_device *dev, int nodeoffset, int node_params)
{
	uint8_t reg = 0xFF;
	struct nc_i2c_ctrl *ctrl;

	ctrl = qsfpp_i2c_open(dev, nodeoffset, node_params);
	if (ctrl == NULL) {
		warnx("Cannot open I2C ctrl for transceiver");
		return;
	}

	nc_i2c_read_reg(ctrl, SFF8636_IDENTIFIER, &reg, 1);
	ni_item_str(ctx, NI_MOD_IDENT, qsfp_get_identifier(reg));

	if (reg == 0x18) {
		cmis_print(ctx, ctrl);
	} else {
		sff8636_print(ctx, ctrl);
	}

	nc_i2c_close(ctrl);
}

void sff8636_print(struct ni_context *ctx, struct nc_i2c_ctrl *ctrl)
{
	const char *text;
	const int CHANNELS = 4;

	uint8_t i;
	uint8_t reg = 0xFF;
	uint16_t reg16 = 0;
	int retries = 0;
	int ret;
	double pwr;

	/* Wait for Data Ready (max 2 sec according specs) */
	/* TODO: this should be done in qsfp_i2c_common functions */
	/* TODO: use nfb_comp_read32_poll_timeout, as this implemenation is time unstable */
	do {
		ret = nc_i2c_read_reg(ctrl, SFF8636_STATUS, &reg, 1);
	} while (ret == 1 && ((reg & 0x01) == 0x1) && (++retries < 10000));

	reg16 = qsfp_i2c_read16(ctrl, SFF8636_TEMPERATURE);

	ni_item_double(ctx, NI_SFF8636_TEMP, ((float) reg16) / 256);
	ni_item_qsfp_i2c_text(ctx, NI_SFF8636_VNDR_NAME, ctrl);
	ni_item_qsfp_i2c_text(ctx, NI_SFF8636_VNDR_SN, ctrl);
	ni_item_qsfp_i2c_text(ctx, NI_SFF8636_VNDR_PN, ctrl);

	nc_i2c_read_reg(ctrl, SFF8636_COMPLIANCE, &reg, 1);
	if (reg & 0x80) {
		nc_i2c_read_reg(ctrl, SFF8636_LINK_CODES, &reg, 1);
		text = sff8024_get_ext_compliance(reg);
	} else {
		text = qsfpp_get_compliance(reg);
	}
	ni_item_str(ctx, NI_TRN_COMPLIANCE, text);

	nc_i2c_read_reg(ctrl, SFF8636_CONNECTOR, &reg, 1);
	ni_item_str(ctx, NI_TRN_CONNECTOR, sff8024_get_connector(reg));
	ni_item_qsfp_i2c_text(ctx, NI_SFF8636_REVISION, ctrl);

	reg16 = qsfp_i2c_read16(ctrl, SFF8636_WAVELENGTH);
	ni_item_double(ctx, NI_SFF8636_WL, ((float) reg16) / 20);

	reg16 = qsfp_i2c_read16(ctrl, SFF8636_WAVELENGTH_TOL);
	ni_item_double(ctx, NI_SFF8636_WL_TOL, ((float) reg16) / 200);

	ni_list(ctx, NI_LIST_TRN_RX_IN_PWR);
	for (i = 0; i < CHANNELS; i++) {
		reg16 = qsfp_i2c_read16(ctrl, SFF8636_RX_INPUT_POWER + i * 2);
		pwr = ((float)reg16) / 10000000;

		ni_item_int(ctx, NI_TRANS_RX_IN_PWR_L, i + 1);
		ni_item_pwr(ctx, NI_TRANS_RX_IN_PWR_V, pwr);
	}
	ni_endlist(ctx, NI_LIST_TRN_RX_IN_PWR);

	nc_i2c_read_reg(ctrl, SFF8636_STXDISABLE, &reg, 1);

	ni_list(ctx, NI_LIST_TRN_STX_DIS);
	for (i = 0; i < CHANNELS; i++) {
		ni_item_int(ctx, NI_TRANS_STX_DIS_L, i + 1);
		ni_item_ctrl_reg(ctx, NI_TRANS_STX_DIS_V, (reg & (1 << i)));
	}
	ni_endlist(ctx, NI_LIST_TRN_STX_DIS);
}

void cmis_print(struct ni_context *ctx, struct nc_i2c_ctrl *ctrl)
{
	int channel_cnt = 8;

	uint8_t i;
	uint8_t reg = 0;
	uint16_t reg16 = 0;
	double pwr;

	nc_i2c_write_reg(ctrl, CMIS_PAGE_SELECT, &reg, 1);

	nc_i2c_read_reg(ctrl, CMIS_REVISION, &reg, 1);
	ni_item_int(ctx, NI_TRN_CMIS_VER_MAJ, reg >> 4);
	ni_item_int(ctx, NI_TRN_CMIS_VER_MIN, reg & 0xF);

	nc_i2c_read_reg(ctrl, CMIS_GLOBAL_STATUS, &reg, 1);
	ni_item_str(ctx, NI_TRN_CMIS_GLB_STAT, cmis_module_state((reg >> 1) & 0x7));

	reg16 = qsfp_i2c_read16(ctrl, CMIS_TEMPERATURE);
	ni_item_double(ctx, NI_SFF8636_TEMP, ((float) reg16) / 256);

	ni_item_qsfp_i2c_text(ctx, NI_TRN_CMIS_VNDR_NAME, ctrl);
	ni_item_qsfp_i2c_text(ctx, NI_TRN_CMIS_VNDR_SN, ctrl);
	ni_item_qsfp_i2c_text(ctx, NI_TRN_CMIS_VNDR_PN, ctrl);

	nc_i2c_read_reg(ctrl, CMIS_MEDIA_TYPE, &reg, 1);
	ni_item_str(ctx, NI_TRN_CMIS_MED_T, cmis_mtf(reg));

	nc_i2c_read_reg(ctrl, CMIS_MEDIA_INTERFACE_T, &reg, 1);
	ni_item_str(ctx, NI_TRN_CMIS_IFC_T, cmis_mit(reg));

	if (nc_i2c_read_reg(ctrl, CMIS_HOST_LANE_COUNT, &reg, 1) == 1) {
		channel_cnt = reg & 0x0F;
	}

	ni_list(ctx, NI_LIST_TRN_RX_IN_PWR);
	for (i = 0; i < channel_cnt; i++) {
		reg = 0x11; nc_i2c_write_reg(ctrl, CMIS_PAGE_SELECT, &reg, 1);
		reg16 = qsfp_i2c_read16(ctrl, CMIS_OPTICAL_POWER_RX + i * 2);
		pwr = ((float)reg16) / 10000000;
		ni_item_int(ctx, NI_TRANS_RX_IN_PWR_L, i + 1);
		ni_item_pwr(ctx, NI_TRANS_RX_IN_PWR_V, pwr);
	}
	ni_endlist(ctx, NI_LIST_TRN_RX_IN_PWR);
}

/**
 * \brief Software disable over i2c
 *
 * @param ifc Interface device
 * @param ifc_space Interface space
 */

int qsfpp_stxdisable(struct nfb_device *dev, int nodeoffset, int node_params, int disable, int channels)
{
	int ret = 0;
	uint8_t reg = 0x18;
	struct nc_i2c_ctrl *ctrl = qsfpp_i2c_open(dev, nodeoffset, node_params);

	if (ctrl == NULL)
		return -ENODEV;

	nc_i2c_read_reg(ctrl, SFF8636_IDENTIFIER, &reg, 1);
	if (reg == 0x18) {
		ret = -ENOTSUP;
	} else {
		channels &= 0x0F;

		nc_i2c_read_reg(ctrl, SFF8636_STXDISABLE, &reg, 1);
		reg = disable ? (reg | channels) : (reg & ~channels);
		nc_i2c_write_reg(ctrl, SFF8636_STXDISABLE, &reg, 1);
	}

	nc_i2c_close(ctrl);
	return ret;
}
