/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - PCS/PMA control
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>

#include <nfb/nfb.h>
#include <netcope/mdio.h>
#include <netcope/eth.h>

#include "eth.h"
#include "ieee802_3.h"

const char *str_active[2] = {"        ", "[active]"};

struct pma_feature_t {
	const char *name;
	int ability_reg;
	int ability_bit;
	int control_reg;
	int control_bit;
} pma_feature_table [] = {
	{"Reset",                           0, -1,     0, 15},
	{"PMA local loopback",              8,  0,     0,  0},
	{"PMA remote loopback",            13, 15,     0,  1},
	{"Low power",                       1,  1,     0, 11},
	{"25G RS-FEC Enable",               0, -1,   200,  2},
	{NULL,                              0,  0,     0,  0},
};

int mdio_read(struct net_device *dev, int prtad, int devad, uint16_t addr)
{
	return nc_mdio_read(dev->mdio, prtad, devad, addr);
}

int mdio_write(struct net_device *dev, int prtad, int devad, uint16_t addr, uint16_t val)
{
	return nc_mdio_write(dev->mdio, prtad, devad, addr, val);
}

void pcspma_print_speed(struct nc_mdio *mdio, int portaddr, uint8_t mdev)
{
	create_mdio_if_info(mdio_info, mdio, portaddr);
	printf("Speed                      : %s\n",
		mdev == 1 ? ieee802_3_get_pma_speed_string(&mdio_info) : ieee802_3_get_pcs_speed_string(&mdio_info));
}

void print_pcspma_common(struct nc_mdio *mdio, int portaddr, uint8_t mdev)
{
	uint32_t reg;
	create_mdio_if_info(mdio_info, mdio, portaddr);

	printf("Link status                : %s | %s\n",
		ieee802_3_get_pcs_pma_link_status_string(&mdio_info, mdev),
		ieee802_3_get_pcs_pma_link_status_string(&mdio_info, mdev));
	pcspma_print_speed(mdio, portaddr, mdev);

	reg = nc_mdio_read(mdio, portaddr, mdev, 8);
	// specification, mask 0x800 or 0x400
	printf("Transmit Fault             : %s\n", (reg & 0x800) ? "Yes" : "No");
	// specification, mask 0x400 or 0x200
	printf("Receive Fault              : %s\n", (reg & 0x400) ? "Yes" : "No");
}

int pcspma_set_type(struct nc_mdio *mdio, int portaddr, struct eth_params *p)
{
	create_mdio_if_info(mdio_info, mdio, portaddr);
	return ieee802_3_set_pma_pmd_type_string(&mdio_info, p->string);
}

int pcspma_set_feature(struct nc_mdio *mdio, int portaddr, struct eth_params *p)
{
	uint16_t reg;

	const struct pma_feature_t *item = pma_feature_table;

	while (item->name) {
		if (!strcmp(p->string, item->name)) {
			reg = nc_mdio_read(mdio, portaddr, 1, item->control_reg);
			reg = p->param ? (reg | (1 << item->control_bit)) : (reg & ~(1 << item->control_bit));
			nc_mdio_write(mdio, portaddr, 1, item->control_reg, reg);
			return 0;
		}
		item++;
	}
	return -1;
}

static inline uint32_t nc_mdio_read_dword(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	return  (nc_mdio_read(mdio, prtad, devad, addr + 0) <<  0) |
		(nc_mdio_read(mdio, prtad, devad, addr + 1) << 16);
}

struct cb_print_pma_pmd_type_priv {
	const char *active;
};

void print_pma_pmd_type(void *p, const char *str)
{
	struct cb_print_pma_pmd_type_priv *priv = p;
	const char * active = strcmp(str, priv->active) ? str_active[0] : str_active[1];

	printf(" * %s                : %s\n", active, str);
}

void pcspma_print_status(struct nc_mdio *mdio, int portaddr, struct eth_params *p)
{
	uint32_t reg;
	uint32_t reg2;

	int lines;
	uint32_t low_bits;
	int i, j;

	const char *active;

	create_mdio_if_info(mdio_info, mdio, portaddr);

	//printf("--------------------------------------- PMA %d regs ----\n", p->index);
	printf("----------------------------------------- PMA regs ----\n");
	print_pcspma_common(mdio, portaddr, 1);
	reg = nc_mdio_read(mdio, portaddr, 1, 7);

	active = ieee802_3_get_pma_pmd_type_string(&mdio_info);
	printf("PMA type                   : %s\n", active);

	if (p->verbose) {
		struct cb_print_pma_pmd_type_priv cb_ppptp;
		const struct pma_feature_t *item;

		printf("Supported PMA types ->\n");

		cb_ppptp.active = active;
		ieee802_3_get_supported_pma_pmd_types_string(&mdio_info, print_pma_pmd_type, &cb_ppptp);

		printf("Supported PMA features ->\n");

		for (item = pma_feature_table; item->name; item++) {
			if (item->ability_bit != -1) {
				reg = nc_mdio_read(mdio, portaddr, 1, item->ability_reg);
			}

			if (reg & (1 << item->ability_bit) || p->verbose > 1) {
				reg = nc_mdio_read(mdio, portaddr, 1, item->control_reg);
				printf(" * %s                : %s\n", str_active[reg & (1 << item->control_bit) ? 1 : 0], item->name);
			}
		}

		/* RS-FEC registers */
		lines = ieee802_3_get_fec_lines(active);
		if (lines > 0) {
			reg = nc_mdio_read_dword(mdio, portaddr, 1, 201);
			printf("RS-FEC status              : %s%s%s\n",
//					reg & (1 <<  0) ? "FEC bypass corection ability | " : "",
//					reg & (1 <<  1) ? "FEC bypass indication ability | " : "",
					reg & (1 <<  2) ? "RS-FEC high SER | " : "",
					reg & (1 << 14) ? "RS-FEC lanes aligned | " : "",
					reg & (1 << 15) ? "PCS lanes aligned | " : "");
			printf("RS-FEC corrected cws       : %d\n", nc_mdio_read_dword(mdio, portaddr, 1, 202));
			printf("RS-FEC uncorrected cws     : %d\n", nc_mdio_read_dword(mdio, portaddr, 1, 204));

			printf("RS-FEC symbol errors ->\n");
			for (i = 0; i < lines; i++) {
				printf(" * Lane %d                  : %d\n", i, nc_mdio_read_dword(mdio, portaddr, 1, 210 + i * 2));
			}
			printf("RS-FEC lane mapping        :");
			reg = nc_mdio_read(mdio, portaddr, 1, 206);
			for (i = 0; i < lines; i++) {
				printf(" %d", ((reg >> (i*2)) & 0x3));
			}
			printf("\n");

			reg = nc_mdio_read(mdio, portaddr, 1, 201);
			printf("RS-FEC AM lock             :");
			for (i = 0; i < lines; i++) {
				printf((reg & (1 << (i + 8))) ? " L" : " X");
			}
			printf("\n");
		}
	}

	//printf("--------------------------------------- PCS %d regs ----\n", p->index);
	printf("----------------------------------------- PCS regs ----\n");
	print_pcspma_common(mdio, portaddr, 3);

	if (p->verbose) {
		// PCS status reg 1 -> 32
		reg = nc_mdio_read(mdio, portaddr, 3, 32);
		// PCS status reg 2 -> 33
		reg2 = nc_mdio_read(mdio, portaddr, 3, 33);
		printf("Global Block Lock          : %s | %s\n",
			(reg & 0x0001) ? "Yes" : "No",
			(reg2 & 0x8000) ? "Yes":"No");
		printf("Global High BER            : %s | %s\n",
			(reg & 0x0002) ? "Yes" : "No",
			(reg2 & 0x4000) ? "Yes":"No");
		// BER counter register -> 44
		reg = nc_mdio_read(mdio, portaddr, 3, 44);
		low_bits = ((reg2 >> 8) & 0x003F);
		printf("BER counter                : %d\n",
			((reg & 0xFFFF) << 6) | low_bits);
		low_bits = (reg2 & 0x00FF);
		// Error blocks register -> 45
		reg = nc_mdio_read(mdio, portaddr, 3, 45);
		printf("Errored blocks             : %d\n",
			((reg & 0x3FFF) << 8) | low_bits);

		lines = ieee802_3_get_pcs_lines(&mdio_info);
		if (lines > 1) {
			printf("\nBlock status for lines     :");
			// Block status register first half (8 lines)   -> 50
			//                       second half (12 lines) -> 51
			//                       total 20 lines (max for 100G)
			reg = nc_mdio_read(mdio, portaddr, 3, 50);
			for (i = 0, j =0; i < lines; i++) {
				if (i == 8) {
					reg = nc_mdio_read(mdio, portaddr, 3, 51);
					j = 0;
				}
				printf((reg & (1 << j)) ? " L" : " X");
				j++;
			}

			printf("\nAM lock                    :");
			// AM lock register first half (8 lines)   -> 52
			//                  second half (12 lines) -> 53
			reg = nc_mdio_read(mdio, portaddr, 3, 52);
			for (i = 0, j = 0; i < lines; i++) {
				if (i == 8) {
					reg = nc_mdio_read(mdio, portaddr, 3, 53);
					j = 0;
				}
				printf((reg & (1 << j)) ? " L" : " X");
				j++;
			}

			printf("\nLane mapping                \n");
			// Lane mapping register for each line starting from -> 400 to 420
			for (i = 0; i < lines; i++) {
				printf(" %2d", nc_mdio_read(mdio, portaddr, 3, 400 + i) & 0x3F);
			}

			printf("\nBIP error counter           \n");
			// BIP counter register for each line starting from -> 200 to 220
			for (i = 0; i < lines; i++) {
				if (i == 10)
					printf("\n");
				printf(" %5d", nc_mdio_read(mdio, portaddr, 3, 200 + i));
			}
			printf("\n");
		}
	}

	nc_mdio_close(mdio);
}

int pcspma_execute_operation(struct nfb_device *dev, int eth_node, struct eth_params *p)
{
	int ret = 0;

	int node_ctrl = -1;
	int node_ctrlparam;

	int proplen;
	const fdt32_t *prop32;

	int portaddr = 0;

	struct nc_mdio *mdio;

	node_ctrl = nc_eth_get_pcspma_control_node(nfb_get_fdt(dev), eth_node, &node_ctrlparam);

	mdio = nc_mdio_open(dev, node_ctrl);
	if (mdio == NULL) {
		warnx("PCS/PMA: Cannot open MDIO for Eth %d", p->index);
		return -1;
	}

	prop32 = fdt_getprop(nfb_get_fdt(dev), node_ctrlparam, "dev", &proplen);
	if (proplen == sizeof(*prop32)) {
		portaddr = fdt32_to_cpu(*prop32);
	} else {
		//warnx("Couldn't find control param property in Device Tree");
	}

	switch (p->command) {
	case CMD_PRINT_SPEED:
		pcspma_print_speed(mdio, portaddr, 1);
		break;
	case CMD_PRINT_STATUS:
		pcspma_print_status(mdio, portaddr, p);
		break;
	case CMD_SET_PMA_TYPE:
		ret = pcspma_set_type(mdio, portaddr, p);
		break;
	case CMD_SET_PMA_FEATURE:
		ret = pcspma_set_feature(mdio, portaddr, p);
		break;
	default:
		warnx("PCS/PMA: Command not implemented");
		break;
	}
	return ret;
}
