/* SPDX-License-Identifier: GPL-2.0 */
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

#include <netcope/rxmac.h>
#include <netcope/txmac.h>

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
	CMD_SET_MASK,
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
int transceivers_print(struct nfb_device *dev);
void transciever_print_short_info(struct nfb_device *dev, int node, struct eth_params *p);

int query_print(const void *fdt, int node, char *queries, int size,
	struct nfb_device *dev, int index);

struct net_device;
struct mdio_if_info;

int mdio_read(struct net_device *dev, int prtad, int devad, uint16_t addr);
int mdio_write(struct net_device *dev, int prtad, int devad, uint16_t addr, uint16_t val);

#define create_mdio_if_info(name, device, port_address) \
	struct net_device _ ##name## _mdio_if_info_dev = {.mdio = device}; \
	struct mdio_if_info name = { \
		.mdio_read = mdio_read, \
		.mdio_write = mdio_write, \
		.dev = &_ ##name## _mdio_if_info_dev, \
		.prtad = port_address, \
	};

#endif /* NFBTOOL_ETH_H */
