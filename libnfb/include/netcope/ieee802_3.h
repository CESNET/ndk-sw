/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - IEEE802.3 registers
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_IEEE802_3_H
#define NETCOPE_IEEE802_3_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <linux/ethtool.h>
#include <netcope/mdio_if_info.h>

#define IEEE802_3_SS_LSB 0x2000
#define IEEE802_3_SS_MSB 0x0040

static inline int ieee802_3_get_pma_speed_value(struct mdio_if_info *if_info)
{
	const int mask = IEEE802_3_SS_MSB | IEEE802_3_SS_LSB;
	int reg;

	reg = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 0);
	if (reg < 0)
		return SPEED_UNKNOWN;

	if ((reg & mask) == mask) {
		switch ((reg >> 2) & 0xF) {
		case 0: return 10000;
		case 1: return 10;
		case 2: return 40000;
		case 3: return 100000;
		case 4: return 25000;
		case 5: return 50000;
		case 8: return 200000;
		case 9: return 400000;
		default: return SPEED_UNKNOWN;
		}
	}

	if (reg & IEEE802_3_SS_MSB)
		return 1000;
	else if (reg & IEEE802_3_SS_LSB)
		return 100;
	else
		return 10;
	return SPEED_UNKNOWN;
}

static inline int ieee802_3_get_pcs_speed_value(struct mdio_if_info *if_info)
{
	const int mask = IEEE802_3_SS_MSB | IEEE802_3_SS_LSB;

	int reg;
	reg = if_info->mdio_read(if_info->dev, if_info->prtad, 3, 0);
	if (reg < 0)
		return SPEED_UNKNOWN;

	if ((reg & mask) == mask) {
		switch ((reg >> 2) & 0xF) {
		case 0: return 10000;
		case 1: return 10;
		case 2: return 1000;
		case 3: return 40000;
		case 4: return 100000;
		case 5: return 25000;
		case 6: return 50000;
		case 9: return 200000;
		case 10: return 400000;
		default: return SPEED_UNKNOWN;
		}
	} else {
		return SPEED_UNKNOWN;
	}
}

#endif /* NETCOPE_IEEE802_3_H */
