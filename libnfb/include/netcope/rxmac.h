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
	unsigned has_ext_drop_counters: 1;
};

struct nc_rxmac_counters {
	unsigned long long cnt_total;            /*!< All processed frames */
	unsigned long long cnt_total_octets;     /*!< All processed bytes */
	unsigned long long cnt_octets;           /*!< Correct octets */
	unsigned long long cnt_received;         /*!< Correct frames */
	unsigned long long cnt_drop;             /*!< All discarded frames (multiple discard reasons can occur at once) */
	unsigned long long cnt_overflowed;       /*!< Discarded frames due to buffer overflow (subset of cnt_drop) */
	unsigned long long cnt_drop_disabled;    /*!< Frames droped due to disabled MAC (subset of cnt_drop) */
	unsigned long long cnt_drop_filtered;    /*!< Frames droped due to MAC address filter (subset of cnt_drop) */
	unsigned long long cnt_erroneous;        /*!< Discarded frames due to error (subset of cnt_drop; multiple errors below can occur at once) */
	unsigned long long cnt_err_length;       /*!< Frames droped due to MTU mismatch (subset of cnt_erroneous)*/
	unsigned long long cnt_err_crc;          /*!< Frames droped due to bad CRC (subset of cnt_erroneous)*/
	unsigned long long cnt_err_mii;          /*!< Frames droped due to errors on MII (subset of cnt_erroneous)*/
};

struct nc_rxmac_etherstats {
	unsigned long long octets;               /*!< Total number of octets received (including bad packets) */
	unsigned long long pkts;                 /*!< Total number of packets received (including bad packets) */
	unsigned long long broadcastPkts;        /*!< Total number of good broadcast packets received */
	unsigned long long multicastPkts;        /*!< Total number of good multicast packets received */
	unsigned long long CRCAlignErrors;       /*!< Total number of received packets that had FCS error; not exactly etherStatsCRCAlignErrors (condition: were between 64 and 1518 bytes long) */
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
	unsigned long long underMinPkts;         /*!< Total number of received packets that were shorter than configured minimum (not in etherStats) */
	unsigned long long overMaxPkts;          /*!< Total number of received packets that were longer than configured maximum (not in etherStats) */
	unsigned long long pkts1519to2047Octets; /*!< Total number of received packets that were between 1519 and 2047 bytes long */
	unsigned long long pkts2048to4095Octets; /*!< Total number of received packets that were between 2048 and 4095 bytes long */
	unsigned long long pkts4096to8191Octets; /*!< Total number of received packets that were between 4096 and 8191 bytes long */
	unsigned long long pktsOverBinsOctets;   /*!< Total number of received packets that were 8192 or more bytes long */
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

#define RXMAC_REG_CNT_ES_OCTETS_LO              0x011C
#define RXMAC_REG_CNT_ES_OCTETS_HI              0x0154

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

union _nc_rxmac_reg_status_buffer {
	/* Register range: 0x0020 - 0x003C */
	struct __attribute__((packed)) {
		uint32_t enabled;
		uint32_t error_mask;
		uint32_t status;
		uint32_t control;
		uint32_t frame_length_min;
		uint32_t frame_length_max;
		uint32_t mac_filter;
	} r1;
};

union _nc_rxmac_reg_buffer {
	/* Register range: 0x0000 - 0x0020 */
	struct __attribute__((packed)) {
		uint32_t total_l;
		uint32_t received_l;
		uint32_t discarded_l;
		uint32_t overflowed_l;
		uint32_t total_h;
		uint32_t received_h;
		uint32_t discarded_h;
		uint32_t overflowed_h;
	} r1;

	/* Register range: 0x003C - 0x0040 */
	struct __attribute__((packed)) {
		uint64_t octets;
	} r2;

	/* Register range: 0x0100 - 0x0180 */
	struct __attribute__((packed)) {
		uint32_t CRCAlignErrors_l;
		uint32_t oversize_l;
		uint32_t undersize_l;
		uint32_t broadcastPkts_l;
		uint32_t multicastPkts_l;
		uint32_t fragments_l;
		uint32_t jabbers_l;
		uint32_t octets_l;
		uint32_t pkts64Octets_l;
		uint32_t pkts65to127Octets_l;
		uint32_t pkts128to255Octets_l;
		uint32_t pkts256to511Octets_l;
		uint32_t pkts512to1023Octets_l;
		uint32_t pkts1024to1518Octets_l;
		uint32_t CRCAlignErrors_h;
		uint32_t oversize_h;
		uint32_t undersize_h;
		uint32_t broadcastPkts_h;
		uint32_t multicastPkts_h;
		uint32_t fragments_h;
		uint32_t jabbers_h;
		uint32_t octets_h;
		uint32_t pkts64Octets_h;
		uint32_t pkts65to127Octets_h;
		uint32_t pkts128to255Octets_h;
		uint32_t pkts256to511Octets_h;
		uint32_t pkts512to1023Octets_h;
		uint32_t pkts1024to1518Octets_h;
		uint64_t over1518;
		uint64_t below64;
		/* 0x0180 */
		uint64_t pkts1519to2047Octets;
		uint64_t pkts2048to4095Octets;
		uint64_t pkts4096to8191Octets;
		uint64_t pkts8192plusOctets;
	} e1;

	/* Register range: 0x01A0 - 0x01D0 */
	struct __attribute__((packed)) {
		uint64_t drop_filtered;
		uint64_t err;
		uint64_t drop_disabled;
		uint64_t err_mii;
		uint64_t err_crc;
		uint64_t err_length;
	} r3;
};

#define RXMAC_READ_CNT(comp, name) \
	(((uint64_t)nfb_comp_read32((comp), RXMAC_REG_CNT_##name##_HI)) << 32 | \
                nfb_comp_read32((comp), RXMAC_REG_CNT_##name##_LO))

#define _NC_RXMAC_REG_BUFFER_PAIR(s, name) ( \
		(((uint64_t)((s).name##_h)) << 32) | \
		(((uint64_t)((s).name##_l)) <<  0) \
	)

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
	mac->has_ext_drop_counters = version >= 0x00000003;

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
	union _nc_rxmac_reg_status_buffer buf;

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	nfb_comp_read(comp, &buf.r1, sizeof(buf.r1), 0x0020);

	s->enabled          = buf.r1.enabled;
	s->error_mask       = buf.r1.error_mask;
	s->mac_filter       = (enum nc_rxmac_mac_filter) buf.r1.mac_filter;
	s->frame_length_min = buf.r1.frame_length_min;
	s->frame_length_max = buf.r1.frame_length_max;

	__nc_rxmac_update_mac_addr_count(mac, buf.r1.status);

	s->link_up          = (buf.r1.status & RXMAC_REG_STATUS_LINK) ? 1 : 0;
	s->overflow         = (buf.r1.status & RXMAC_REG_STATUS_OVER) ? 1 : 0;
	s->mac_addr_count   = nc_rxmac_mac_address_count(mac);

	s->frame_length_max_capable = mac->mtu;

	s->speed = (enum nc_mac_speed) ((buf.r1.status >> 4) & 0x7);
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
	union _nc_rxmac_reg_buffer buf;

	if (!nfb_comp_lock(comp, RXMAC_COMP_LOCK))
		return -EAGAIN;

	nfb_comp_write32(comp, RXMAC_REG_CONTROL, RXMAC_CMD_STROBE);

	if (c) {
		nfb_comp_read(comp, &buf.r1, sizeof(buf.r1), 0x0000);
		c->cnt_total               = _NC_RXMAC_REG_BUFFER_PAIR(buf.r1, total);
		c->cnt_received            = _NC_RXMAC_REG_BUFFER_PAIR(buf.r1, received);
		c->cnt_overflowed          = _NC_RXMAC_REG_BUFFER_PAIR(buf.r1, overflowed);
		c->cnt_drop                = _NC_RXMAC_REG_BUFFER_PAIR(buf.r1, discarded);

		nfb_comp_read(comp, &buf.r2, sizeof(buf.r2), 0x003C);
		c->cnt_octets              = buf.r2.octets;

		if (mac->has_ext_drop_counters) {
			nfb_comp_read(comp, &buf.r3, sizeof(buf.r3), 0x01A0);
			c->cnt_err_length       = buf.r3.err_length;
			c->cnt_err_crc          = buf.r3.err_crc;
			c->cnt_err_mii          = buf.r3.err_mii;
			c->cnt_drop_disabled    = buf.r3.drop_disabled;
			c->cnt_drop_filtered    = buf.r3.drop_filtered;

			c->cnt_erroneous        = buf.r3.err;
		} else {
			c->cnt_err_length       = 0;
			c->cnt_err_crc          = 0;
			c->cnt_err_mii          = 0;
			c->cnt_drop_disabled    = 0;
			c->cnt_drop_filtered    = 0;

			c->cnt_erroneous           = c->cnt_drop - c->cnt_overflowed;
		}
	}

	if (s) {
		nfb_comp_read(comp, &buf.e1, sizeof(buf.e1), 0x0100);

		s->pkts                    = c ? c->cnt_total : RXMAC_READ_CNT(comp, PACKETS);

		s->CRCAlignErrors          = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, CRCAlignErrors);
		s->broadcastPkts           = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, broadcastPkts);
		s->multicastPkts           = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, multicastPkts);
		s->fragments               = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, fragments);
		s->jabbers                 = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, jabbers);
		s->octets                  = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, octets);
		s->pkts64Octets            = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, pkts64Octets);
		s->pkts65to127Octets       = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, pkts65to127Octets);
		s->pkts128to255Octets      = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, pkts128to255Octets);
		s->pkts256to511Octets      = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, pkts256to511Octets);
		s->pkts512to1023Octets     = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, pkts512to1023Octets);
		s->pkts1024to1518Octets    = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, pkts1024to1518Octets);

		s->underMinPkts            = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, undersize);
		s->overMaxPkts             = _NC_RXMAC_REG_BUFFER_PAIR(buf.e1, oversize);

		s->undersizePkts           = mac->has_counter_below_64 ? buf.e1.below64 : 0;
		s->oversizePkts            = buf.e1.over1518;
		s->pkts1519to2047Octets    = buf.e1.pkts1519to2047Octets;
		s->pkts2048to4095Octets    = buf.e1.pkts2048to4095Octets;
		s->pkts4096to8191Octets    = buf.e1.pkts4096to8191Octets;
		s->pktsOverBinsOctets      = buf.e1.pkts8192plusOctets;
	}

	if (c) {
		c->cnt_total_octets        = s ? s->octets : RXMAC_READ_CNT(comp, ES_OCTETS);
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
