/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - general MDIO access
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Peresini <xperes00@stud.fit.vutbr.cz>
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_MDIO_H
#define NETCOPE_MDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ INCLUDES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#include "mdio_dmap.h"
#include "mdio_ctrl.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_mdio {
	int (*mdio_read)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr);
	int (*mdio_write)(struct nfb_comp *dev, int prtad, int devad, uint16_t addr, uint16_t val);
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_mdio *nc_mdio_open (const struct nfb_device *dev, int fdt_offset);
static inline void         nc_mdio_close(struct nc_mdio *mdio);
static inline int          nc_mdio_read (struct nc_mdio *mdio, int prtad, int devad, uint16_t addr);
static inline int          nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_mdio *nc_mdio_open(const struct nfb_device *dev, int fdt_offset)
{
	struct nc_mdio *mdio;
	struct nfb_comp *comp;

	comp = nc_mdio_ctrl_open_ext(dev, fdt_offset, sizeof(struct nc_mdio));
	if (comp) {
		mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
		mdio->mdio_read = nc_mdio_ctrl_read;
		mdio->mdio_write = nc_mdio_ctrl_write;
		return mdio;
	}

	comp = nc_mdio_dmap_open_ext(dev, fdt_offset, sizeof(struct nc_mdio));
	if (comp) {
		mdio = (struct nc_mdio *) nfb_comp_to_user(comp);
		mdio->mdio_read = nc_mdio_dmap_read;
		mdio->mdio_write = nc_mdio_dmap_write;
		return mdio;
	}

	return NULL;
}

static inline void nc_mdio_close(struct nc_mdio *m)
{
	nfb_comp_close(nfb_user_to_comp(m));
}

static inline int nc_mdio_read(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	return mdio->mdio_read(comp, prtad, devad, addr);
}

static inline int nc_mdio_write(struct nc_mdio *mdio, int prtad, int devad, uint16_t addr, uint16_t val)
{
	struct nfb_comp *comp = nfb_user_to_comp(mdio);
	return mdio->mdio_write(comp, prtad, devad, addr, val);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MDIO_H */
