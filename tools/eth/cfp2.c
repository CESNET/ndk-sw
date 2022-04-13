/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - CFP2 control
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <inttypes.h>
#include <stdio.h>
#include <math.h>

#include <nfb/nfb.h>
#include <netcope/mdio.h>

#define TEMPERATURE     0xA02F
#define VEN_NAME_FIRST  0x8021
#define VEN_NAME_LAST   0x8031
#define VEN_PN_FIRST    0x8034
#define VEN_PN_LAST     0x8044
#define CONNECTOR       0x8002
#define COMPLIANCE      0x8003
#define HW_REV          0x8068
#define MGMT_REV        0x8069
#define RX_IN           0xA2D0

/**
 * \brief Print compliance
 *
 * @param reg Register
 * @return number of channels
 */
const char *cfp2_get_compliance(uint8_t reg, int *channels)
{
	switch (reg) {
	case 0x01: *channels =  4; return "100GE-LR4";
	case 0x02: *channels =  4; return "100GE-ER4";
	case 0x03: *channels = 10; return "100GBASE-SR10";
	case 0x04: *channels =  4; return "100GBASE-SR4";
	case 0x05: *channels =  4; return "40GE-LR4";
	case 0x07: *channels =  4; return "40GE-SR4";
	case 0x0D: *channels =  4; return "40GE-CR4 Copper";
	case 0x0E: *channels = 10; return "100GE-CR10 Copper";
	case 0x0F: *channels =  1; return "40G BASE-FR";
	case 0x10: *channels =  1; return "100GE-ZR1";
	case 0x11: *channels =  1; return "100GE-DWDM-Coherent";
	default:   *channels =  0; return "Undefined";
	}
}

/**
 * \brief Print connector type
 *
 * @param reg Regiter
 */
const char *cfp2_get_connector(uint8_t reg)
{
	switch (reg) {
	case 0x01: return "SC";
	case 0x07: return "LC";
	case 0x08: return "MT-RJ";
	case 0x09: return "MPO";
	case 0x0D: return "Angled LC";
	default:   return "Undefined";
	}
}

/**
 * \brief Print ASCII text stored in CFP2 registers first..last
 *
 * @param ifc Interface device
 * @param space Interface space
 * @param first First register
 * @param last Last register (not printed)
 */
void cfp2_print_text(struct nc_mdio *mdio, int first, int last, int mdev)
{
	uint8_t c;
	int i;
	for (i = first; i < last; i++) {
		c = nc_mdio_read(mdio, mdev, 1, i);
		if (!c) {
			break;
		}
		printf("%c", c);
	}
	printf("\n");
}

/**
 * \brief Fallback mode - determine plugged transceiver, based on wrong temperature
 *
 * @param i device
 * @param s device space
 * @return transceiver is present(plugged) = 1, else 0
 */
int cfp_present(struct nfb_device *dev __attribute__((unused)), int nodeoffset __attribute__((unused)) )
{
/*	uint16_t reg16;
	phandle = fdt_getprop(fdt, node_pcspma, "control", 0);
	node_ctrl = fdt_node_offset_by_phandle(fdt,
			fdt32_to_cpu(*(uint32_t *)phandle));

	node_ctrlparam = fdt_subnode_offset(fdt, node_pcspma, "control-param");
	prop = fdt_getprop(fdt, node_ctrlparam, "dev", &proplen);
	if (proplen == sizeof(*prop)) {
		portaddr = fdt32_to_cpu(*prop);
	} else {
		//warnx("Couldn't find control param property in Device Tree");
	}

	mdio = nc_mdio_open(dev, node_ctrl);
	if (mdio == NULL) {
		warnx("Cannot open MDIO for Eth %d", p->index);
		return;
	}
	reg16 = cs_nc_mdio_read(i, s, mdev, 1, TEMPERATURE);
	if (reg16 == 0xFFFF)
		return 0;*/
	return 1;
}

/**
 * \brief Print informations about transceiver
 *
 * @param ifc Interface device
 * @param space Interface space
 */
void cfp2_print(struct nfb_device *dev, int nodeoffset, int control_params_node)
{
	(void) control_params_node; /* unused */

	int i, channels;
	uint8_t reg8;
	uint16_t reg16;

	const void *fdt = nfb_get_fdt(dev);

	int node_ctrl;
	int node_ctrlparam;

	const fdt32_t* prop32;
	int proplen;

	struct nc_mdio *mdio;
	int mdev = 0;
	
	prop32 = fdt_getprop(fdt, nodeoffset, "control", &proplen);
	node_ctrl = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop32));
	node_ctrlparam = fdt_subnode_offset(fdt, nodeoffset, "control-param");
	prop32 = fdt_getprop(fdt, node_ctrlparam, "dev", &proplen);
	if (proplen == sizeof(*prop32)) {
		mdev = fdt32_to_cpu(*prop32);
	} else {
		//warnx("Couldn't find control param property in Device Tree");
	}

	mdio = nc_mdio_open(dev, node_ctrl);
	if (mdio == NULL) {
		//warnx("Cannot open MDIO for Eth %d", p->index);
		return;
	}

	reg16 = nc_mdio_read(mdio, mdev, 1, TEMPERATURE);

	printf("Temperature                : %.2f C\n", ((float) reg16) / 256);

	printf("Vendor name                : ");
	cfp2_print_text(mdio, VEN_NAME_FIRST, VEN_NAME_LAST, mdev);

	printf("Vendor serial number       : ");
	cfp2_print_text(mdio, 0x8044, 0x8054, mdev);

	printf("Vendor PN                  : ");
	cfp2_print_text(mdio, VEN_PN_FIRST, VEN_PN_LAST, mdev);

	reg8 = nc_mdio_read(mdio, mdev, 1, COMPLIANCE);
	printf("Compliance                 : %s\n", cfp2_get_compliance(reg8, &channels));

	reg8 = nc_mdio_read(mdio, mdev, 1, CONNECTOR);
	printf("Connector                  : %s\n", cfp2_get_connector(reg8));

	reg8 = nc_mdio_read(mdio, mdev, 1, HW_REV);
	printf("HW spec. rev.              : %.2f\n", ((float) reg8) / 10);

	reg8 = nc_mdio_read(mdio, mdev, 1, MGMT_REV);
	printf("Managenent ifc. spec. rev  : %.2f\n", ((float) reg8) / 10);

	printf("\nRX input power\n");
	for (i = 0; i < channels; i++) {
		reg16 = nc_mdio_read(mdio, mdev, 1, RX_IN + i);
		printf(" * Lane %2d                 : %.2f %s (%.2f dBm)\n", i + 1,
			(reg16 < 10000) ? ((float)reg16) / 10 : ((float) reg16) / 10000 ,
			(reg16 < 10000) ? "uW" : "mW", 10 * log10(((float) reg16) / 10000));
	}

}

/**
 * \brief Software disable
 *
 * @param ifc Interface device
 * @param ifc_space Interface space
 */
/*void cfp2_stxdisable(cs_device_t *ifc, cs_space_t *ifc_space)
{
	cs_mdio_write(ifc, ifc_space, 0, 1, 0xA013, 0xFFFF);
}*/
