/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - transceiver control
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
#include <netcope/i2c_ctrl.h>
#include <netcope/transceiver.h>

#include "eth.h"

void qsfpp_print(struct nfb_device *dev, int nodeoffset, int control_params_node);
void cfp2_print(struct nfb_device *dev, int nodeoffset, int control_params_node);

int qsfpp_stxdisable(struct nfb_device *dev, int nodeoffset, int node_params, int disable, int channels);

typedef void transc_print_t(struct nfb_device *dev, int nodeoffset, int control_params_node);
typedef int transc_plug_t(struct nfb_device *dev, int nodeoffset);

struct transceiver_t {
	transc_print_t *print_status;
	const char *type;
};

/* printing method for transceiver, based on TYPE of transceiver*/
static const struct transceiver_t transc_printing[] = {
	{ &qsfpp_print, "QSFP"   },
	{ &qsfpp_print, "QSFP28" },
	{ &cfp2_print,  "CFP2"   },
	{ &cfp2_print,  "CFP4"   },
	{ NULL },
};

int transceiver_is_present(struct nfb_device *dev, int node)
{
	int ret;
	int node_statusreg;

	struct nfb_comp *comp_status;

	node_statusreg = fdt_node_offset_by_phandle_ref(nfb_get_fdt(dev), node, "status-reg");
	comp_status = nfb_comp_open(dev, node_statusreg);
	if (comp_status == NULL)
		return -1;

	ret = nc_transceiver_statusreg_is_present(comp_status);
	nfb_comp_close(comp_status);
	return ret;
}

void transceiver_print_short_info(struct nfb_device *dev, int node, struct eth_params *p)
{
	int node_transceiver_by_phandle;
	int node_transceiver;
	int node_params;
	int index;
	int present;

	const void *fdt;

	const fdt32_t* prop32;
	const char *prop;
	int proplen;

	fdt = nfb_get_fdt(dev);
	node_transceiver_by_phandle = fdt_node_offset_by_phandle_ref(fdt, node, "pmd");

	index = 0;
	fdt_for_each_compatible_node(fdt, node_transceiver, "netcope,transceiver") {
		if (node_transceiver == node_transceiver_by_phandle) {
			present = transceiver_is_present(dev, node_transceiver);

			prop = fdt_getprop(fdt, node_transceiver, "type", &proplen);
			if (proplen < 0)
				prop = "Unknown";

			printf("Transceiver status         : %s\n",
					(present < 0 ? "Unknown" :
					(present > 0 ? "OK" : "Not plugged")));

			printf("Transceiver cage           : %s-%d\n", prop, index);
			node_params = fdt_subnode_offset(fdt, node, "pmd-params");
			if (node_params >= 0 && p->verbose) {
				prop32 = fdt_getprop(fdt, node_params, "lines", &proplen);
				if (prop32) {
					printf("Transceiver lane(s)        : ");
					while (proplen > 0) {
						printf("%d", fdt32_to_cpu(*prop32));
						proplen -= sizeof(*prop32);
						prop32++;
						if (proplen)
							printf("|");
					}
					printf("\n");
				}
			}
			break;
		}
		index++;
	}
}

int transceiver_print(struct nfb_device *dev, int node_transceiver, int index)
{
	const struct transceiver_t *transceiver;
	const char *property;
	int proplen;
	int present;
	const void *fdt;
	unsigned i;

	fdt = nfb_get_fdt(dev);

	property = fdt_getprop(fdt, node_transceiver, "type", &proplen);
	if (proplen < 0)
		property = "Unknown";

	transceiver = transc_printing;
	while (transceiver->type) {
		if (!strcmp(transceiver->type, (char *)property))
			break;
		transceiver++;
	}

	for (i = 0; i < 47 - strlen(property); i++)
	       printf("-");
	printf(" %s-%d ----\n", property, index++);

	present = transceiver_is_present(dev, node_transceiver);
	printf("Transceiver status         : %s\n",
			(present < 0 ? "Unknown" :
			(present > 0 ? "OK" : "Not plugged")));

	if (transceiver->type == NULL || present == 0)
		return -ENODEV;

	if (!transceiver->print_status) {
		warnx("wrong or unsupported transceiver for accessing PMD in \
			Device Tree description of design");
		return -EOPNOTSUPP;
	}

	transceiver->print_status(dev, node_transceiver, fdt_subnode_offset(fdt, node_transceiver, "control-param"));
	return 0;
}

int _transceiver_execute_operation(struct nfb_device *dev, int node_transceiver, struct eth_params *p, int eth_node)
{
	int ret;
	int present;
	uint8_t ch;

	int proplen;
	int node_params;
	const void *fdt;
	const fdt32_t *prop32;
	const char *property;

	fdt = nfb_get_fdt(dev);

	property = fdt_getprop(fdt, node_transceiver, "type", &proplen);
	if (proplen < 0)
		property = "Unknown";

	present = transceiver_is_present(dev, node_transceiver);
	if (!present)
		return 0;

	switch (p->command) {
	case CMD_SET_PMA_FEATURE:
		if (!strcmp(p->string, "Software TX disable")) {
			if (strcmp((char *)property, "QSFP") != 0 && strcmp((char *)property, "QSFP28") != 0) {
				warnx("Transceiver: Command not implemented");
				return -EOPNOTSUPP;
			}

			if (eth_node >= 0) {
				node_params = fdt_subnode_offset(fdt, eth_node, "pmd-params");
				if (node_params < 0) {
					warnx("Transceiver: No pmd-params node in Device Tree");
					return -1;
				}

				prop32 = fdt_getprop(fdt, node_params, "lines", &proplen);
				if (!prop32) {
					warnx("Transceiver: No lines property in Device Tree");
					return -1;
				}

				ch = 0x0;
				while (proplen > 0) {
					ch |= (1 << fdt32_to_cpu(*prop32));
					proplen -= sizeof(*prop32);
					prop32++;
				}
			} else {
				/* bitmask of all 4 QSFP channels */
				ch = 0x0F;
			}

			ret = qsfpp_stxdisable(dev, node_transceiver, fdt_subnode_offset(fdt, node_transceiver, "control-param"), p->param, ch);
			if (ret) {
				warnx("Transceiver: Command failed");
				return ret;
			}
		} else {
			warnx("Transceiver: Command not implemented");
		}
		break;

	default:
		warnx("Transceiver: Command not implemented");
		return -EOPNOTSUPP;
	}
	return 0;
}

int transceiver_execute_operation_for_eth(struct nfb_device *dev, int node_eth, struct eth_params *p)
{
	int node_transceiver_by_phandle;
	node_transceiver_by_phandle = fdt_node_offset_by_phandle_ref(nfb_get_fdt(dev), node_eth, "pmd");

	return _transceiver_execute_operation(dev, node_transceiver_by_phandle, p, node_eth);
}

int transceiver_execute_operation(struct nfb_device *dev, int node_transceiver, struct eth_params *p)
{
	return _transceiver_execute_operation(dev, node_transceiver, p, -1);
}
