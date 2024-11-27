/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver module header for Intel M10 BMC
 *
 * Copyright (C) 2023 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/mfd/nfb-intel-m10-bmc.h>

#include "nfb-common.h"

#ifndef NFB_BOOT_PMCI_H
#define NFB_BOOT_PMCI_H

struct nfb_boot;

int nfb_pmci_init(void);
void nfb_pmci_exit(void);
int nfb_pmci_attach(struct nfb_boot *boot);
void nfb_pmci_detach(struct nfb_boot *boot);

struct m10bmc_sec;

struct pmci_device {
	struct intel_m10bmc m10bmc;
	struct nfb_comp * comp;

	struct fpga_flash_ops flash_ops;
	struct platform_device *pd;

	struct fpga_image_load *imgld;
	struct image_load* image_load;

	struct m10bmc_sec *sec;
};

#endif
