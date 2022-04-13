/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - directly mapped MDIO component
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Peresini <xperes00@stud.fit.vutbr.cz>
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_MDIODMAP_H
#define NETCOPE_MDIODMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ DEFINES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define COMP_NETCOPE_DMAP "netcope,pcsregs"

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nfb_comp *nc_mdio_dmap_open_ext(const struct nfb_device *dev,
		int fdt_offset, int user_size);
static inline int nc_mdio_dmap_read(struct nfb_comp *comp, int prtad, int devad, uint16_t addr);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nfb_comp *nc_mdio_dmap_open_ext(const struct nfb_device *dev,
		int fdt_offset, int user_size)
{
	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_DMAP))
		return NULL;

	return nfb_comp_open_ext(dev, fdt_offset, user_size);
}

static inline int nc_mdio_dmap_read(struct nfb_comp *comp, int prtad __attribute__((unused)), int devad, uint16_t addr)
{
	uint32_t val = 0;
	uint32_t offset = devad * 0x10000 + addr *2;

	/* HW bug: we have only DEV 1, 2, 3 */
	if (devad > 0 && devad < 4)
		val = nfb_comp_read16(comp, offset);

	return val;
}

static inline int nc_mdio_dmap_write(struct nfb_comp *comp, int prtad __attribute__((unused)), int devad, uint16_t addr, uint16_t val)
{
	uint32_t offset = devad * 0x10000 + addr *2;

	/* HW bug: we have only DEV 1, 2, 3 */
	if (devad > 0 && devad < 4)
		nfb_comp_write16(comp, offset, val);

	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MDIODMAP_H */
