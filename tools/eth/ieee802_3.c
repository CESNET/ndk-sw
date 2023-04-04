/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool - IEEE 802.3 registers
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jakub Cabal <cabal@cesnet.cz>
 */

#include <stdlib.h>
#include <string.h>

#include <linux/ethtool.h>

#include <netcope/ieee802_3.h>
#include "ieee802_3.h"


#define ET(base_name) (ETHTOOL_LINK_MODE_ ## base_name ## _BIT)

struct pma_pmd_type_t {
	uint16_t nr;		/* register value / bit number */
	const char *name;	/* name of PMA type */
	int link_mode;		/* ethtool link mode */
	unsigned int flags;	/* various flags */
};

/* PMA/PMD extended ability register table */
/* Item: MDIO reg 1.7 value, string identification, ETHTOOL_LINK_MODE bit, flags) */
static const struct pma_pmd_type_t ieee802_3_pma_pmd_type[] = {
	{0x00, "10GBASE-CX4",          -1, 0},
	{0x01, "10GBASE-EW",           -1, 0},
	{0x02, "10GBASE-LW",           -1, 0},
	{0x03, "10GBASE-SW",           -1, 0},
	{0x04, "10GBASE-LX4",          -1, 0},
	{0x05, "10GBASE-ER",           ET(10000baseER_Full), 0},
	{0x06, "10GBASE-LR",           ET(10000baseLR_Full), 0},
	{0x07, "10GBASE-SR",           ET(10000baseSR_Full), 0},
	{0x08, "10GBASE-LRM",          -1, 0},
	{0x09, "10GBASE-T",            ET(10000baseT_Full), 0},
	{0x0A, "10GBASE-KX4",          -1, 0},
	{0x0B, "10GBASE-KR",           ET(10000baseKR_Full), 0},
	{0x0C, "1000BASE-T",           -1, 0},
	{0x0D, "1000BASE-KX",          -1, 0},
	{0x0E, "100BASE-TX",           -1, 0},
	{0x0F, "10BASE-T",             -1, 0},
	{0x10, "10/1GBASE-PRX-D1",     -1, 0},
	{0x11, "10/1GBASE-PRX-D2",     -1, 0},
	{0x12, "10/1GBASE-PRX-D3",     -1, 0},
	{0x13, "10GBASE-PR-D1",        -1, 0},
	{0x14, "10GBASE-PR-D2",        -1, 0},
	{0x15, "10GBASE-PR-D3",        -1, 0},
	{0x16, "10/1GBASE-PRX-U1",     -1, 0},
	{0x17, "10/1GBASE-PRX-U2",     -1, 0},
	{0x18, "10/1GBASE-PRX-U3",     -1, 0},
	{0x19, "10GBASE-PR-U1",        -1, 0},
	{0x1A, "10GBASE-PR-U3",        -1, 0},
	{0x1C, "10GBASE-PR-D4",        -1, 0},
	{0x1D, "10/1GBASE-PRX-D4",     -1, 0},
	{0x1E, "10GBASE-PR-U4",        -1, 0},
	{0x1F, "10/1GBASE-PRX-U4",     -1, 0},
	{0x20, "40GBASE-KR4",          ET(40000baseKR4_Full), 0},
	{0x21, "40GBASE-CR4",          ET(40000baseCR4_Full), 0},
	{0x22, "40GBASE-SR4",          ET(40000baseSR4_Full), 0},
	{0x23, "40GBASE-LR4",          ET(40000baseLR4_Full), 0},
	{0x24, "40GBASE-FR",           -1, 0},
	{0x25, "40GBASE-ER4",          -1, 0},
	{0x26, "40GBASE-T",            -1, 0},
	{0x28, "100GBASE-CR10",        -1, 0},
	{0x29, "100GBASE-SR10",        -1, 0},
	{0x2A, "100GBASE-LR4",         ET(100000baseLR4_ER4_Full), 0},
	{0x2B, "100GBASE-ER4",         ET(100000baseLR4_ER4_Full), 0},
	{0x2C, "100GBASE-KP4",         -1,  4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x2D, "100GBASE-KR4",         ET(100000baseKR4_Full), 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x2E, "100GBASE-CR4",         ET(100000baseCR4_Full), 0}, /* Note: IEEE requires RSFEC */
	{0x2F, "100GBASE-SR4",         ET(100000baseSR4_Full), 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x30, "2.5GBASE-T",           ET(2500baseT_Full), 0},
	{0x31, "5GBASE-T",             ET(5000baseT_Full), 0},
	{0x32, "10GPASS-XR-D",         -1, 0},
	{0x33, "10GPASS-XR-U",         -1, 0},
	{0x34, "BASE-H",               -1, 0},
	{0x35, "25GBASE-LR",           -1, 0},
	{0x36, "25GBASE-ER",           -1, 0},
	{0x37, "25GBASE-T",            -1, 0},
	{0x38, "25GBASE-CR-S",         -1, IEEE802_3_FLAG_FEC_VARIANT},
	{0x38, "25GBASE-CR",           ET(25000baseCR_Full), (1 | IEEE802_3_FLAG_FEC_VARIANT | IEEE802_3_FLAG_FEC_MANDATORY)},
	{0x39, "25GBASE-KR",           ET(25000baseKR_Full), 0},
	{0x3A, "25GBASE-SR",           ET(25000baseSR_Full), 1 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x3B, "2.5GBASE-KX",          -1, 0},
	{0x3C, "5GBASE-KR",            -1, 0},
	{0x3D, "BASE-T1",              -1, 0},
	{0x40, "50GBASE-KR",           ET(50000baseKR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x41, "50GBASE-CR",           ET(50000baseCR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x42, "50GBASE-SR",           ET(50000baseSR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x43, "50GBASE-FR",           ET(50000baseLR_ER_FR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x44, "50GBASE-LR",           ET(50000baseLR_ER_FR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x45, "50GBASE-ER",           ET(50000baseLR_ER_FR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x46, "100GBASE-KR1",         -1, 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x47, "100GBASE-CR1",         -1, 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x48, "100GBASE-KR2",         ET(100000baseKR2_Full), 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x49, "100GBASE-CR2",         ET(100000baseCR2_Full), 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x4A, "100GBASE-SR2",         ET(100000baseSR2_Full), 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x4B, "100GBASE-DR",          -1, 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x50, "200GBASE-KR4",         ET(200000baseKR4_Full), 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x51, "200GBASE-CR4",         ET(200000baseCR4_Full), 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x52, "200GBASE-SR4",         ET(200000baseSR4_Full), 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x53, "200GBASE-DR4",         ET(200000baseDR4_Full), 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x54, "200GBASE-FR4",         ET(200000baseLR4_ER4_FR4_Full), 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x55, "200GBASE-LR4",         ET(200000baseLR4_ER4_FR4_Full), 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x56, "200GBASE-KR2",         -1, 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x57, "200GBASE-CR2",         -1, 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x58, "200GBASE-ER4",         -1, 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x59, "400GBASE-SR16",        -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x5A, "400GBASE-DR4",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x5B, "400GBASE-FR8",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x5C, "400GBASE-LR8",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x5D, "400GBASE-KR4",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x5E, "400GBASE-CR4",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x5F, "400GBASE-SR8",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x60, "400GBASE-SR4.2",       -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x61, "400GBASE-FR4",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x62, "400GBASE-LR4-6",       -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x63, "400GBASE-ER8",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x67, "10GBASE-BR10-D",       -1, 0},
	{0x68, "10GBASE-BR20-D",       -1, 0},
	{0x69, "10GBASE-BR40-D",       -1, 0},
	{0x6A, "10GBASE-BR10-U",       -1, 0},
	{0x6B, "10GBASE-BR20-U",       -1, 0},
	{0x6C, "10GBASE-BR40-U",       -1, 0},
	{0x6D, "25GBASE-BR10-D",       -1, 0},
	{0x6E, "25GBASE-BR20-D",       -1, 0},
	{0x6F, "25GBASE-BR40-D",       -1, 0},
	{0x70, "25GBASE-BR10-U",       -1, 0},
	{0x71, "25GBASE-BR20-U",       -1, 0},
	{0x72, "25GBASE-BR40-U",       -1, 0},
	{0x73, "25GBASE-BR10-D",       -1, 0},
	{0x74, "25GBASE-BR20-D",       -1, 0},
	{0x75, "25GBASE-BR40-D",       -1, 0},
	{0x76, "25GBASE-BR10-U",       -1, 0},
	{0x77, "25GBASE-BR20-U",       -1, 0},
	{0x78, "25GBASE-BR40-U",       -1, 0},
	{0x79, "100GBASE-VR1",         -1, 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x7A, "100GBASE-SR1",         -1, 4 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x7B, "200GBASE-VR2",         -1, 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x7C, "200GBASE-SR2",         -1, 8 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x7C, "400GBASE-VR4",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x7C, "400GBASE-SR4",         -1, 16 | IEEE802_3_FLAG_FEC_MANDATORY},
	{0x00, NULL,                   -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_ability_list[] = {
	{1, "10GBASE-EW",              -1, 0},
	{2, "10GBASE-LW",              -1, 0},
	{3, "10GBASE-SW",              -1, 0},
	{4, "10GBASE-LX4",             -1, 0},
	{5, "10GBASE-ER",              ET(10000baseER_Full), 0},
	{6, "10GBASE-LR",              ET(10000baseLR_Full), 0},
	{7, "10GBASE-SR",              ET(10000baseSR_Full), 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_extended_ability_list[] = {
	{0, "10GBASE-CX4",             -1, 0},
	{1, "10GBASE-LRM",             -1, 0},
	{2, "10GBASE-T",               ET(10000baseT_Full), 0},
	{3, "10GBASE-KX4",             -1, 0},
	{4, "10GBASE-KR",              ET(10000baseKR_Full), 0},
	{5, "1000BASE-T",              -1, 0},
	{6, "1000BASE-KX",             -1, 0},
	{7, "1000BASE-TX",             -1, 0},
	{8, "10BASE-T",                -1, 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_40g_100g_extended_ability_list[] = {
	{0, "40GBASE-KR4",             ET(40000baseKR4_Full), 0},
	{1, "40GBASE-CR4",             ET(40000baseCR4_Full), 0},
	{2, "40GBASE-SR4",             ET(40000baseSR4_Full), 0},
	{3, "40GBASE-LR4",             ET(40000baseLR4_Full), 0},
	{4, "40GBASE-FR",              -1, 0},
	{7, "100GBASE-SR4",            ET(100000baseSR4_Full), 0},
	{8, "100GBASE-CR10",           -1, 0},
	{9, "100GBASE-SR10",           -1, 0},
	{10, "100GBASE-LR4",           ET(100000baseLR4_ER4_Full), 0},
	{11, "100GBASE-ER4",           ET(100000baseLR4_ER4_Full), 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_25g_extended_ability_list[] = {
	{0, "25GBASE-KR-S",            ET(25000baseKR_Full), 0},
	{1, "25GBASE-KR",              ET(25000baseKR_Full), IEEE802_3_FLAG_FEC_MANDATORY},
	{2, "25GBASE-CR-S",            ET(25000baseCR_Full), 0},
	{3, "25GBASE-CR",              ET(25000baseCR_Full), IEEE802_3_FLAG_FEC_MANDATORY},
	{4, "25GBASE-SR",              ET(25000baseSR_Full), 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_200g_extended_ability_list[] = {
	{3, "200GBASE-DR4",            ET(200000baseDR4_Full), 0},
	{4, "200GBASE-FR4",            ET(200000baseLR4_ER4_FR4_Full), 0},
	{5, "200GBASE-LR4",            ET(200000baseLR4_ER4_FR4_Full), 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_400g_extended_ability_list[] = {
	{2, "400GBASE-SR16",           -1, 0},
	{3, "400GBASE-DR4",            -1, 0},
	{4, "400GBASE-FR8",            -1, 0},
	{5, "400GBASE-LR8",            -1, 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_50g_extended_ability_list[] = {
	{0, "50GBASE-KR",              ET(50000baseKR_Full), 0},
	{1, "50GBASE-CR",              ET(50000baseCR_Full), 0},
	{2, "50GBASE-SR",              ET(50000baseSR_Full), 2 | IEEE802_3_FLAG_FEC_MANDATORY},
	{3, "50GBASE-FR",              ET(50000baseLR_ER_FR_Full), 0},
	{4, "50GBASE-LR",              ET(50000baseLR_ER_FR_Full), 0},
	{-1, NULL,                     -1, 0},
};

static const struct pma_pmd_type_t ieee802_3_pma_pmd_40g_100g_extended_ability2_list[] = {
	{3, "100GBASE-DR",              -1, 0},
	{4, "100GBASE-FR1",             -1, 0},
	{5, "100GBASE-LR1",             -1, 0},
	{6, "100GBASE-ZR",              -1, 0},
	{7, "100GBASE-KR2",             -1, 0},
	{8, "100GBASE-CR2",             -1, 0},
	{9, "100GBASE-SR2",             -1, 0},
	{-1, NULL,                      -1, 0},
};


static inline struct pma_pmd_type_t const *_find_pma_pmd_type_by_string(struct pma_pmd_type_t const *table, const char *string)
{
	if (string == NULL)
		return NULL;

	while (table->name) {
		if (!strcmp(string, table->name))
			return table;
		table++;
	}
	return NULL;
}

static inline struct pma_pmd_type_t const *_find_pma_pmd_type_by_nr(const struct pma_pmd_type_t *table, int nr)
{
	while (table->name) {
		if (table->nr == nr)
			return table;
		table++;
	}
	return NULL;
}

const char *ieee802_3_get_pma_pmd_type_string(struct mdio_if_info *if_info)
{
	uint16_t reg, reg200;
	int reg200_r;

	int fec_enabled = 0, fec_mandatory;

	const struct pma_pmd_type_t *table = ieee802_3_pma_pmd_type;

	reg200_r = 0;

	reg = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 7);
	reg &= 0x7f;

	while ((table = _find_pma_pmd_type_by_nr(table, reg))) {
		if (table->flags & IEEE802_3_FLAG_FEC_VARIANT) {
			if (!reg200_r) {
				reg200 = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 200);
				reg200_r = 1;
				fec_enabled = (reg200 & (1 << 2) ? 1 : 0);
			}
			fec_mandatory = ((table->flags) & IEEE802_3_FLAG_FEC_MANDATORY) ? 1 : 0;

			if (fec_enabled == fec_mandatory) {
				return table->name;
			}
		} else {
			return table->name;
		}
	}

	return "Unknown";
}

int ieee802_3_set_pma_pmd_type_string(struct mdio_if_info *if_info, const char *string)
{
	int reg = -1;
	const struct pma_pmd_type_t *table = ieee802_3_pma_pmd_type;

	if (string == NULL)
		return -1;

	table = _find_pma_pmd_type_by_string(table, string);
	if (!table)
		return -1;

	if_info->mdio_write(if_info->dev, if_info->prtad, 1, 7, table->nr);

	if (table->flags & IEEE802_3_FLAG_FEC_VARIANT) {
		reg = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 200);
		reg = (table->flags & IEEE802_3_FLAG_FEC_MANDATORY) ? (reg | (1 << 2)) : (reg & ~(1 << 2));
		if_info->mdio_write(if_info->dev, if_info->prtad, 1, 200, reg);
	}
	return 0;
}

void ieee802_3_get_supported_pma_pmd_types_string(struct mdio_if_info *if_info, string_cb_t cb, void *cb_priv)
{
	#define ST_TABLES_COUNT 8
	const int ext_abilities_hotfix = 1;

	uint16_t reg[ST_TABLES_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0,};
	uint16_t reg_pma_ea2 = 0;
	int have_caps[ST_TABLES_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0,};
	const struct pma_pmd_type_t *tables[ST_TABLES_COUNT] = {
		ieee802_3_pma_pmd_ability_list,
		ieee802_3_pma_pmd_extended_ability_list,
		ieee802_3_pma_pmd_40g_100g_extended_ability_list,
		ieee802_3_pma_pmd_25g_extended_ability_list,
		ieee802_3_pma_pmd_200g_extended_ability_list,
		ieee802_3_pma_pmd_400g_extended_ability_list,
		ieee802_3_pma_pmd_50g_extended_ability_list,
		ieee802_3_pma_pmd_40g_100g_extended_ability2_list
	};

	const struct pma_pmd_type_t *table;

	int i;

	reg[0] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 8);
	have_caps[0] = 1;
	have_caps[1] = reg[0] & (1 << 9);

	if (have_caps[1] || ext_abilities_hotfix) {
		reg[1] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 11);
		have_caps[2] = reg[1] & (1 << 10);
		have_caps[3] = reg[1] & (1 << 12);
		have_caps[4] = reg[1] & (1 << 13);
		have_caps[5] = reg[1] & (1 << 13);
		have_caps[7] = reg[1] & (1 << 10);
		// read PMA/PMD extended ability 2
		reg_pma_ea2 = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 25);
		have_caps[6] = reg_pma_ea2 & (1 << 0);

		if (have_caps[2] || ext_abilities_hotfix) {
			reg[2] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 13);
		}
		if (have_caps[3] || ext_abilities_hotfix) {
			reg[3] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 19);
		}
		if (have_caps[4] || ext_abilities_hotfix) {
			reg[4] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 23);
		}
		if (have_caps[5] || ext_abilities_hotfix) {
			reg[5] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 24);
		}
		if (have_caps[6] || ext_abilities_hotfix) {
			reg[6] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 20);
		}
		if (have_caps[7] || ext_abilities_hotfix) {
			reg[7] = if_info->mdio_read(if_info->dev, if_info->prtad, 1, 26);
		}

	}

	for (i = 0; i < ST_TABLES_COUNT; i++) {
		table = tables[i];
		while (table->name) {
			if (reg[i] & (1 << table->nr)) {
				cb(cb_priv, table->name);
			}
			table++;
		}
	}
}

const char *ieee802_3_get_speed_string(int val)
{
	switch (val) {
	case 10:        return "10 Mb/s";
	case 100:       return "100 Mb/s";
	case 1000:      return "1 Gb/s";
	case 10000:     return "10 Gb/s";
	case 25000:     return "25 Gb/s";
	case 40000:     return "40 Gb/s";
	case 50000:     return "50 Gb/s";
	case 100000:    return "100 Gb/s";
	case 200000:    return "200 Gb/s";
	case 400000:    return "400 Gb/s";
	default:        return "Unknown";
	}
}

const char *ieee802_3_get_pma_speed_string(struct mdio_if_info *if_info)
{
	int val = ieee802_3_get_pma_speed_value(if_info);
	return ieee802_3_get_speed_string(val);
}

const char *ieee802_3_get_pcs_speed_string(struct mdio_if_info *if_info)
{
	int val = ieee802_3_get_pcs_speed_value(if_info);
	return ieee802_3_get_speed_string(val);
}

/* PCS number of lines based on speed type "hackaround" for our cards */
int ieee802_3_get_pcs_lines(struct mdio_if_info *if_info)
{
	int reg;
	const int mask = IEEE802_3_SS_MSB | IEEE802_3_SS_LSB;

	reg = if_info->mdio_read(if_info->dev, if_info->prtad, 3, 0);
	if (reg < 0)
		return -1;

	if ((reg & mask) == mask) {
		switch ((reg >> 2) & 0xF) {
			case 0: return 1;
			case 3: return 4;
			case 4: return 20;
			case 5: return 1;
			case 6: return 4;
			case 9: return 8;
			case 10: return 16;
			default: return -1;
		}
	} else {
		return -1;
	}
}

const char *ieee802_3_get_pcs_pma_link_status_string(struct mdio_if_info *if_info, int devad)
{
	int reg;
	reg = if_info->mdio_read(if_info->dev, if_info->prtad, devad, 1);
	if (reg < 0)
		return "Unknown";
	return (reg & 0x4) ? "UP" : "DOWN";
}

int ieee802_3_get_fec_lines(const char *string)
{
	const struct pma_pmd_type_t *t = _find_pma_pmd_type_by_string(ieee802_3_pma_pmd_type, string);
	if (!t)
		return -1;
	return t->flags & IEEE802_3_FLAG_LINES_MASK;
}
