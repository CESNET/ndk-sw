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

static inline void __nc_mdio_dmap_drp_access(struct nfb_comp *comp, int prtad __attribute__((unused)), uint32_t page, uint32_t addr, uint32_t *data, int rw)
{
#define DRP_BUSY_BIT	(1 << 31)

	/* Vendor specific registers for DRP (dev 1 is on MI offset 0x10000) */
	/* INFO: DMAP doesn't have enough dataspace, VS registers moved from 32768 to 16384 */
	const int VS = 0x10000 + (16384 << 1);

	/* 32b registers */
	const int VS_DRP_DATA = VS + 0x10;
	const int VS_DRP_ADDR = VS + 0x14;
	const int VS_DRP_CTRL = VS + 0x18;
	uint32_t drpstat;
	int retries = 0;

	nfb_comp_write32(comp, VS_DRP_ADDR, addr);
	if (rw) {
		nfb_comp_write32(comp, VS_DRP_DATA, *data);
	}
	nfb_comp_write32(comp, VS_DRP_CTRL, (page << 4) | rw);

	if (!rw) {
		do { /* Wait until the DRP operation finishes */
			drpstat = nfb_comp_read32(comp, VS_DRP_CTRL);
		} while (((drpstat & DRP_BUSY_BIT) != 0) && ++retries < 1000);
		*data = nfb_comp_read32(comp, VS_DRP_DATA);
	}
}

static inline uint32_t nc_mdio_dmap_drp_read(struct nfb_comp *comp, int prtad, uint32_t page, uint32_t addr)
{
	uint32_t data;
	__nc_mdio_dmap_drp_access(comp, prtad, page, addr, &data, 0);
	return data;
}

static inline void nc_mdio_dmap_drp_write(struct nfb_comp *comp, int prtad __attribute__((unused)), uint32_t page, uint32_t addr, uint32_t data)
{
	__nc_mdio_dmap_drp_access(comp, prtad, page, addr, &data, 1);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MDIODMAP_H */
