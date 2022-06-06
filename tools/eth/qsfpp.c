/* SPDX-License-Identifier: GPL-2.0 */
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

#include <nfb/nfb.h>
#include <netcope/i2c_ctrl.h>

/* I2C registers addresses */
#define SFF8636_IDENTIFIER        0
#define SFF8636_REV_COMPLIANCE    1
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
void qsfpp_print_compliance(uint32_t reg)
{
	if (reg & 0x1) {
		printf("40G Active Cable (XLPPI)\n");
	} else if (reg & 0x2) {
		printf("40GBASE-LR4\n");
	} else if (reg & 0x4) {
		printf("40GBASE-SR4\n");
	} else if (reg & 0x8) {
		printf("40GBASE-CR4\n");
	} else if (reg & 0x10) {
		printf("10GBASE-SR\n");
	} else if (reg & 0x20) {
		printf("10GBASE-LR\n");
	} else if (reg & 0x40) {
		printf("10GBASE-LRM\n");
	} else {
		printf("Reserved\n");
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
	case 0x0C: return "MPO";
	case 0x20: return "HSSDC II";
	case 0x21: return "Copper pigtail";
	case 0x22: return "RJ45";
	case 0x23: return "No separable connector";
	default:   return "Unknown or unspecified";
	}
}

/**
 * \brief Print ASCII text stored in QSFP registers
 *
 * @param i2c I2C controller
 * @param reg Register offset
 * @param count Maximal number of characters
 */

void qsfp_i2c_text_print(struct nc_i2c_ctrl *i2c, uint8_t reg, unsigned count)
{
	/* INFO: Some modules doesn't supports continuous reads, disable burst mode */
	const unsigned BURST = 1;
	char text[BURST + 1];

	int ret;
	unsigned cnt;
	unsigned i = 0;

	while (count) {
		cnt = count < BURST ? count : BURST;
		ret = nc_i2c_read_reg(i2c, reg + i, (uint8_t*)text, cnt);

		if (ret < 0)
			break;
		text[ret] = 0;

		printf(text);
		count -= cnt;
		i += cnt;

		if (strlen(text) < cnt)
			break;
	}
	printf("\n");
}

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

void sff8636_print(struct nc_i2c_ctrl *ctrl);

/**
 * \brief Print informations about transceiver
 *
 * @param ifc Interface device
 * @param space Interface space
 */
void qsfpp_print(struct nfb_device *dev, int nodeoffset, int node_params)
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
		return;
	}

	nc_i2c_set_addr(ctrl, i2c_addr);

	sff8636_print(ctrl);

	nc_i2c_close(ctrl);
}

void sff8636_print(struct nc_i2c_ctrl *ctrl)
{
	const int CHANNELS = 4;

	uint8_t i;
	uint8_t reg = 0xFF;
	uint16_t reg16 = 0;

	reg16 = qsfp_i2c_read16(ctrl, SFF8636_TEMPERATURE);
	printf("Temperature                : %.2f C\n", ((float) reg16) / 256);

	printf("Vendor name                : ");
	qsfp_i2c_text_print(ctrl, SFF8636_VENDOR_NAME, 16);

	printf("Vendor serial number       : ");
	qsfp_i2c_text_print(ctrl, SFF8636_VENDOR_SN, 16);

	printf("Vendor PN                  : ");
	qsfp_i2c_text_print(ctrl, SFF8636_VENDOR_PN, 16);

	nc_i2c_read_reg(ctrl, SFF8636_COMPLIANCE, &reg, 1);
	printf("Compliance                 : ");
	if (reg & 0x80) {
		nc_i2c_read_reg(ctrl, SFF8636_LINK_CODES, &reg, 1);
		printf("%s\n", sff8024_get_ext_compliance(reg));
	} else {
		qsfpp_print_compliance(reg);
	}

	nc_i2c_read_reg(ctrl, SFF8636_CONNECTOR, &reg, 1);
	printf("Connector                  : %s\n", sff8024_get_connector(reg));

	printf("Revision                   : ");
	qsfp_i2c_text_print(ctrl, SFF8636_REVISION, 2);

	reg16 = qsfp_i2c_read16(ctrl, SFF8636_WAVELENGTH);
	printf("Wavelength                 : %.2f nm ", ((float) reg16) / 20);

	reg16 = qsfp_i2c_read16(ctrl, SFF8636_WAVELENGTH_TOL);
	printf("+- %.2f nm\n", ((float) reg16) / 200);

	printf("\nRX input power\n");
	for (i = 0; i < CHANNELS; i++) {
		reg16 = qsfp_i2c_read16(ctrl, SFF8636_RX_INPUT_POWER + i * 2);
		printf(" * Lane %d                  : %.2f %s (%.2f dBm)\n", i + 1,
			(reg16 < 10000) ? ((float)reg16) / 10 : ((float) reg16) / 10000 ,
			(reg16 < 10000) ? "uW" : "mW", 10 * log10(((float) reg16) / 10000));
	}

	nc_i2c_read_reg(ctrl, SFF8636_STXDISABLE, &reg, 1);

	printf("\nSoftware TX disable\n");
	for (i = 0; i < CHANNELS; i++) {
		printf(" * Lane %d                  : %sactive\n", i + 1, (reg & (1 << i)) ? "" : "in");
	}
}

/**
 * \brief Software disable over i2c
 *
 * @param ifc Interface device
 * @param ifc_space Interface space
 */
void qsfpp_stxdisable(struct nc_i2c_ctrl *ctrl)
{
	/* FIXME: get correct i2c address and check for SFF8636 type! */
	uint8_t reg = 0xFF;
	nc_i2c_set_addr(ctrl, 0xA0);
	nc_i2c_write_reg(ctrl, SFF8636_STXDISABLE, &reg, 1);
}
