/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Ethernet interface configuration tool - private header
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFBTOOL_ETH_H
#define NFBTOOL_ETH_H

#include <stdbool.h>

#include <nfb/boot.h>
#include <netcope/rxmac.h>
#include <netcope/txmac.h>

#include "mdio.h"

// this enum need to corespond with queries[] array
enum queries {
	RX_STATUS,
	RX_OCTETS,
	RX_PROCESSED,
	RX_ERRONEOUS,
	RX_LINK,
	RX_RECEIVED,
	RX_OVERFLOWED,
	TX_STATUS,
	TX_OCTETS,
	TX_PROCESSED,
	TX_ERRONEOUS,
	TX_TRANSMITTED,
	PMA_TYPE,
	PMA_SPEED,
};
static const char * const queries[] = {
	"rx_status",
	"rx_octets",
	"rx_processed",
	"rx_erroneous",
	"rx_link",
	"rx_received",
	"rx_overflowed",
	"tx_status",
	"tx_octets",
	"tx_processed",
	"tx_erroneous",
	"tx_transmitted",
	"pma_type",
	"pma_speed",
};

enum commands {
	CMD_PRINT_STATUS,
	CMD_PRINT_SPEED,
	CMD_USAGE,
	CMD_ENABLE,
	CMD_RESET,
	CMD_SET_ERROR_MASK,
	CMD_SET_PMA_TYPE,
	CMD_SET_PMA_FEATURE,
	CMD_SET_MAX_LENGTH,
	CMD_SET_MIN_LENGTH,
	CMD_SET_REPEATER,
	CMD_MAC_CHECK_MODE,
	CMD_SHOW_MACS,
	CMD_CLEAR_MACS,
	CMD_FILL_MACS,
	CMD_ADD_MAC,
	CMD_REMOVE_MAC,
	CMD_QUERY,
};

struct eth_params {
	enum commands command;
	long param;
	int index;
	int verbose;
	bool ether_stats;
	unsigned long long mac_address;
	const char *string;
};

int rxmac_execute_operation(struct nc_rxmac *rxmac, struct eth_params *p);
int txmac_execute_operation(struct nc_txmac *txmac, struct eth_params *p);
int pcspma_execute_operation(struct nfb_device *dev, int eth_node, struct eth_params *p);
int transceiver_execute_operation(struct nfb_device *dev, int node_transceiver, struct eth_params *p);
int transceiver_execute_operation_for_eth(struct nfb_device *dev, int node_eth, struct eth_params *p);
int transceiver_print(struct nfb_device *dev, int transceiver_node, int index);
void transceiver_print_short_info(struct nfb_device *dev, int node, struct eth_params *p);

int query_print(const void *fdt, int node, char *queries, int size,
	struct nfb_device *dev, int index);

static inline struct mdio_if_info nfb_eth_create_mdio_info(struct nc_mdio *mdio, int port_address)
{
	return (struct mdio_if_info) {
		.mdio_read = (mdio_read_t) nc_mdio_read,
		.mdio_write = (mdio_write_t) nc_mdio_write,
		.dev = (mdio_if_info_priv_t) mdio,
		.prtad = port_address,
	};
}

#endif /* NFBTOOL_ETH_H */
