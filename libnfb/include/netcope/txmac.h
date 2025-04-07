/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - TX MAC component
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_TXMAC_H
#define NETCOPE_TXMAC_H


#ifdef __cplusplus
extern "C" {
#endif

#include "mac.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_txmac {
	unsigned has_ext_drop_counters : 1;
};

struct nc_txmac_counters {
	unsigned long long cnt_total;           /*!< All processed frames */
	unsigned long long cnt_octets;          /*!< Correct octets */
	unsigned long long cnt_sent;            /*!< Correct frames */
	unsigned long long cnt_drop;            /*!< All discarded frames (multiple discard reasons can occur at once) */
	unsigned long long cnt_total_octets;    /*!< All processed bytes */

	unsigned long long cnt_drop_disabled;   /*!< Frames droped due to disabled MAC */
	unsigned long long cnt_drop_link;       /*!< Frames droped due to link down */
	unsigned long long cnt_erroneous;       /*!< Discarded frames due to error (subset of cnt_drop; multiple errors below can occur at once) */
	unsigned long long cnt_err_length;      /*!< Frames droped due to MTU mismatch */
};

struct nc_txmac_status {
	unsigned enabled : 1;
	enum nc_mac_speed speed;
};


/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txmac *nc_txmac_open(struct nfb_device *dev, int fdt_offset);
static inline struct nc_txmac *nc_txmac_open_index(struct nfb_device *dev, unsigned index);
static inline void      nc_txmac_close(struct nc_txmac *mac);
static inline void      nc_txmac_enable(struct nc_txmac *mac);
static inline void      nc_txmac_disable(struct nc_txmac *mac);
static inline int       nc_txmac_read_counters(struct nc_txmac *mac, struct nc_txmac_counters *counters);
static inline int       nc_txmac_reset_counters(struct nc_txmac *mac);


/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define TXMAC_REG_CNT_PACKETS_LO     0x0000
#define TXMAC_REG_CNT_PACKETS_HI     0x0010
#define TXMAC_REG_CNT_OCTETS_LO      0x0004
#define TXMAC_REG_CNT_OCTETS_HI      0x0014
#define TXMAC_REG_CNT_DISCARDED_LO   0x0008
#define TXMAC_REG_CNT_DISCARDED_HI   0x0018
#define TXMAC_REG_CNT_SENT_LO        0x000C
#define TXMAC_REG_CNT_SENT_HI        0x001C

#define TXMAC_REG_ENABLE             0x0020
#define TXMAC_REG_STATUS             0x0030
#define TXMAC_REG_STATUS_LINK          0x80
#define TXMAC_REG_CONTROL            0x002C

enum nc_txmac_cmds {
	TXMAC_CMD_STROBE = 0x01,
	TXMAC_CMD_RESET  = 0x02,
};

#define TXMAC_READ_CNT(comp, name) \
	((uint64_t)nfb_comp_read32((comp), TXMAC_REG_CNT_##name##_HI) << 32 | \
	           nfb_comp_read32((comp), TXMAC_REG_CNT_##name##_LO))

#define _NC_TXMAC_REG_BUFFER_PAIR(s, name) ( \
		(((uint64_t)((s).name##_h)) << 32) | \
		(((uint64_t)((s).name##_l)) <<  0) \
	)

#define COMP_NETCOPE_TXMAC "netcope,txmac"

#define TXMAC_COMP_LOCK (1 << 0)

union _nc_txmac_reg_buffer {
	/* Register range: 0x0000 - 0x0020 */
	struct __attribute__((packed)) {
		uint32_t total_l;
		uint32_t octets_l;
		uint32_t discarded_l;
		uint32_t sent_l;
		uint32_t total_h;
		uint32_t octets_h;
		uint32_t discarded_h;
		uint32_t sent_h;
	} r1;

	/* Register range: 0x0040 - 0x0060 */
	struct __attribute__((packed)) {
		uint64_t total_octets;
		uint64_t drop_link;
		uint64_t err_length;
		uint64_t drop_disabled;
	} r2;
};

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txmac *nc_txmac_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_txmac *mac;
	struct nfb_comp *comp;
	const fdt32_t *prop;
	int proplen;
	unsigned version = 0;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXMAC))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_txmac));
	if (!comp)
		return NULL;

	mac = (struct nc_txmac *) nfb_comp_to_user(comp);

	prop = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), fdt_offset, "version", &proplen);
	if (proplen == sizeof(*prop))
		version = fdt32_to_cpu(*prop);

	mac->has_ext_drop_counters = version >= 0x00000003;

	return mac;
}

static inline struct nc_txmac *nc_txmac_open_index(struct nfb_device *dev, unsigned index)
{
	int fdt_offset = nfb_comp_find(dev, COMP_NETCOPE_TXMAC, index);
	return nc_txmac_open(dev, fdt_offset);
}

static inline void nc_txmac_close(struct nc_txmac *mac)
{
	nfb_comp_close(nfb_user_to_comp(mac));
}

static inline void nc_txmac_enable(struct nc_txmac *mac)
{
	nfb_comp_write32(nfb_user_to_comp(mac), TXMAC_REG_ENABLE, 1);
}

static inline void nc_txmac_disable(struct nc_txmac *mac)
{
	nfb_comp_write32(nfb_user_to_comp(mac), TXMAC_REG_ENABLE, 0);
}

static inline int nc_txmac_read_status(struct nc_txmac *mac, struct nc_txmac_status *s)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);
	uint32_t reg;

	if (!nfb_comp_lock(comp, TXMAC_COMP_LOCK))
		return -EAGAIN;

	nfb_comp_write32(comp, TXMAC_REG_CONTROL, TXMAC_CMD_STROBE);

	s->enabled          = nfb_comp_read32(comp, TXMAC_REG_ENABLE);
	reg                 = nfb_comp_read32(comp, TXMAC_REG_STATUS);

	s->speed = (enum nc_mac_speed) ((reg >> 4) & 0x7);
	switch (s->speed) {
		case MAC_SPEED_10G:
		case MAC_SPEED_40G:
		case MAC_SPEED_100G:
			break;
		default:
			s->speed = MAC_SPEED_UNKNOWN;
			break;
	}
	nfb_comp_unlock(comp, TXMAC_COMP_LOCK);
	return 0;
}


static inline int nc_txmac_read_counters(struct nc_txmac *mac, struct nc_txmac_counters *counters)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);
	union _nc_txmac_reg_buffer buf;

	if (!nfb_comp_lock(comp, TXMAC_COMP_LOCK))
		return -EAGAIN;

	nfb_comp_write32(comp, TXMAC_REG_CONTROL, TXMAC_CMD_STROBE);

	nfb_comp_read(comp, &buf.r1, sizeof(buf.r1), 0x0000);

	counters->cnt_total             = _NC_TXMAC_REG_BUFFER_PAIR(buf.r1, total);
	counters->cnt_octets            = _NC_TXMAC_REG_BUFFER_PAIR(buf.r1, octets);
	counters->cnt_sent              = _NC_TXMAC_REG_BUFFER_PAIR(buf.r1, sent);
	counters->cnt_drop              = _NC_TXMAC_REG_BUFFER_PAIR(buf.r1, discarded);

	if (mac->has_ext_drop_counters) {
		nfb_comp_read(comp, &buf.r2, sizeof(buf.r2), 0x0040);

		counters->cnt_total_octets      = buf.r2.total_octets;
		counters->cnt_drop_disabled     = buf.r2.drop_disabled;
		counters->cnt_drop_link         = buf.r2.drop_link;
		counters->cnt_err_length        = buf.r2.err_length;

		counters->cnt_erroneous         = counters->cnt_err_length;
	} else {
		counters->cnt_total_octets      = 0;
		counters->cnt_drop_disabled     = 0;
		counters->cnt_drop_link         = 0;
		counters->cnt_err_length        = 0;

		counters->cnt_erroneous         = counters->cnt_drop;
	}

	nfb_comp_unlock(comp, TXMAC_COMP_LOCK);
	return 0;
}

static inline int nc_txmac_reset_counters(struct nc_txmac *mac)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	nfb_comp_write32(comp, TXMAC_REG_CONTROL, TXMAC_CMD_RESET);

	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_TXMAC_H */
