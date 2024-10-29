/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * QDR driver module of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#include <linux/pci.h>
#include <libfdt.h>

#include "../nfb.h"

#include <netcope/qdr.h>

#include "qdr.h"


int nfb_qdr_attach(struct nfb_device *nfb, void **priv)
{
	int ret = 0;
	int index;
	int fdt_offset;

	struct nc_qdr *qdr;

	index = 0;
	fdt_for_each_compatible_node(nfb->fdt, fdt_offset, COMP_NETCOPE_QDR) {

		qdr = nc_qdr_open(nfb, fdt_offset);
		if (qdr) {
		 	if (!nc_qdr_get_ready(qdr)) {
		 		nc_qdr_start(qdr);
		 	}

		 	nc_qdr_close(qdr);
		}

		index++;
	}

	dev_info(&nfb->pci->dev, "nfb_qdr: Attached successfully (%d QDR controllers)\n", index);
	return ret;
}

void nfb_qdr_detach(struct nfb_device *nfb, void *priv)
{

}
