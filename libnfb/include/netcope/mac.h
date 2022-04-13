/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - MAC common
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_MAC_H
#define NETCOPE_MAC_H

#ifdef __cplusplus
extern "C" {
#endif


enum nc_mac_speed {
	MAC_SPEED_UNKNOWN = 0x0,
	MAC_SPEED_10G = 0x3,
	MAC_SPEED_40G = 0x4,
	MAC_SPEED_100G = 0x5
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MAC_H */

