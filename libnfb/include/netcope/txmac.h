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
	int _unused;
};

struct nc_txmac_counters {
	unsigned long long cnt_total;     /*!< All processed frames */
	unsigned long long cnt_octets;    /*!< Correct octets */
	unsigned long long cnt_sent;      /*!< Correct frames */
	unsigned long long cnt_erroneous; /*!< Discarded frames */
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

#define COMP_NETCOPE_TXMAC "netcope,txmac"

#define TXMAC_COMP_LOCK (1 << 0)

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_txmac *nc_txmac_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_txmac *mac;
	struct nfb_comp *comp;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_TXMAC))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_txmac));
	if (!comp)
		return NULL;

	mac = (struct nc_txmac *) nfb_comp_to_user(comp);

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

	if (!nfb_comp_lock(comp, TXMAC_COMP_LOCK))
		return -EAGAIN;

	nfb_comp_write32(comp, TXMAC_REG_CONTROL, TXMAC_CMD_STROBE);

	counters->cnt_total = TXMAC_READ_CNT(comp, PACKETS);
	counters->cnt_sent = TXMAC_READ_CNT(comp, SENT);
	counters->cnt_erroneous = TXMAC_READ_CNT(comp, DISCARDED);
	counters->cnt_octets = TXMAC_READ_CNT(comp, OCTETS);

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
