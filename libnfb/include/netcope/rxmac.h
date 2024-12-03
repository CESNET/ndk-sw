/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - RX MAC component
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_RXMAC_H
#define NETCOPE_RXMAC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __KERNEL__
#include <stdbool.h>
#endif

#include "mac.h"

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

enum nc_rxmac_frame_length_limit {
	RXMAC_FRAME_LENGTH_MIN = 0x0,
	RXMAC_FRAME_LENGTH_MAX = 0x1,
};

enum nc_rxmac_mac_filter {
	RXMAC_MAC_FILTER_PROMISCUOUS = 0x0,
	RXMAC_MAC_FILTER_TABLE = 0x1,
	RXMAC_MAC_FILTER_TABLE_BCAST = 0x2,
	RXMAC_MAC_FILTER_TABLE_BCAST_MCAST = 0x3,
};

struct nc_rxmac {
	unsigned mtu;
	unsigned mac_addr_count;

	unsigned has_counter_below_64 : 1;
	unsigned mac_addr_count_valid : 1;
};

struct nc_rxmac_counters {
	unsigned long long cnt_total;            /*!< All processed frames */
	unsigned long long cnt_octets;           /*!< Correct octets */
	unsigned long long cnt_received;         /*!< Correct frames */
	unsigned long long cnt_erroneous;        /*!< Discarded frames due to error */
	unsigned long long cnt_overflowed;       /*!< Discarded frames due to buffer overflow */
};

struct nc_rxmac_etherstats {
	unsigned long long octets;               /*!< Total number of octets received (including bad packets) */
	unsigned long long pkts;                 /*!< Total number of packets received (including bad packets) */
	unsigned long long broadcastPkts;        /*!< Total number of good broadcast packets received */
	unsigned long long multicastPkts;        /*!< Total number of good multicast packets received */
	unsigned long long CRCAlignErrors;       /*!< Total number of received packets that were between 64 and 1518 bytes long and had FCS error */
	unsigned long long undersizePkts;        /*!< Total number of received packets that were shorter than 64 bytes and OK */
	unsigned long long oversizePkts;         /*!< Total number of received packets that were longer than 1518 bytes and OK */
	unsigned long long fragments;            /*!< Total number of received packets that were shorter than 64 bytes and had FCS error */
	unsigned long long jabbers;              /*!< Total number of received packets that were longer than 1518 bytes and had FCS error */
	unsigned long long pkts64Octets;         /*!< Total number of received packets that were 64 bytes long */
	unsigned long long pkts65to127Octets;    /*!< Total number of received packets that were between 65 and 127 bytes long */
	unsigned long long pkts128to255Octets;   /*!< Total number of received packets that were between 128 and 255 bytes long */
	unsigned long long pkts256to511Octets;   /*!< Total number of received packets that were between 256 and 511 bytes long */
	unsigned long long pkts512to1023Octets;  /*!< Total number of received packets that were between 512 and 1023 bytes long */
	unsigned long long pkts1024to1518Octets; /*!< Total number of received packets that were between 1024 and 1518 bytes long */
};

struct nc_rxmac_status {
	unsigned enabled : 1;
	unsigned link_up : 1;
	unsigned overflow : 1;
	enum nc_rxmac_mac_filter mac_filter;    /*!< MAC address filtering status */
	unsigned mac_addr_count;		/*!< Maximum number of MAC address supported in MAC */
	unsigned error_mask;                    /*!< Error mask register */
	unsigned frame_length_min;              /*!< Minimal accepted frame length */
	unsigned frame_length_max;              /*!< Maximal accepted frame length */
	unsigned frame_length_max_capable;      /*!< Maximal configurable frame length */
	enum nc_mac_speed speed;
};


/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_rxmac *nc_rxmac_open(struct nfb_device *dev, int fdt_offset);
static inline struct nc_rxmac *nc_rxmac_open_index(struct nfb_device *dev, unsigned index);
static inline void             nc_rxmac_close(struct nc_rxmac *mac);
static inline void             nc_rxmac_enable(struct nc_rxmac *mac);
static inline void             nc_rxmac_disable(struct nc_rxmac *mac);
static inline int              nc_rxmac_read_status(struct nc_rxmac *mac, struct nc_rxmac_status *status);
static inline int              nc_rxmac_read_counters(struct nc_rxmac *mac, struct nc_rxmac_counters *counters, struct nc_rxmac_etherstats *stats);
static inline int              nc_rxmac_reset_counters(struct nc_rxmac *mac);
static inline void             nc_rxmac_mac_filter_enable(struct nc_rxmac *mac, enum nc_rxmac_mac_filter mode);
static inline unsigned         nc_rxmac_mac_address_count(struct nc_rxmac *mac);
static inline void             nc_rxmac_set_error_mask(struct nc_rxmac *mac, unsigned error_mask);


/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define RXMAC_REG_CNT_PACKETS_LO                0x0000
#define RXMAC_REG_CNT_PACKETS_HI                0x0010
#define RXMAC_REG_CNT_RECEIVED_LO               0x0004
#define RXMAC_REG_CNT_RECEIVED_HI               0x0014
#define RXMAC_REG_CNT_DISCARDED_LO              0x0008
#define RXMAC_REG_CNT_DISCARDED_HI              0x0018
#define RXMAC_REG_CNT_OVERFLOW_LO               0x000C
#define RXMAC_REG_CNT_OVERFLOW_HI               0x001C
#define RXMAC_REG_CNT_OCTETS_LO                 0x003C
#define RXMAC_REG_CNT_OCTETS_HI                 0x0040

/* Total received packets with bad CRC (etherStatsCRCAlignErrors) */
#define RXMAC_REG_CNT_ES_CRC_ERR_LO             0x0100
#define RXMAC_REG_CNT_ES_CRC_ERR_HI             0x0138

/* Total received packets over set MTU */
#define RXMAC_REG_CNT_ES_OVERSIZE_LO            0x0104
#define RXMAC_REG_CNT_ES_OVERSIZE_HI            0x013C

/* Total received packets below set minimal length */
#define RXMAC_REG_CNT_ES_UNDERSIZE_LO           0x0108
#define RXMAC_REG_CNT_ES_UNDERSIZE_HI           0x0140

/* Total received packets with broadcast address (etherStatsBroadcastPkts) */
#define RXMAC_REG_CNT_ES_BCAST_LO               0x010C
#define RXMAC_REG_CNT_ES_BCAST_HI               0x0144

/* Total received packets with multicast address (etherStatsMulticastPkts) */
#define RXMAC_REG_CNT_ES_MCAST_LO               0x0110
#define RXMAC_REG_CNT_ES_MCAST_HI               0x0148

/* Total received fragment frames (etherStatsFragments) */
#define RXMAC_REG_CNT_ES_FRAGMENTS_LO           0x0114
#define RXMAC_REG_CNT_ES_FRAGMENTS_HI           0x014C

/* Total received jabber frames (etherStatsJabbers) */
#define RXMAC_REG_CNT_ES_JABBERS_LO             0x0118
#define RXMAC_REG_CNT_ES_JABBERS_HI             0x0150

/* Total transfered octets (etherStatsOctets) */
#define RXMAC_REG_CNT_ES_OCTETS_LO              0x011C
#define RXMAC_REG_CNT_ES_OCTETS_HI              0x0154

/* Total received frames with length 64B (etherStatsPkts64Octets) */
#define RXMAC_REG_CNT_ES_FRAMES_64_LO           0x0120
#define RXMAC_REG_CNT_ES_FRAMES_64_HI           0x0158

/* Total received frames with length from 65B to 127B (etherStatsPkts65to127Octets) */
#define RXMAC_REG_CNT_ES_FRAMES_65_127_LO       0x0124
#define RXMAC_REG_CNT_ES_FRAMES_65_127_HI       0x015C

/* Total received frames with length from 128B to 255B (etherStatsPkts128to255Octets) */
#define RXMAC_REG_CNT_ES_FRAMES_128_255_LO      0x0128
#define RXMAC_REG_CNT_ES_FRAMES_128_255_HI      0x0160

/* Total received frames with length from 256B to 511B (etherStatsPkts256to511Octets) */
#define RXMAC_REG_CNT_ES_FRAMES_256_511_LO      0x012C
#define RXMAC_REG_CNT_ES_FRAMES_256_511_HI      0x0164

/* Total received frames with length from 512B to 1023B (etherStatsPkts512to1023Octets) */
#define RXMAC_REG_CNT_ES_FRAMES_512_1023_LO     0x0130
#define RXMAC_REG_CNT_ES_FRAMES_512_1023_HI     0x0168

/* Total received frames with length from 1024B to 1518B (etherStatsPkts1024to1518Octets) */
#define RXMAC_REG_CNT_ES_FRAMES_1024_1518_LO    0x0134
#define RXMAC_REG_CNT_ES_FRAMES_1024_1518_HI    0x016C

/* Total received frames with length above 1518B (etherStatsOversizePkts) */
#define RXMAC_REG_CNT_ES_FRAMES_OVER_1518_LO    0x0170
#define RXMAC_REG_CNT_ES_FRAMES_OVER_1518_HI    0x0174

/* Total received frames with length below 64B (etherStatsUndersizePkts) */
#define RXMAC_REG_CNT_ES_FRAMES_BELOW_64_LO     0x0178
#define RXMAC_REG_CNT_ES_FRAMES_BELOW_64_HI     0x017C

#define RXMAC_REG_ENABLE             0x0020
#define RXMAC_REG_ERROR_MASK         0x0024
#define RXMAC_REG_STATUS             0x0028
#define RXMAC_REG_STATUS_OVER          0x01
#define RXMAC_REG_STATUS_LINK          0x80
#define RXMAC_REG_CONTROL            0x002C
#define RXMAC_REG_FRAME_LEN_MIN      0x0030
#define RXMAC_REG_FRAME_LEN_MAX      0x0034
#define RXMAC_REG_MAC_FILTER         0x0038

#define RXMAC_REG_MAC_BASE           0x0080

#define RXMAC_MAC_ADDR_MASK 0xFFFFFFFFFFFFL
#define RXMAC_MAC_ADDR_VALID_BIT_MASK (1L << 48)

enum nc_rxmac_cmds {
	RXMAC_CMD_STROBE = 0x01,
	RXMAC_CMD_RESET  = 0x02,
};

#define RXMAC_READ_CNT(comp, name) \
	(((uint64_t)nfb_comp_read32((comp), RXMAC_REG_CNT_##name##_HI)) << 32 | \
                nfb_comp_read32((comp), RXMAC_REG_CNT_##name##_LO))

#define COMP_NETCOPE_RXMAC "netcope,rxmac"

#define RXMAC_COMP_LOCK (1 << 0)

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_rxmac *nc_rxmac_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_rxmac *mac;
	struct nfb_comp *comp;
	const fdt32_t *prop;
	int proplen;
	unsigned version = 0;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_RXMAC))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_rxmac));
	if (!comp)
		return NULL;

	mac = (struct nc_rxmac *) nfb_comp_to_user(comp);

	prop = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), fdt_offset, "version", &proplen);
	if (proplen == sizeof(*prop))
		version = fdt32_to_cpu(*prop);

	mac->has_counter_below_64 = version >= 0x00000002;

	prop = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), fdt_offset, "mtu", &proplen);

	if (proplen == sizeof(*prop))
		mac->mtu = fdt32_to_cpu(*prop);
	else
		mac->mtu = 0;

	mac->mac_addr_count_valid = 0;

	return mac;
}

static inline struct nc_rxmac *nc_rxmac_open_index(struct nfb_device *dev, unsigned index)
{
	int fdt_offset = nfb_comp_find(dev, COMP_NETCOPE_RXMAC, index);
	return nc_rxmac_open(dev, fdt_offset);
}

static inline void nc_rxmac_close(struct nc_rxmac *mac)
{
	nfb_comp_close(nfb_user_to_comp(mac));
}

static inline void __nc_rxmac_update_mac_addr_count(struct nc_rxmac *mac, uint32_t reg)
{
	if (mac->mac_addr_count_valid == 0) {
		mac->mac_addr_count = (reg & 0x0F800000) >> 23;
		mac->mac_addr_count_valid = 1;
	}
}

static inline void nc_rxmac_enable(struct nc_rxmac *mac)
{
	nfb_comp_write32(nfb_user_to_comp(mac), RXMAC_REG_ENABLE, 1);
}

static inline void nc_rxmac_disable(struct nc_rxmac *mac)
{
	nfb_comp_write32(nfb_user_to_comp(mac), RXMAC_REG_ENABLE, 0);
}

static inline int nc_rxmac_get_link(struct nc_rxmac *mac)
{
	uint32_t val = nfb_comp_read32(nfb_user_to_comp(mac), RXMAC_REG_STATUS);
	__nc_rxmac_update_mac_addr_count(mac, val);
	return (val & RXMAC_REG_STATUS_LINK) ? 1 : 0;
}

static inline int nc_rxmac_read_status(struct nc_rxmac *mac, struct nc_rxmac_status *s)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);
	uint32_t reg;

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	s->enabled          = nfb_comp_read32(comp, RXMAC_REG_ENABLE);
	s->error_mask       = nfb_comp_read32(comp, RXMAC_REG_ERROR_MASK);
	s->mac_filter       = (enum nc_rxmac_mac_filter) nfb_comp_read32(comp, RXMAC_REG_MAC_FILTER);
	s->frame_length_min = nfb_comp_read32(comp, RXMAC_REG_FRAME_LEN_MIN);
	s->frame_length_max = nfb_comp_read32(comp, RXMAC_REG_FRAME_LEN_MAX);

	reg                 = nfb_comp_read32(comp, RXMAC_REG_STATUS);
	__nc_rxmac_update_mac_addr_count(mac, reg);

	s->link_up          = (reg & RXMAC_REG_STATUS_LINK) ? 1 : 0;
	s->overflow         = (reg & RXMAC_REG_STATUS_OVER) ? 1 : 0;
	s->mac_addr_count   = nc_rxmac_mac_address_count(mac);

	s->frame_length_max_capable = mac->mtu;

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
	nfb_comp_unlock(comp, RXMAC_COMP_LOCK);
	return 0;
}

static inline int nc_rxmac_read_counters(struct nc_rxmac *mac, struct nc_rxmac_counters *c, struct nc_rxmac_etherstats *s)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	unsigned long long cnt_total = 0;

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	nfb_comp_write32(comp, RXMAC_REG_CONTROL, RXMAC_CMD_STROBE);

	if (c || s) {
		cnt_total        = RXMAC_READ_CNT(comp, PACKETS);
	}

	if (c) {
		c->cnt_total        = cnt_total;
		c->cnt_received     = RXMAC_READ_CNT(comp, RECEIVED);
		c->cnt_overflowed   = RXMAC_READ_CNT(comp, OVERFLOW);
		c->cnt_erroneous    = RXMAC_READ_CNT(comp, DISCARDED) - c->cnt_overflowed;
		c->cnt_octets       = RXMAC_READ_CNT(comp, OCTETS);
	}

	if (s) {
		s->octets                  = RXMAC_READ_CNT(comp, ES_OCTETS);
		s->pkts                    = cnt_total;
		s->broadcastPkts           = RXMAC_READ_CNT(comp, ES_BCAST);
		s->multicastPkts           = RXMAC_READ_CNT(comp, ES_MCAST);
		s->CRCAlignErrors          = RXMAC_READ_CNT(comp, ES_CRC_ERR);
		if (mac->has_counter_below_64) {
			s->undersizePkts       = RXMAC_READ_CNT(comp, ES_FRAMES_BELOW_64);
		} else {
			s->undersizePkts       = RXMAC_READ_CNT(comp, ES_UNDERSIZE);
		}
		s->oversizePkts            = RXMAC_READ_CNT(comp, ES_FRAMES_OVER_1518);
		s->fragments               = RXMAC_READ_CNT(comp, ES_FRAGMENTS);
		s->jabbers                 = RXMAC_READ_CNT(comp, ES_JABBERS);
		s->pkts64Octets            = RXMAC_READ_CNT(comp, ES_FRAMES_64);
		s->pkts65to127Octets       = RXMAC_READ_CNT(comp, ES_FRAMES_65_127);
		s->pkts128to255Octets      = RXMAC_READ_CNT(comp, ES_FRAMES_128_255);
		s->pkts256to511Octets      = RXMAC_READ_CNT(comp, ES_FRAMES_256_511);
		s->pkts512to1023Octets     = RXMAC_READ_CNT(comp, ES_FRAMES_512_1023);
		s->pkts1024to1518Octets    = RXMAC_READ_CNT(comp, ES_FRAMES_1024_1518);
	}

	nfb_comp_unlock(comp, RXMAC_COMP_LOCK);
	return 0;
}

static inline int nc_rxmac_reset_counters(struct nc_rxmac *mac)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	nfb_comp_write32(comp, RXMAC_REG_CONTROL, RXMAC_CMD_RESET);

	return 0;
}

static inline unsigned nc_rxmac_mac_address_count(struct nc_rxmac *mac)
{
	uint32_t reg;

	if (!mac->mac_addr_count_valid) {
		reg = nfb_comp_read32(nfb_user_to_comp(mac), RXMAC_REG_STATUS);
		__nc_rxmac_update_mac_addr_count(mac, reg);
	}
	return mac->mac_addr_count;
}

static inline int nc_rxmac_set_frame_length(struct nc_rxmac *mac, unsigned length, enum nc_rxmac_frame_length_limit limit)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	nfb_comp_write32(comp, limit == RXMAC_FRAME_LENGTH_MIN ? RXMAC_REG_FRAME_LEN_MIN : RXMAC_REG_FRAME_LEN_MAX, length);

	return 0;
}

static inline void nc_rxmac_mac_filter_enable(struct nc_rxmac *mac, enum nc_rxmac_mac_filter mode)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	nfb_comp_write32(comp, RXMAC_REG_MAC_FILTER, mode);
}

static inline void nc_rxmac_set_error_mask(struct nc_rxmac *mac, unsigned error_mask)
{
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	/* only lowest 5 bits are used */
	nfb_comp_write32(comp, RXMAC_REG_ERROR_MASK, error_mask & 0x1F);
}


static inline int _nc_rxmac_set_mac(struct nc_rxmac *mac, int index, unsigned long long mac_addr, bool valid)
{
	unsigned i;
	struct nfb_comp *comp = nfb_user_to_comp(mac);
	uint64_t reg64;

	if (index >= (signed) nc_rxmac_mac_address_count(mac))
		return -EINVAL;

	if (index < 0) {
		/* Find an empty position */
		for (i = 0; i < nc_rxmac_mac_address_count(mac); i++) {
			reg64 = nfb_comp_read64(comp, RXMAC_REG_MAC_BASE + i * 8);
			if ((reg64 & RXMAC_MAC_ADDR_VALID_BIT_MASK) == 0) {
				index = i;
				break;
			}
		}
		if (index < 0)
			return -ENOMEM;
	}

	if (valid)
		mac_addr |= RXMAC_MAC_ADDR_VALID_BIT_MASK;
	nfb_comp_write64(comp, RXMAC_REG_MAC_BASE + index * 8, mac_addr);
	return index;
}

static inline int nc_rxmac_set_mac(struct nc_rxmac *mac, int index, unsigned long long mac_addr, bool valid)
{
	int ret;
	bool enabled;
	struct nfb_comp *comp = nfb_user_to_comp(mac);

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	/* Check current state of MAC */
	enabled = nfb_comp_read32(comp, RXMAC_REG_ENABLE);

	if (enabled)
		nc_rxmac_disable(mac);

	ret = _nc_rxmac_set_mac(mac, index, mac_addr, valid);

	if (enabled)
		nc_rxmac_enable(mac);

	nfb_comp_unlock(comp, RXMAC_COMP_LOCK);

	return ret;
}

static inline int nc_rxmac_get_mac_list(struct nc_rxmac *mac, unsigned long long *mac_addr_list, bool *valid, unsigned mac_addr_count)
{
	unsigned i;
	uint64_t reg64;
	bool enabled;

	struct nfb_comp *comp = nfb_user_to_comp(mac);

	if (mac_addr_count > nc_rxmac_mac_address_count(mac))
		return -EINVAL;

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	enabled = nfb_comp_read32(comp, RXMAC_REG_ENABLE);

	if (enabled)
		nc_rxmac_disable(mac);

	for (i = 0; i < mac_addr_count; i++) {
		reg64 = nfb_comp_read64(comp, RXMAC_REG_MAC_BASE + i*8);

		mac_addr_list[i] = reg64 & RXMAC_MAC_ADDR_MASK;

		if (valid != NULL)
			valid[i] = (reg64 & RXMAC_MAC_ADDR_VALID_BIT_MASK) ? 1 : 0;
	}

	if (enabled)
		nc_rxmac_enable(mac);

	nfb_comp_unlock(comp, RXMAC_COMP_LOCK);

	return mac_addr_count;
}

static inline int nc_rxmac_set_mac_list(struct nc_rxmac *mac, unsigned long long *mac_addr_list, bool *valid, unsigned mac_addr_count)
{
	unsigned i;
	bool enabled;

	struct nfb_comp *comp = nfb_user_to_comp(mac);

	if (mac_addr_count > nc_rxmac_mac_address_count(mac))
		return -EINVAL;

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	enabled = nfb_comp_read32(comp, RXMAC_REG_ENABLE);

	if (enabled)
		nc_rxmac_disable(mac);

	for (i = 0; i < mac_addr_count; i++) {
		_nc_rxmac_set_mac(mac, i, mac_addr_list[i], valid[i]);
	}

	if (enabled)
		nc_rxmac_enable(mac);

	nfb_comp_unlock(comp, RXMAC_COMP_LOCK);

	return mac_addr_count;
}

static inline int nc_rxmac_counters_initialize(struct nc_rxmac_counters *c, struct nc_rxmac_etherstats *s)
{
	struct nc_rxmac_counters ic = {0};
	struct nc_rxmac_etherstats is = {0};
	*c = ic;
	*s = is;
	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_RXMAC_H */
