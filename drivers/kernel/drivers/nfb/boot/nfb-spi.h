/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Boot driver module header for Intel M10 BMC via SPI core.
 * Support for Silicom N5014.
 *
 * Copyright (C) 2024 BrnoLogic
 * Author(s):
 *   Vlastimil Kosar <kosar@brnologic.com>
 */

#include <linux/mfd/nfb-intel-m10-bmc.h>

#include "nfb-common.h"

#ifndef NFB_BOOT_SPI_H
#define NFB_BOOT_SPI_H

struct nfb_boot;

int nfb_spi_init(void);
void nfb_spi_exit(void);
int nfb_spi_attach(struct nfb_boot *boot);
void nfb_spi_detach(struct nfb_boot *boot);

struct m10bmc_sec;

struct m10bmc_spi_nfb_device {
	struct intel_m10bmc m10bmc;
	struct nfb_comp * comp;

	struct platform_device *pd;

	struct fpga_image_load *imgld;
	struct image_load* image_load;

	struct m10bmc_sec *sec;
    
	struct spi_controller *host;
};

#endif
