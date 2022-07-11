/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - TX MAC control
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <unistd.h>
#include <stdio.h>

#include <nfb/nfb.h>

#include "eth.h"

#define CNT_FMT "20llu"

void txmac_print_status(struct nc_txmac *txmac, struct eth_params *p __attribute__((unused)) )
{
	int ret;
#if 0
	const char *text;
#endif
	struct nc_txmac_status s;
	struct nc_txmac_counters c;

	ret = nc_txmac_read_status(txmac, &s);
	if (ret)
		return;

	ret = nc_txmac_read_counters(txmac, &c);
	if (ret)
		return;

	printf("------------------------------------- TXMAC Status ----\n");
#if 0
	switch (s.speed) {
		case MAC_SPEED_10G:   text = "10 Gb/s";  break;
		case MAC_SPEED_40G:   text = "40 Gb/s";  break;
		case MAC_SPEED_100G:  text = "100 Gb/s"; break;
		default:              text = "Unknown";  break;
	}
	printf("TXMAC speed                : %s\n", text);
#endif
	printf("TXMAC status               : %s\n", s.enabled ? "ENABLED" : "DISABLED");
	printf("Transmitted octets         : %" CNT_FMT "\n", c.cnt_octets);
	printf("Processed                  : %" CNT_FMT "\n", c.cnt_total);
	printf("Transmitted                : %" CNT_FMT "\n", c.cnt_sent);
	printf("Erroneous                  : %" CNT_FMT "\n", c.cnt_erroneous);
}

int txmac_execute_operation(struct nc_txmac *txmac, struct eth_params *p)
{
	switch (p->command) {
	case CMD_PRINT_STATUS:
		txmac_print_status(txmac, p);
		break;
	case CMD_RESET:
		nc_txmac_reset_counters(txmac);
		break;
	case CMD_ENABLE:
		if (p->param)
			nc_txmac_enable(txmac);
		else
			nc_txmac_disable(txmac);
		break;
	default:
		warnx("TXMAC: Command not implemented");
		break;
	}
	return 0;
}
