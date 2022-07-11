/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - query interface
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <unistd.h>
#include <stdio.h>

#include <nfb/nfb.h>
#include <libfdt.h>

#include <netcope/eth.h>
#include <netcope/idcomp.h>
#include <netcope/nccommon.h>
#include <netcope/mdio.h>

#include "ieee802_3.h"
#include "eth.h"

int query_print(const void *fdt, int node, char *queries, int size,
	struct nfb_device *dev, int index)
{
	int fdt_offset;
	int node_ctrlparam;
	int proplen;
	const fdt32_t *prop32;
	struct nc_mdio *mdio;
	int portaddr = 0;
	int ret;
	// RXMAC
	struct nc_rxmac *rxmac;
	struct nc_rxmac_status sr;
	struct nc_rxmac_counters cr;
	// TXMAC
	struct nc_txmac *txmac;
	struct nc_txmac_status st;
	struct nc_txmac_counters ct;
	// open RXMAC
	fdt_offset = nc_eth_get_rxmac_node(fdt, node);
	rxmac = nc_rxmac_open(dev, fdt_offset);
	if (!rxmac) {
		warnx("Cannot open RXMAC for ETH%d", index);
	}
	ret = nc_rxmac_read_status(rxmac, &sr);
	if (ret)
		return ret;
	ret = nc_rxmac_read_counters(rxmac, &cr, NULL);
	if (ret)
		return ret;
	nc_rxmac_close(rxmac);
	// open TXMAC
	fdt_offset = nc_eth_get_txmac_node(fdt, node);
	txmac = nc_txmac_open(dev, fdt_offset);
	if (!txmac) {
		warnx("Cannot open TXMAC for ETH%d", index);
	}
	ret = nc_txmac_read_status(txmac, &st);
	if (ret)
		return ret;
	ret = nc_txmac_read_counters(txmac, &ct);
	if (ret)
		return ret;
	nc_txmac_close(txmac);

	fdt_offset = nc_eth_get_pcspma_control_node(fdt, node, &node_ctrlparam);
	mdio = nc_mdio_open(dev, fdt_offset);
	if (mdio == NULL) {
		warnx("PCS/PMA: Cannot open MDIO for Eth %d", index);
		return -1;
	}
	prop32 = fdt_getprop(fdt, node_ctrlparam, "dev", &proplen);
	if (proplen == sizeof(*prop32)) {
		portaddr = fdt32_to_cpu(*prop32);
	} else {
		// warnx("Couldn't find control param property in Device Tree");
		// return -1;
	}

	for (int i = 0; i < size; ++i) {
		switch (queries[i]) {
		case RX_STATUS:
			printf("%s\n", sr.enabled ? "ENABLED" : "DISABLED"); break;
		case RX_OCTETS:
			printf("%llu\n", cr.cnt_octets); break;
		case RX_PROCESSED:
			printf("%llu\n", cr.cnt_total); break;
		case RX_ERRONEOUS:
			printf("%llu\n", cr.cnt_erroneous); break;
		case RX_LINK:
			printf("%s\n", sr.link_up ? "UP" : "DOWN"); break;
		case RX_RECEIVED:
			printf("%llu\n", cr.cnt_received); break;
		case RX_OVERFLOWED:
			printf("%llu\n", cr.cnt_overflowed); break;
		case TX_STATUS:
			printf("%s\n", st.enabled ? "ENABLED" : "DISABLED"); break;
		case TX_OCTETS:
			printf("%llu\n", ct.cnt_octets); break;
		case TX_PROCESSED:
			printf("%llu\n", ct.cnt_total); break;
		case TX_ERRONEOUS:
			printf("%llu\n", ct.cnt_erroneous); break;
		case TX_TRANSMITTED:
			printf("%llu\n", ct.cnt_sent); break;
		case PMA_TYPE: {
				struct mdio_if_info mdio_info = nfb_eth_create_mdio_info(mdio, portaddr);
				printf("%s\n", ieee802_3_get_pma_pmd_type_string(&mdio_info));
				break;
			}
		case PMA_SPEED: {
				struct mdio_if_info mdio_info = nfb_eth_create_mdio_info(mdio, portaddr);
				printf("%s\n", ieee802_3_get_pma_speed_string(&mdio_info));
				break;
			}
		default:
			break;
		}
	}

	nc_mdio_close(mdio);
	return 0;
}
