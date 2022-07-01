/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - MDIO interface ops
 * Copyright (C) 2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_MDIO_IF_INFO_H
#define NETCOPE_MDIO_IF_INFO_H

#ifdef __KERNEL__
/* Already defined, include appropriate header */
#else

#ifndef MDIO_IF_INFO_PRIV_T_DEFINED
typedef void * mdio_if_info_priv_t;
#endif

typedef int (*mdio_read_t)(mdio_if_info_priv_t dev, int prtad, int devad, uint16_t addr);
typedef int (*mdio_write_t)(mdio_if_info_priv_t dev, int prtad, int devad, uint16_t addr, uint16_t val);

struct mdio_if_info {
	int prtad;
	uint32_t mmds;
	unsigned mode_support;
	mdio_if_info_priv_t dev;
	mdio_read_t mdio_read;
	mdio_write_t mdio_write;
};

#endif

#endif /* NETCOPE_MDIO_IF_INFO_H */
