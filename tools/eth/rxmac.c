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

void rxmac_print_status(struct ni_context *ctx, struct nc_rxmac *rxmac, struct eth_params *p)
{
	int ret;
	struct nc_rxmac_status s;
	struct nc_rxmac_counters c;

	ret = nc_rxmac_read_status(rxmac, &s);
	if (ret)
		return;

	ret = nc_rxmac_read_counters(rxmac, &c, NULL);
	if (ret)
		return;

	ni_section(ctx, NI_SEC_RXMAC);

	ni_item_ctrl_reg(ctx, NI_RXM_ENABLED, s.enabled);
	ni_item_ctrl_reg(ctx, NI_RXM_LINK, s.link_up);
	ni_item_ctrl_reg(ctx, NI_RXM_HFIFO_OVF, s.overflow);

	ni_section(ctx, NI_SEC_MAC_S);
	ni_item_uint64_t(ctx, NI_MAC_TOTAL_O, c.cnt_total_octets);
	ni_item_uint64_t(ctx, NI_RXM_PASS_O, c.cnt_octets);
	ni_item_uint64_t(ctx, NI_MAC_TOTAL, c.cnt_total);
	ni_item_uint64_t(ctx, NI_RXM_PASS, c.cnt_received);

	ni_item_uint64_t(ctx, NI_MAC_DROP, c.cnt_drop);
	if (rxmac->has_ext_drop_counters) {
		ni_item_uint64_t(ctx, NI_MAC_DROP_DISABLED, c.cnt_drop_disabled);
		ni_item_uint64_t(ctx, NI_MAC_DROP_FILTERED, c.cnt_drop_filtered);
	}
	ni_item_uint64_t(ctx, NI_RXM_OVERFLOWED, c.cnt_overflowed);
	ni_item_uint64_t(ctx, NI_MAC_DROP_ERR, c.cnt_erroneous);
	if (rxmac->has_ext_drop_counters) {
		ni_item_uint64_t(ctx, NI_MAC_DROP_ERR_LEN, c.cnt_err_length);
		ni_item_uint64_t(ctx, NI_MAC_DROP_ERR_CRC, c.cnt_err_crc);
		ni_item_uint64_t(ctx, NI_MAC_DROP_ERR_MII, c.cnt_err_mii);
	}

	ni_endsection(ctx, NI_SEC_MAC_S);

	if (p->verbose) {
		ni_section(ctx, NI_SEC_RXMAC_CONF);
		ni_item_uint64_tx(ctx, NI_RXM_ERR_MASK_REG, s.error_mask);
		ni_item_ctrl_reg(ctx, NI_RXM_ERR_FRAME, s.error_mask & 0x1);
		ni_item_ctrl_reg(ctx, NI_RXM_ERR_CRC, s.error_mask & 0x2);
		ni_item_ctrl_reg(ctx, NI_RXM_ERR_MIN_LEN, s.error_mask & 0x4);
		ni_item_uint64_t(ctx, NI_RXM_MIN_LEN, s.frame_length_min);
		ni_item_ctrl_reg(ctx, NI_RXM_ERR_MAX_LEN, s.error_mask & 0x8);
		ni_item_uint64_t(ctx, NI_RXM_MAX_LEN, s.frame_length_max);
		if (s.frame_length_max_capable)
			ni_item_uint64_t(ctx, NI_RXM_MAX_LEN_CAP, s.frame_length_max_capable);
		ni_item_ctrl_reg(ctx, NI_RXM_ERR_MAC_CHECK, s.error_mask & 0x10);
		ni_item_ctrl_reg(ctx, NI_RXM_ERR_MAC_MODE, s.mac_filter);
		ni_item_uint64_t(ctx, NI_RXM_MAC_MAX_COUNT, s.mac_addr_count);

		ni_endsection(ctx, NI_SEC_RXMAC_CONF);
	}
	ni_endsection(ctx, NI_SEC_RXMAC);
}

void rxmac_print_ether_stats(struct ni_context *ctx, struct nc_rxmac *rxmac)
{
	int ret;
	struct nc_rxmac_etherstats s;
	ret = nc_rxmac_read_counters(rxmac, NULL, &s);

	if (ret) {
		warnx("Cannot get etherStats from rxmac");
		return;
	}

	ni_section(ctx, NI_SEC_RXMAC_ES);
	ni_item_uint64_t(ctx, NI_RXM_ES_OCTS, s.octets);
	ni_item_uint64_t(ctx, NI_RXM_ES_PKTS, s.pkts);
	ni_item_uint64_t(ctx, NI_RXM_ES_BCST, s.broadcastPkts);
	ni_item_uint64_t(ctx, NI_RXM_ES_MCST, s.multicastPkts);
	ni_item_uint64_t(ctx, NI_RXM_ES_CRCE, s.CRCAlignErrors);
	ni_item_uint64_t(ctx, NI_RXM_ES_UNDR, s.undersizePkts);
	ni_item_uint64_t(ctx, NI_RXM_ES_OVER, s.oversizePkts);
	ni_item_uint64_t(ctx, NI_RXM_ES_FRAG, s.fragments);
	ni_item_uint64_t(ctx, NI_RXM_ES_JABB, s.jabbers);
	ni_item_uint64_t(ctx, NI_RXM_ES_64, s.pkts64Octets);
	ni_item_uint64_t(ctx, NI_RXM_ES_65_127, s.pkts65to127Octets);
	ni_item_uint64_t(ctx, NI_RXM_ES_128_255, s.pkts128to255Octets);
	ni_item_uint64_t(ctx, NI_RXM_ES_256_511, s.pkts256to511Octets);
	ni_item_uint64_t(ctx, NI_RXM_ES_512_1023, s.pkts512to1023Octets);
	ni_item_uint64_t(ctx, NI_RXM_ES_1024_1518, s.pkts1024to1518Octets);
	if (rxmac->has_ext_drop_counters) {
		ni_item_uint64_t(ctx, NI_RXM_ES_1519_2047, s.pkts128to255Octets);
		ni_item_uint64_t(ctx, NI_RXM_ES_2048_4095, s.pkts256to511Octets);
		ni_item_uint64_t(ctx, NI_RXM_ES_4096_8191, s.pkts512to1023Octets);
		ni_item_uint64_t(ctx, NI_RXM_ES_OVER_BINS, s.pktsOverBinsOctets);
	}
	ni_item_uint64_t(ctx, NI_RXM_ES_UNDR_SET, s.underMinPkts);
	ni_item_uint64_t(ctx, NI_RXM_ES_OVER_SET, s.overMaxPkts);
	ni_endsection(ctx, NI_SEC_RXMAC_ES);
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

int rxmac_execute_operation(struct ni_context *ctx, struct nc_rxmac *rxmac, struct eth_params *p)
{
	int ret = 0;

	switch (p->command) {
	case CMD_PRINT_STATUS:
		rxmac_print_status(ctx, rxmac, p);
		if (p->ether_stats)
			rxmac_print_ether_stats(ctx, rxmac);
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
	case CMD_SET_ERROR_MASK:
		nc_rxmac_set_error_mask(rxmac, p->param);
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
