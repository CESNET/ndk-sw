/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Boot driver module header for Intel M10 BMC via SPI core.
 * Support for Silicom N5014.
 *
 * Copyright (C) 2024 BrnoLogic
 * Author(s):
 *   Vlastimil Kosar <kosar@brnologic.com>
 */

#ifndef NFB_BOOT_COMMON_H
#define NFB_BOOT_COMMON_H

struct m10bmc_sec;

struct image_load {
	const char *name;
	int (*load_image)(struct m10bmc_sec *sec);
};

#endif
