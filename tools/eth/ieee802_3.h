/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - IEEE 802.3 registers header file
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef IEEE802_3_H
#define IEEE802_3_H

#include <stdint.h>

#ifdef __KERNEL__
/* TODO: include */
#else
struct mdio_if_info {
	int prtad;
	uint32_t mmds;
	unsigned mode_support;
	struct net_device *dev;
	int (*mdio_read)(struct net_device *dev, int prtad, int devad, uint16_t addr);
	int (*mdio_write)(struct net_device *dev, int prtad, int devad, uint16_t addr, uint16_t val);
};

struct net_device {
	struct nc_mdio *mdio;
};
#endif

typedef void (string_cb_t)(void *priv, const char *str);
typedef void (ethtool_cb_t)(void *priv, int ethtool_link_mode, int flags);

int ieee802_3_set_pma_pmd_type_string(struct mdio_if_info *if_info, const char *string);
const char *ieee802_3_get_pma_pmd_type_string(struct mdio_if_info *if_info);

void ieee802_3_get_supported_pma_pmd_types_string(struct mdio_if_info *if_info, string_cb_t cb, void *cb_priv);

const char *ieee802_3_get_pcs_speed_string(struct mdio_if_info *if_info);
const char *ieee802_3_get_pma_speed_string(struct mdio_if_info *if_info);
const char *ieee802_3_get_pcs_pma_link_status_string(struct mdio_if_info *if_info, int devad);

int ieee802_3_get_pcs_lines(struct mdio_if_info *if_info);
int ieee802_3_get_fec_lines(const char *type);

#define IEEE802_3_FLAG_LINES_MASK       (0xFF)

#define IEEE802_3_FLAG_FEC_VARIANT      (1 << 8)
#define IEEE802_3_FLAG_FEC_MANDATORY    (1 << 9)

#endif
