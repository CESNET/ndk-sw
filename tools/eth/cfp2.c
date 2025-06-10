/* SPDX-License-Identifier: BSD-3-Clause */
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

#include "eth.h"

#define TEMPERATURE     0xA02F
#define VEN_NAME_FIRST  0x8021
#define VEN_NAME_LAST   0x8031
#define VEN_PN_FIRST    0x8034
#define VEN_PN_LAST     0x8044
#define VEN_SN_FIRST    0x8044
#define VEN_SN_LAST     0x8054
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
int print_mdio_text(FILE *fout, struct mdio_if_mdev *mdio_if, int item)
{
	uint8_t c;
	int cnt = 0;
	int i;
	int first = 0, last = 0;

	switch (item) {
	case NI_MDIO_VNDR_NAME: first = VEN_NAME_FIRST; last = VEN_NAME_LAST; break;
	case NI_MDIO_SN: first = VEN_SN_FIRST; last = VEN_SN_LAST; break;
	case NI_MDIO_PN: first = VEN_PN_FIRST; last = VEN_PN_LAST; break;
	}

	for (i = first; i < last; i++) {
		c = nc_mdio_read(mdio_if->mdio, mdio_if->mdev, 1, i);
		if (!c) {
			break;
		}
		cnt += fprintf(fout, "%c", c);
	}
	return cnt;
}

int print_mdio_text_user(void *priv, int item, struct mdio_if_mdev *mdio_if)
{
	struct ni_user_cbp *p = priv;
	return print_mdio_text(p->f, mdio_if, item);
}

int print_mdio_text_json(void *priv, int item, struct mdio_if_mdev *mdio_if)
{
	struct ni_json_cbp *p = priv;
	return print_mdio_text(p->f, mdio_if, item);
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
void cfp2_print(struct ni_context *ctx, struct nfb_device *dev, int nodeoffset, int control_params_node)
{
	(void) control_params_node; /* unused */

	int i, channels;
	uint8_t reg8;
	uint16_t reg16;

	const void *fdt = nfb_get_fdt(dev);
	double pwr;

	int node_ctrl;
	int node_ctrlparam;

	const fdt32_t* prop32;
	int proplen;

	struct nc_mdio *mdio;
	struct mdio_if_mdev mdio_if;
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

	mdio = nc_mdio_open(dev, node_ctrl, node_ctrlparam);
	if (mdio == NULL) {
		//warnx("Cannot open MDIO for Eth %d", p->index);
		return;
	}

	mdio_if.mdio = mdio;
	mdio_if.mdev = mdev;

	reg16 = nc_mdio_read(mdio, mdev, 1, TEMPERATURE);

	ni_item_double(ctx, NI_SFF8636_TEMP, ((float) reg16) / 256);

	ni_item_mdio_text(ctx, NI_MDIO_VNDR_NAME, &mdio_if);
	ni_item_mdio_text(ctx, NI_MDIO_SN, &mdio_if);
	ni_item_mdio_text(ctx, NI_MDIO_PN, &mdio_if);

	reg8 = nc_mdio_read(mdio, mdev, 1, COMPLIANCE);
	ni_item_str(ctx, NI_TRN_COMPLIANCE, cfp2_get_compliance(reg8, &channels));

	reg8 = nc_mdio_read(mdio, mdev, 1, CONNECTOR);
	ni_item_str(ctx, NI_TRN_CONNECTOR, cfp2_get_connector(reg8));

	reg8 = nc_mdio_read(mdio, mdev, 1, HW_REV);
	ni_item_double(ctx, NI_MDIO_HW_REV, ((float) reg8) / 10);

	reg8 = nc_mdio_read(mdio, mdev, 1, MGMT_REV);
	ni_item_double(ctx, NI_MDIO_MGMT_REV, ((float) reg8) / 10);

	ni_list(ctx, NI_LIST_TRN_RX_IN_PWR);
	for (i = 0; i < channels; i++) {
		reg16 = nc_mdio_read(mdio, mdev, 1, RX_IN + i);
		pwr = ((float)reg16) / 10000000;

		ni_item_int(ctx, NI_TRANS_RX_IN_PWR_L, i + 1);
		ni_item_pwr(ctx, NI_TRANS_RX_IN_PWR_V, pwr);
	}
	ni_endlist(ctx, NI_LIST_TRN_RX_IN_PWR);
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
