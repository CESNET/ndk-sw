/* SPDX-License-Identifier: BSD-3-Clause */
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

void txmac_print_status(struct ni_context *ctx, struct nc_txmac *txmac, struct eth_params *p __attribute__((unused)) )
{
	int ret;
	struct nc_txmac_status s;
	struct nc_txmac_counters c;

	ret = nc_txmac_read_status(txmac, &s);
	if (ret)
		return;

	ret = nc_txmac_read_counters(txmac, &c);
	if (ret)
		return;

	ni_section(ctx, NI_SEC_TXMAC);
	ni_item_ctrl_reg(ctx, NI_TXM_ENABLED, s.enabled);
	ni_section(ctx, NI_SEC_TXMAC_S);
	ni_item_uint64_t(ctx, NI_TXM_SENT_O, c.cnt_octets);
	ni_item_uint64_t(ctx, NI_TXM_PROCESSED, c.cnt_total);
	ni_item_uint64_t(ctx, NI_TXM_SENT, c.cnt_sent);
	ni_item_uint64_t(ctx, NI_TXM_ERRONEOUS, c.cnt_erroneous);
	ni_endsection(ctx, NI_SEC_TXMAC_S);
	ni_endsection(ctx, NI_SEC_TXMAC);
}

int txmac_execute_operation(struct ni_context *ctx, struct nc_txmac *txmac, struct eth_params *p)
{
	switch (p->command) {
	case CMD_PRINT_STATUS:
		txmac_print_status(ctx, txmac, p);
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
