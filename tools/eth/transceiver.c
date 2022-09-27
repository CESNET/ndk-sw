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

int transceivers_print(struct nfb_device *dev)
{
	const struct transceiver_t *transceiver;
	const char *property;
	int proplen;
	int node_transceiver;
	int present;
	const void *fdt;
	int index = 0;
	unsigned i;

	fdt = nfb_get_fdt(dev);

	fdt_for_each_compatible_node(fdt, node_transceiver, "netcope,transceiver") {
		property = fdt_getprop(fdt, node_transceiver, "type", &proplen);
		if (proplen < 0)
			property = "Unknown";

		transceiver = transc_printing;
		while (transceiver->type) {
			if (!strcmp(transceiver->type, (char *)property))
				break;
			transceiver++;
		}

		if (index)
			printf("\n");

		for (i = 0; i < 47 - strlen(property); i++)
                       printf("-");
		printf(" %s-%d ----\n", property, index++);

		present = transceiver_is_present(dev, node_transceiver);
		printf("Transceiver status         : %s\n",
				(present < 0 ? "Unknown" :
				(present > 0 ? "OK" : "Not plugged")));

		if (transceiver->type == NULL || present == 0)
			continue;

		if (!transceiver->print_status) {
			warnx("wrong or unsupported transceiver for accessing PMD in \
				Device Tree description of design");
			return -1;
		}

		transceiver->print_status(dev, node_transceiver, fdt_subnode_offset(fdt, node_transceiver, "control-param"));
	}
	return 0;
}
