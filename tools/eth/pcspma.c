/* SPDX-License-Identifier: BSD-3-Clause */
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
#include <netcope/ieee802_3.h>

#include "eth.h"
#include "ieee802_3.h"

const char *str_active[2] = {"        ", "[active]"};

struct phy_feature_t {
	const char *name;
	int ability_reg;
	int ability_bit;
	int control_reg;
	int control_bit;
};

static const struct phy_feature_t pma_feature_table [] = {
	{"Reset",                       0, -1,    0, 15},
	{"PMA local loopback",          8,  0,    0,  0},
	{"PMA remote loopback",        13, 15,    0,  1},
	{"Low power",                   1,  1,    0, 11},
	{"25G RS-FEC Enable",           0, -1,  200,  2},
	{NULL,                          0,  0,    0,  0},
};

static const struct phy_feature_t pcs_feature_table [] = {
	{"PCS reverse loopback",    16385,  0, 16384, 0},
	{NULL,                          0,  0,     0, 0},
};

void pcspma_print_speed(struct nc_mdio *mdio, int portaddr, uint8_t mdev)
{
	struct mdio_if_info mdio_info = nfb_eth_create_mdio_info(mdio, portaddr);
	printf("Speed                      : %s\n",
		mdev == 1 ? ieee802_3_get_pma_speed_string(&mdio_info) : ieee802_3_get_pcs_speed_string(&mdio_info));
}

void print_pcspma_common(struct nc_mdio *mdio, int portaddr, uint8_t mdev)
{
	uint32_t reg;
	struct mdio_if_info mdio_info = nfb_eth_create_mdio_info(mdio, portaddr);

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
	struct mdio_if_info mdio_info = nfb_eth_create_mdio_info(mdio, portaddr);
	return ieee802_3_set_pma_pmd_type_string(&mdio_info, p->string);
}

int pcspma_set_feature(struct nc_mdio *mdio, int portaddr, struct eth_params *p)
{
	uint16_t reg;

	const struct phy_feature_t *item     = pma_feature_table;
	const struct phy_feature_t *pcs_item = pcs_feature_table;

	while (item->name) {
		if (!strcmp(p->string, item->name)) {
			reg = nc_mdio_read(mdio, portaddr, 1, item->control_reg);
			reg = p->param ? (reg | (1 << item->control_bit)) : (reg & ~(1 << item->control_bit));
			nc_mdio_write(mdio, portaddr, 1, item->control_reg, reg);
			return 0;
		}
		item++;
	}
	while (pcs_item->name) {
		if (!strcmp(p->string, pcs_item->name)) {
			reg = nc_mdio_read(mdio, portaddr, 3, pcs_item->control_reg);
			reg = p->param ? (reg | (1 << pcs_item->control_bit)) : (reg & ~(1 << pcs_item->control_bit));
			nc_mdio_write(mdio, portaddr, 3, pcs_item->control_reg, reg);
			return 0;
		}
		pcs_item++;
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

	int lines, fec_lines = 0;
	uint32_t low_bits;
	int i, j;

	const char *active;

	struct mdio_if_info mdio_info = nfb_eth_create_mdio_info(mdio, portaddr);
	int pma_speed = ieee802_3_get_pma_speed_value(&mdio_info);

	//printf("--------------------------------------- PMA %d regs ----\n", p->index);
	printf("----------------------------------------- PMA regs ----\n");
	print_pcspma_common(mdio, portaddr, 1);
	reg = nc_mdio_read(mdio, portaddr, 1, 7);

	active = ieee802_3_get_pma_pmd_type_string(&mdio_info);
	printf("PMA type                   : %s\n", active);

	if (p->verbose) {
		struct cb_print_pma_pmd_type_priv cb_ppptp;
		const struct phy_feature_t *item;

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
		fec_lines = ieee802_3_get_fec_lines(active);
		if ((fec_lines > 0) && (pma_speed < 200000)) { /* RS-FEC Clause 91, 108 or 134 - RSFEC reg located in 1.200 - 1.300 */
			reg = nc_mdio_read_dword(mdio, portaddr, 1, 201);
			printf("RS-FEC status              : %s%s%s\n",
//					reg & (1 <<  0) ? "FEC bypass corection ability | " : "",
//					reg & (1 <<  1) ? "FEC bypass indication ability | " : "",
					reg & (1 <<  2) ? "RS-FEC high SER | " : "",
					reg & (1 << 14) ? "RS-FEC lanes aligned | " : "",
					reg & (1 << 15) ? "PCS lanes aligned | " : "");
			printf("RS-FEC corrected cws       : %u\n", nc_mdio_read_dword(mdio, portaddr, 1, 202));
			printf("RS-FEC uncorrected cws     : %u\n", nc_mdio_read_dword(mdio, portaddr, 1, 204));

			printf("RS-FEC symbol errors ->\n");
			for (i = 0; i < fec_lines; i++) {
				printf(" * Lane %d                  : %u\n", i, nc_mdio_read_dword(mdio, portaddr, 1, 210 + i * 2));
			}
			printf("RS-FEC lane mapping        :");
			reg = nc_mdio_read(mdio, portaddr, 1, 206);
			for (i = 0; i < fec_lines; i++) {
				printf(" %d", ((reg >> (i*2)) & 0x3));
			}
			printf("\n");

			reg = nc_mdio_read(mdio, portaddr, 1, 201);
			printf("RS-FEC AM lock             :");
			for (i = 0; i < fec_lines; i++) {
				printf((reg & (1 << (i + 8))) ? " L" : " X");
			}
			printf("\n");
		}
		/* RS-FEC integrated in PCS clause 119 (not bypassable) */
		if (pma_speed >= 200000) {
			reg = nc_mdio_read_dword(mdio, portaddr, 3, 801);
			printf("RS-FEC status              : %s%s%s%s\n",
					reg & (1 <<  2) ? "RS-FEC high SER | " : "",
					reg & (1 <<  4) ? "RS-FEC degraded SER | " : "",
					reg & (1 <<  5) ? "Remote degraded SER | " : "",
					reg & (1 <<  6) ? "Local degraded SER" : "");
			printf("RS-FEC corrected cws       : %u\n", nc_mdio_read_dword(mdio, portaddr, 3, 802));
			printf("RS-FEC uncorrected cws     : %u\n", nc_mdio_read_dword(mdio, portaddr, 3, 804));

			printf("RS-FEC symbol errors ->\n");
			for (i = 0; i < fec_lines; i++) {
				printf(" * Lane %d                  : %u\n", i, nc_mdio_read_dword(mdio, portaddr, 3, 600 + i * 2));
			}
		}
	}

	//printf("--------------------------------------- PCS %d regs ----\n", p->index);
	printf("----------------------------------------- PCS regs ----\n");
	print_pcspma_common(mdio, portaddr, 3);

	if (p->verbose) {
		const struct phy_feature_t *item;
		printf("Supported PCS features ->\n");

		for (item = pcs_feature_table; item->name; item++) {
			if (item->ability_bit != -1) {
				reg = nc_mdio_read(mdio, portaddr, 3, item->ability_reg);
			}

			if (reg & (1 << item->ability_bit) || p->verbose > 1) {
				reg = nc_mdio_read(mdio, portaddr, 3, item->control_reg);
				printf(" * %s                : %s\n", str_active[reg & (1 << item->control_bit) ? 1 : 0], item->name);
			}
		}

		// PCS status reg 1 -> 32
		reg = nc_mdio_read(mdio, portaddr, 3, 32);
		// PCS status reg 2 -> 33
		reg2 = nc_mdio_read(mdio, portaddr, 3, 33);
		if (pma_speed <= 100000) /* Block lock not defined above 100G */
			printf("Global Block Lock          : %s | %s\n",
				(reg & 0x0001) ? "Yes" : "No",
				(reg2 & 0x8000) ? "Yes" : "No");
		printf("Global High BER            : %s | %s\n",
			(reg & 0x0002) ? "Yes" : "No",
			(reg2 & 0x4000) ? "Yes":"No");
		// BER counter register -> 44
		reg = nc_mdio_read(mdio, portaddr, 3, 44);
		low_bits = ((reg2 >> 8) & 0x003F);
		printf("BER counter                : %u\n",
			((reg & 0xFFFF) << 6) | low_bits);
		low_bits = (reg2 & 0x00FF);
		// Error blocks register -> 45
		reg = nc_mdio_read(mdio, portaddr, 3, 45);
		printf("Errored blocks             : %u\n",
			((reg & 0x3FFF) << 8) | low_bits);

		lines = ieee802_3_get_pcs_lines(&mdio_info);
		if (lines > 1) {
			reg = nc_mdio_read(mdio, portaddr, 3, 50);
			printf("PCS lanes aligned          : %s\n",
				(reg & 0x1000) ? "Yes" : "No");
			printf("\nBlock status for lines     :");
			// Block status register first half (8 lines)   -> 50
			//                       second half (12 lines) -> 51
			//                       total 20 lines (max for 100G)
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
			if (!nc_mdio_pcs_lane_map_valid(mdio)) {
				// Intel PCS/PMAs are not reporting PCS lane mapping when the RS-FEC is active
				for (i = 0; i < lines; i++) {
					printf("  U");
				}
			} else {
				// Lane mapping register for each line starting from -> 400 to 420
				for (i = 0; i < lines; i++) {
					printf(" %2d", nc_mdio_read(mdio, portaddr, 3, 400 + i) & 0x3F);
				}
			}

			// BIP counters not defined for speeds above 100
			if (pma_speed <= 100000) {
				printf("\nBIP error counter\n");
				// BIP counter register for each line starting from -> 200 to 220
				for (i = 0; i < lines; i++) {
					if (i == 10)
						printf("\n");
					printf(" %5d", nc_mdio_read(mdio, portaddr, 3, 200 + i));
				}
			}
			printf("\n");
		}
	}
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

	mdio = nc_mdio_open(dev, node_ctrl, node_ctrlparam);
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
		if (!strcmp(p->string, "Software TX disable")) {
			ret = transceiver_execute_operation_for_eth(dev, eth_node, p);
		} else {
			ret = pcspma_set_feature(mdio, portaddr, p);
		}
		break;
	default:
		warnx("PCS/PMA: Command not implemented");
		break;
	}

	nc_mdio_close(mdio);

	return ret;
}
