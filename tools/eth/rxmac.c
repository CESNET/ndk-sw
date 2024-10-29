/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Ethernet interface configuration tool - RX MAC control
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
#include "eth.h"

#define CNT_FMT "20llu"

void rxmac_print_status(struct nc_rxmac *rxmac, struct eth_params *p)
{
	int ret;
	const char *text = NULL;
	struct nc_rxmac_status s;
	struct nc_rxmac_counters c;

	ret = nc_rxmac_read_status(rxmac, &s);
	if (ret)
		return;

	ret = nc_rxmac_read_counters(rxmac, &c, NULL);
	if (ret)
		return;

	printf("------------------------------------- RXMAC Status ----\n");
#if 0
	switch (s.speed) {
		case MAC_SPEED_10G:   text = "10 Gb/s";  break;
		case MAC_SPEED_40G:   text = "40 Gb/s";  break;
		case MAC_SPEED_100G:  text = "100 Gb/s"; break;
		default:              text = "Unknown";  break;
	}
	printf("RXMAC speed                : %s\n", text);
#endif
	printf("RXMAC status               : %s\n", s.enabled ? "ENABLED" : "DISABLED");
	printf("Link status                : %s\n", s.link_up ? "UP" : "DOWN");
	printf("HFIFO overflow occurred    : %s\n", s.overflow ? "True" : "False");
	printf("Received octets            : %" CNT_FMT "\n", c.cnt_octets);
	printf("Processed                  : %" CNT_FMT "\n", c.cnt_total);
	printf("Received                   : %" CNT_FMT "\n", c.cnt_received);
	printf("Erroneous                  : %" CNT_FMT "\n", c.cnt_erroneous);
	printf("Overflowed                 : %" CNT_FMT "\n", c.cnt_overflowed);

	if (!p->verbose)
		return;

	printf("------------------------------ RXMAC Configuration ----\n");
	printf("Frame error from MII  [1]  : %s", (s.error_mask & 0x00000001) ? "enabled\n" : "disabled\n");
	printf("CRC check             [2]  : %s", (s.error_mask & 0x00000002) ? "enabled\n" : "disabled\n");
	printf("Minimum frame length  [4]  : %s\n"
			"* length                   : %d B\n",
			(s.error_mask & 0x00000004) ? "enabled" : "disabled",
			s.frame_length_min);
	printf("MTU frame length      [8]  : %s\n"
			"* length                   : %d B",
			(s.error_mask & 0x00000008) ? "enabled" : "disabled",
			s.frame_length_max);

	if (s.frame_length_max_capable == 0) {
		printf(" (max unknown)\n");
	} else {
		printf(" (max %d B)\n", s.frame_length_max_capable);
	}

	switch (s.mac_filter) {
	case RXMAC_MAC_FILTER_PROMISCUOUS:	text = "Promiscuous mode"; break;
	case RXMAC_MAC_FILTER_TABLE:		text = "Filter by MAC address table"; break;
	case RXMAC_MAC_FILTER_TABLE_BCAST:	text = "Filter by MAC address table, allow broadcast"; break;
	case RXMAC_MAC_FILTER_TABLE_BCAST_MCAST:text = "Filter by MAC address table, allow broadcast + multicast"; break;
	}
	printf("MAC address check     [16] : %s\n"
			"* mode                     : %s\n",
			(s.error_mask & 0x00000010) ? "enabled" : "disabled", text);
	printf("MAC address table size     : %d\n", s.mac_addr_count);
}

void rxmac_print_ether_stats(struct nc_rxmac *rxmac)
{
	int ret;
	struct nc_rxmac_etherstats s;
	ret = nc_rxmac_read_counters(rxmac, NULL, &s);

	if (ret) {
		warnx("Cannot get etherStats from rxmac");
		return;
	}

	printf("---------------------------- RXMAC etherStatsTable ----\n");
//	printf("etherStatsDropEvents          : %llu\n")
	printf("etherStatsOctets              : %llu\n", s.octets);
	printf("etherStatsPkts                : %llu\n", s.pkts);
	printf("etherStatsBroadcastPkts       : %llu\n", s.broadcastPkts);
	printf("etherStatsMulticastPkts       : %llu\n", s.multicastPkts);
	printf("etherStatsCRCAlignErrors      : %llu\n", s.CRCAlignErrors);
	printf("etherStatsUndersizePkts       : %llu\n", s.undersizePkts);
	printf("etherStatsOversizePkts        : %llu\n", s.oversizePkts);
//	printf("etherStatsOversizePkts        : %llu\n");
	printf("etherStatsFragments           : %llu\n", s.fragments);
	printf("etherStatsJabbers             : %llu\n", s.jabbers);
//	printf("etherStatsCollisions          : %llu\n");
	printf("etherStatsPkts64Octets        : %llu\n", s.pkts64Octets);
	printf("etherStatsPkts65to127Octets   : %llu\n", s.pkts65to127Octets);
	printf("etherStatsPkts128to255Octets  : %llu\n", s.pkts128to255Octets);
	printf("etherStatsPkts256to511Octets  : %llu\n", s.pkts256to511Octets);
	printf("etherStatsPkts512to1023Octets : %llu\n", s.pkts512to1023Octets);
	printf("etherStatsPkts1024to1518Octets: %llu\n", s.pkts1024to1518Octets);
}

int clear_mac_addresses(struct nc_rxmac *rxmac)
{
	unsigned count = nc_rxmac_mac_address_count(rxmac);

	unsigned i;
	bool valid[count];
	unsigned long long mac_addr_list[count];

	for (i = 0; i < count; i++) {
		valid[i] = 0;
		mac_addr_list[i] = 0;
	}

	nc_rxmac_set_mac_list(rxmac, mac_addr_list, valid, count);
	return 0;
}

int fill_mac_addresses(struct nc_rxmac *rxmac)
{
	unsigned count = nc_rxmac_mac_address_count(rxmac);

	int ret;
	unsigned i, j;
	bool valid[count];
	unsigned long long mac_addr_list[count];
	unsigned mv[6];

	for (i = 0; i < count; i++) {
		valid[i] = 0;
		mac_addr_list[i] = 0;
	}

	for (i = 0; i < count; i++) {
		ret = scanf("%02X:%02X:%02X:%02X:%02X:%02X\n",
				&mv[5], &mv[4], &mv[3], &mv[2], &mv[1], &mv[0]);
                if (ret == EOF || ret != 6)
			return -1;

		for (j = 0; j < 6; j++) {
			mac_addr_list[i] <<= 8;
			mac_addr_list[i] |= mv[5-j] & 0xFF;
		}
		valid[i] = 1;
	}
	nc_rxmac_set_mac_list(rxmac, mac_addr_list, valid, count);
	return 0;
}

int show_mac_addresses(struct nc_rxmac *rxmac)
{
	unsigned count = nc_rxmac_mac_address_count(rxmac);

	unsigned i;
	bool valid[count];
	unsigned long long mac_addr_list[count];
	unsigned char *mv;

	if (nc_rxmac_get_mac_list(rxmac, mac_addr_list, valid, count) < 0) {
		warnx("RXMAC: Cannot get MAC addresses");
		return -1;
	}

	for (i = 0; i < count; i++) {
		if (!valid[i])
			continue;
		mv = (unsigned char*) (mac_addr_list + i);
		printf("MAC % 2d: %02X:%02X:%02X:%02X:%02X:%02X\n", i + 1,
				mv[5], mv[4], mv[3], mv[2], mv[1], mv[0]);
	}

	return 0;
}

int remove_mac_address(struct nc_rxmac *rxmac, unsigned long long mac_address)
{
	unsigned count = nc_rxmac_mac_address_count(rxmac);

	unsigned i;
	bool valid[count];
	unsigned long long mac_addr_list[count];

	if (nc_rxmac_get_mac_list(rxmac, mac_addr_list, valid, count) < 0) {
		warnx("RXMAC: Cannot get MAC addresses");
		return -1;
	}

	for (i = 0; i < count; i++) {
		if (!valid[i])
			continue;
		if (mac_addr_list[i] == mac_address) {
			nc_rxmac_set_mac(rxmac, i, mac_address, false);
			return 0;
		}
	}

	return 0;
}

int rxmac_execute_operation(struct nc_rxmac *rxmac, struct eth_params *p)
{
	int ret = 0;

	switch (p->command) {
	case CMD_PRINT_STATUS:
		rxmac_print_status(rxmac, p);
		if (p->ether_stats)
			rxmac_print_ether_stats(rxmac);
		break;
	case CMD_RESET:
		nc_rxmac_reset_counters(rxmac);
		break;
	case CMD_ENABLE:
		if (p->param)
			nc_rxmac_enable(rxmac);
		else
			nc_rxmac_disable(rxmac);
		break;
	case CMD_SET_MAX_LENGTH:
	case CMD_SET_MIN_LENGTH:
		nc_rxmac_set_frame_length(rxmac, p->param,
				p->command == CMD_SET_MAX_LENGTH ? RXMAC_FRAME_LENGTH_MAX : RXMAC_FRAME_LENGTH_MIN);
		break;
	case CMD_SHOW_MACS:
		ret = show_mac_addresses(rxmac);
		break;
	case CMD_CLEAR_MACS:
		ret = clear_mac_addresses(rxmac);
		break;
	case CMD_FILL_MACS:
		ret = fill_mac_addresses(rxmac);
		break;
	case CMD_ADD_MAC:
		if (nc_rxmac_set_mac(rxmac, -1, p->mac_address, 1) < 0)
			ret = -1;
		break;
	case CMD_REMOVE_MAC:
		ret = remove_mac_address(rxmac, p->mac_address);
		break;
	case CMD_MAC_CHECK_MODE:
		nc_rxmac_mac_filter_enable(rxmac, p->param);
		break;
	default:
		warnx("RXMAC: Command not implemented");
		break;
	}
	return ret;
}
