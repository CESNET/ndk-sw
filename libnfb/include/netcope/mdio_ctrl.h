/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - MDIO controller component
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Peresini <xperes00@stud.fit.vutbr.cz>
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_MDIOCTRL_H
#define NETCOPE_MDIOCTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __KERNEL__
#include <linux/printk.h>
#else
#include <err.h>
#endif /* __KERNEL__ */

/* ~~~~[ DEFINES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define COMP_NETCOPE_MDIO "netcope,mdio"

/* MDIO registers in design */
/** MDIO frame register in design */
#define MDIO_CTRL_REG_FRAME     0x00
/** MDIO data register in design */
#define MDIO_CTRL_REG_DATA      0x08
/** MDIO status register in design */
#define MDIO_CTRL_REG_STAT      0x0c

/* MDIO operation constants */
/** MDIO operation for passing address (EDF only) */
#define MDIO_CTRL_OP_ADDR       0x00
/** MDIO write opeartion (SDF & EDF) */
#define MDIO_CTRL_OP_WRITE      0x01
/** MDIO read operation (SDF only) */
#define MDIO_CTRL_OP_READ       0x02
/** MDIO read operation (EDF only) */
#define MDIO_CTRL_OP_EDF_READ   0x03
/** Start of frame for SDF (stadard data format) */
#define MDIO_CTRL_SDF           0x01
/** Start of frame for EDF (extended data format) */
#define MDIO_CTRL_EDF           0x00

#define MDIO_COMP_LOCK (1 << 0)

/* ~~~~[ HELPERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline unsigned NT_MDIO_DATA(unsigned val) { return val & 0xffff; }
static inline unsigned NT_MDIO_REG (unsigned val) { return val & 0x001f; }
static inline unsigned NT_MDIO_PHY (unsigned val) { return NT_MDIO_DATA(val); }
static inline unsigned NT_MDIO_OP  (unsigned val) { return val & 0x0003; }
static inline unsigned NT_MDIO_SDF (unsigned val) { return NT_MDIO_OP(val); }


/* ~~~~[ MACROS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/**
 * MDIO_SEND Send a MDIO frame
 */

static inline void _mdio_ctrl_send(struct nfb_comp *comp, int sdf, int op,
		int prtad, int devad, uint16_t addr_data)
{
	uint32_t val =
		NT_MDIO_DATA(addr_data)	<< 16 |
		NT_MDIO_REG (devad)	<<  9 |
		NT_MDIO_PHY (prtad) 	<<  4 |
		NT_MDIO_OP  (op)    	<<  2 |
		NT_MDIO_SDF (sdf)   	<<  0;

#ifdef __KERNEL__
	udelay(150);
#else
	usleep(150);
#endif
	nfb_comp_write32(comp, MDIO_CTRL_REG_FRAME, val);
}

static inline void _mdio_ctrl_wait(struct nfb_comp *comp)
{
	do {
#ifdef __KERNEL__
		udelay(150);
#else
		usleep(150);
#endif
	} while (nfb_comp_read32(comp, MDIO_CTRL_REG_STAT) & 0x00010000);
}

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nfb_comp *nc_mdio_ctrl_open_ext(const struct nfb_device *dev,
		int fdt_offset, int user_size);
static inline int nc_mdio_ctrl_read(struct nfb_comp *comp, int prtad, int devad, uint16_t addr);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nfb_comp *nc_mdio_ctrl_open_ext(const struct nfb_device *dev,
		int fdt_offset, int user_size)
{
	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_MDIO))
		return NULL;

	return nfb_comp_open_ext(dev, fdt_offset, user_size);
}

static inline int nc_mdio_ctrl_read(struct nfb_comp *comp, int prtad, int devad, uint16_t addr)
{
	int data;

	if (!nfb_comp_lock(comp, MDIO_COMP_LOCK))
		return -EAGAIN;

	_mdio_ctrl_send(comp, MDIO_CTRL_EDF, MDIO_CTRL_OP_ADDR, prtad, devad, addr);
	_mdio_ctrl_wait(comp);
	_mdio_ctrl_send(comp, MDIO_CTRL_EDF, MDIO_CTRL_OP_EDF_READ, prtad, devad, 0);
	_mdio_ctrl_wait(comp);

	data = NT_MDIO_DATA(nfb_comp_read32(comp, MDIO_CTRL_REG_DATA));
	nfb_comp_unlock(comp, MDIO_COMP_LOCK);
	return data;
}

static inline int nc_mdio_ctrl_write(struct nfb_comp *comp, int prtad, int devad, uint16_t addr, uint16_t data)
{
	if (!nfb_comp_lock(comp, MDIO_COMP_LOCK))
		return -EAGAIN;

	_mdio_ctrl_send(comp, MDIO_CTRL_EDF, MDIO_CTRL_OP_ADDR, prtad, devad, addr);
	_mdio_ctrl_wait(comp);
	_mdio_ctrl_send(comp, MDIO_CTRL_EDF, MDIO_CTRL_OP_WRITE, prtad, devad, data);
	_mdio_ctrl_wait(comp);
	nfb_comp_unlock(comp, MDIO_COMP_LOCK);

	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MDIOCTRL_H */
