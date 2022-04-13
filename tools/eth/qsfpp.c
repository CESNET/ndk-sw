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

#include <nfb/nfb.h>
#include <netcope/i2c_ctrl.h>

/* I2C registers addresses */
#define TEMPERATURE   22
#define RX_IN         34
#define STXDISABLE    86
#define V_NAME_FIRST 148
#define V_NAME_LAST  164
#define V_PN_FIRST   168
#define V_PN_LAST    184
#define COMPLIANCE   131
#define CONNECTOR    130
#define REVISION     184
#define WAVELENGTH   186
#define WAVELEN_TOL  188
#define EXT_SPEC_COMPLIANCE     192

#define CHANNELS 4

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

const char *qsfpp_get_compliance_ext(uint32_t reg)
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
const char *qsfpp_get_connector(uint32_t reg)
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
 * \brief Print ASCII text stored in QSFP registers first..last
 *
 * @param ifc Interface device
 * @param space Interface space
 * @param sel Channel select
 * @param first First register
 * @param last Last register (not printed)
 */
void qsfpp_print_text(struct nc_i2c_ctrl *i2c, int first, int last)
{
	int i;
	uint32_t c;
	i2c_set_data_bytes(i2c, 1);
	for (i = first; i < last; i++) {
		nc_i2c_read(i2c, i, &c);
		if (c == 0) {
			break;
		}
		printf("%c", c);
	}
	printf("\n");
}

/**
 * \brief Fallback mode - determine plugged transceiver, based on wrong temperature
 *
 * @param i Interface device
 * @param s Interface space
 * @return transceiver is present(plugged) = 1, else 0
 */
int qsfp_present(struct nc_i2c_ctrl *i2c, int mdev __attribute__((unused)) )
{
	uint32_t reg;
	i2c_set_addr(i2c, 0xA0);
	i2c_set_data_bytes(i2c, 2);
	nc_i2c_read(i2c, TEMPERATURE, &reg);
	if (reg == 0xFFFF)
		return 0;
	return 1;
}

/**
 * \brief Print informations about transceiver
 *
 * @param ifc Interface device
 * @param space Interface space
 */
void qsfpp_print(struct nfb_device *dev, int nodeoffset, int node_params)
{
	uint8_t i;
	uint32_t reg;
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

	i2c_set_addr(ctrl, i2c_addr);

	i2c_set_data_bytes(ctrl, 2);

	nc_i2c_read(ctrl, TEMPERATURE, &reg);
	printf("Temperature                : %.2f C\n", ((float) reg) / 256);

	printf("Vendor name                : ");
	qsfpp_print_text(ctrl, V_NAME_FIRST, V_NAME_LAST);

	printf("Vendor serial number       : ");
	qsfpp_print_text(ctrl, 196, 211);

	printf("Vendor PN                  : ");
	qsfpp_print_text(ctrl, V_PN_FIRST, V_PN_LAST);

	nc_i2c_read(ctrl, COMPLIANCE, &reg);
	printf("Compliance                 : ");
	if (reg & 0x80) {
		nc_i2c_read(ctrl, EXT_SPEC_COMPLIANCE, &reg);
		printf("%s\n", qsfpp_get_compliance_ext(reg));
	} else {
		qsfpp_print_compliance(reg);
	}

	nc_i2c_read(ctrl, CONNECTOR, &reg);
	printf("Connector                  : %s\n", qsfpp_get_connector(reg));

	printf("Revision                   : ");
	qsfpp_print_text(ctrl, REVISION, REVISION + 2);

	i2c_set_data_bytes(ctrl, 2);
	nc_i2c_read(ctrl, WAVELENGTH, &reg);
	printf("Wavelength                 : %.2f nm ", ((float) reg) / 20);

	nc_i2c_read(ctrl, WAVELEN_TOL, &reg);
	printf("+- %.2f nm\n", ((float) reg) / 200);

	printf("\nRX input power\n");
	for (i = 0; i < CHANNELS; i++) {
		nc_i2c_read(ctrl, RX_IN + i * 2, &reg);
		printf(" * Lane %d                  : %.2f %s (%.2f dBm)\n", i + 1,
			(reg < 10000) ? ((float)reg) / 10 : ((float) reg) / 10000 ,
			(reg < 10000) ? "uW" : "mW", 10 * log10(((float) reg) / 10000));
	}

	i2c_set_data_bytes(ctrl, 1);
	nc_i2c_read(ctrl, STXDISABLE, &reg);

	printf("\nSoftware TX disable\n");
	for (i = 0; i < CHANNELS; i++) {
		printf(" * Lane %d                  : %sactive\n", i + 1, (reg & (1 << i)) ? "" : "in");
	}

	nc_i2c_close(ctrl);
}

/**
 * \brief Software disable over i2c
 *
 * @param ifc Interface device
 * @param ifc_space Interface space
 */
void qsfpp_stxdisable(struct nc_i2c_ctrl *ctrl)
{
	i2c_set_addr(ctrl, 0xA0);
	i2c_set_data_bytes(ctrl, 1);
	nc_i2c_write(ctrl, 86, 0xFF);
}
