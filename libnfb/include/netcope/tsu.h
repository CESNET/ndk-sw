/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - timestamping unit functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_TSU_H
#define NETCOPE_TSU_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __KERNEL__
#include <libfdt.h>
#include <nfb/nfb.h>
#endif

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_tsu {
    int _unused;
};

/*!
 * \brief  Time representation used internally by TSU unit
 *
 */
struct nc_tsu_time {
	uint32_t sec;
	uint64_t fraction;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/*! Open the TSU component on given FDT offset */
static inline struct nc_tsu *       nc_tsu_open(struct nfb_device *dev, int fdt_offset);
/*! Close TSU component */
static inline void                  nc_tsu_close(struct nc_tsu *tsu);

/*! Enable TSU component (mark Timestamp signal as valid) */
static inline void                  nc_tsu_enable(struct nc_tsu *tsu);
/*! Disable TSU component (mark Timestamp signal as invalid) */
static inline void                  nc_tsu_disable(struct nc_tsu *tsu);

/*! Get TSU component's real time value (RTR register) */
static inline struct nc_tsu_time    nc_tsu_get_rtr(struct nc_tsu *tsu);
/*! Set TSU component's real time value (RTR register) */
static inline void                  nc_tsu_set_rtr(struct nc_tsu *tsu, struct nc_tsu_time rtr);

/*! Get TSU component's per-tick increment value (INCR_VAL register) */
static inline uint64_t              nc_tsu_get_inc(struct nc_tsu *tsu);
/*! Set TSU component's per-tick increment value (INCR_VAL register) */
static inline void                  nc_tsu_set_inc(struct nc_tsu *tsu, uint64_t frac);

/*! Get TSU component's PPS register value (RTR is copied here on PPS signal's falling edge) */
static inline struct nc_tsu_time    nc_tsu_get_pps(struct nc_tsu *tsu);

/*! Get the number of configured clock signal sources */
static inline unsigned              nc_tsu_clk_sources_count(struct nc_tsu *tsu);
/*! Get the number of configured PPS signal sources */
static inline unsigned              nc_tsu_pps_sources_count(struct nc_tsu *tsu);

/*! Select clock signal source 'clk_index' to be used */
static inline void                  nc_tsu_select_clk_source(struct nc_tsu *tsu, unsigned clk_index);
/*! Select PPS signal source 'pps_index' to be used */
static inline void                  nc_tsu_select_pps_source(struct nc_tsu *tsu, unsigned pps_index);

/*! Return nonzero if PPS signal activity was detected */
static inline int                   nc_tsu_pps_is_active(struct nc_tsu *tsu);
/*! Return nonzero if clock signal activity was detected */
static inline int                   nc_tsu_clk_is_active(struct nc_tsu *tsu);

/*! Get TSU component's frequency in Hz */
static inline unsigned              nc_tsu_get_frequency(struct nc_tsu *tsu);

static inline int                   nc_tsu_lock(struct nc_tsu *tsu);
static inline void                  nc_tsu_unlock(struct nc_tsu *tsu);

#define TSU_LOCK_MODIFY         1

/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define TSU_REG_MI_DATA_LOW     0x00
#define TSU_REG_MI_DATA_MIDDLE  0x04
#define TSU_REG_MI_DATA_HIGH    0x08
#define TSU_REG_CONTROL         0x0C
#define TSU_REG_STATE           0x10
#define TSU_REG_INTA            0x14
#define TSU_REG_PPS_SEL         0x18
#define TSU_REG_FREQUENCY       0x1C
#define TSU_REG_CLK_SEL         0x20
#define TSU_REG_SRC_REG         0x24

#define TSU_CMD_WRITE_INC       0x00
#define TSU_CMD_WRITE_RT        0x01
#define TSU_CMD_READ_INC        0x04
#define TSU_CMD_READ_RT         0x05
#define TSU_CMD_READ_PPS        0x07

#define COMP_NETCOPE_TSU "netcope,tsu"

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_tsu *nc_tsu_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_tsu *tsu;
	struct nfb_comp *comp;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TSU))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_tsu));
	if (!comp)
		return NULL;

	tsu = (struct nc_tsu *) nfb_comp_to_user(comp);

	return tsu;
}

static inline void nc_tsu_close(struct nc_tsu *tsu)
{
	nfb_comp_close(nfb_user_to_comp(tsu));
}

static inline void nc_tsu_enable(struct nc_tsu *tsu)
{
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_INTA, 1);
}

static inline void nc_tsu_disable(struct nc_tsu *tsu)
{
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_INTA, 0);
}

static inline struct nc_tsu_time nc_tsu_get_rtr(struct nc_tsu *tsu)
{
	struct nc_tsu_time ret;

	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_CONTROL, TSU_CMD_READ_RT);

	ret.sec = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_HIGH);
	ret.fraction = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_MIDDLE);
	ret.fraction <<= 32;
	ret.fraction |= nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_LOW);

	return ret;
}

static inline void nc_tsu_set_rtr(struct nc_tsu *tsu, struct nc_tsu_time rtr)
{
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_HIGH, rtr.sec);
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_MIDDLE, rtr.fraction >> 32);
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_LOW, rtr.fraction & 0xFFFFFFFFULL);

	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_CONTROL, TSU_CMD_WRITE_RT);
}

static inline uint64_t nc_tsu_get_inc(struct nc_tsu *tsu)
{
	uint64_t ret;

	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_CONTROL, TSU_CMD_READ_INC);

	/* The INCR_VAL register is only 39 bits long, the middle uses 7 bits */
	ret = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_MIDDLE) & 0x7F;
	ret <<= 32;
	ret |= nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_LOW);

	return ret;
}

static inline void nc_tsu_set_inc(struct nc_tsu *tsu, uint64_t frac)
{
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_HIGH, 0);
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_MIDDLE, (frac >> 32) & 0x7F);
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_LOW, frac);

	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_CONTROL, TSU_CMD_WRITE_INC);
}

static inline struct nc_tsu_time nc_tsu_get_pps(struct nc_tsu *tsu)
{
	struct nc_tsu_time ret;

	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_CONTROL, TSU_CMD_READ_PPS);

	ret.sec = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_HIGH);
	ret.fraction = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_MIDDLE);
	ret.fraction <<= 32;
	ret.fraction |= nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_MI_DATA_LOW);

	return ret;
}

/* CLK / PPS */
static inline unsigned nc_tsu_clk_sources_count(struct nc_tsu *tsu)
{
	uint32_t tmp = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_SRC_REG);
	return (tmp >> 16);
}

static inline unsigned nc_tsu_pps_sources_count(struct nc_tsu *tsu)
{
	uint32_t tmp = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_SRC_REG);
	return (tmp & 0xFFFF);
}

static inline void nc_tsu_select_clk_source(struct nc_tsu *tsu, unsigned clk_index)
{
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_CLK_SEL, clk_index);
}

static inline void nc_tsu_select_pps_source(struct nc_tsu *tsu, unsigned pps_index)
{
	nfb_comp_write32(nfb_user_to_comp(tsu), TSU_REG_PPS_SEL, pps_index);
}

static inline int nc_tsu_clk_is_active(struct nc_tsu *tsu)
{
	uint32_t tmp = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_STATE);

	return (tmp & 0x2) ? 1 : 0;
}

static inline int nc_tsu_pps_is_active(struct nc_tsu *tsu)
{
	uint32_t tmp = nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_STATE);

	return (tmp & 0x1) ? 1 : 0;
}

static inline unsigned nc_tsu_get_frequency(struct nc_tsu *tsu)
{
	return nfb_comp_read32(nfb_user_to_comp(tsu), TSU_REG_FREQUENCY) + 1;
}

static inline int nc_tsu_lock(struct nc_tsu *tsu)
{
	return nfb_comp_lock(nfb_user_to_comp(tsu), TSU_LOCK_MODIFY);
}

static inline void nc_tsu_unlock(struct nc_tsu *tsu)
{
	nfb_comp_unlock(nfb_user_to_comp(tsu), TSU_LOCK_MODIFY);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_TSU_H */
