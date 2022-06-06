/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network interface driver of the NFB platform - ethtool support
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#include <libfdt.h>

#include "../nfb.h"

#include "net.h"

#include <linux/pci.h>
#include <linux/mdio.h>
#include <linux/netdevice.h>

#include <netcope/mdio.h>
#include <netcope/transceiver.h>
#include <netcope/i2c_ctrl.h>


enum nfb_net_device_stat_type {
	NETDEV_STAT,
	RXMAC_COUNTER,
	TXMAC_COUNTER,
	RXMAC_ETHERSTAT,
};

struct nfb_net_device_stat {
	char stat_string[ETH_GSTRING_LEN];
	enum nfb_net_device_stat_type stat_type;
	int sizeof_stat;
	int stat_offset;
};

#define NFB_NET_NETDEV_STAT(m)		NETDEV_STAT, \
	sizeof(((struct rtnl_link_stats64 *) 0)->m), \
		offsetof(struct rtnl_link_stats64, m)
#define NFB_NET_RXMAC_COUNTER(m)	RXMAC_COUNTER, \
	sizeof(((struct nc_rxmac_counters *) 0)->m), \
		offsetof(struct nc_rxmac_counters, m)
#define NFB_NET_TXMAC_COUNTER(m)	TXMAC_COUNTER, \
	sizeof(((struct nc_txmac_counters *) 0)->m), \
		offsetof(struct nc_txmac_counters, m)
#define NFB_NET_RXMAC_ETHERSTAT(m)	RXMAC_ETHERSTAT, \
	sizeof(((struct nc_rxmac_etherstats *) 0)->m), \
		offsetof(struct nc_rxmac_etherstats, m)

static const struct nfb_net_device_stat nfb_net_device_stats[] = {
	{"rx_packets",                          NFB_NET_NETDEV_STAT(rx_packets)},
	{"tx_packets",                          NFB_NET_NETDEV_STAT(tx_packets)},
	{"rx_bytes",                            NFB_NET_NETDEV_STAT(rx_bytes)},
	{"tx_bytes",                            NFB_NET_NETDEV_STAT(tx_bytes)},
	{"rx_errors",                           NFB_NET_NETDEV_STAT(rx_errors)},
	{"tx_errors",                           NFB_NET_NETDEV_STAT(tx_errors)},
	{"rx_dropped",                          NFB_NET_NETDEV_STAT(rx_dropped)},
	{"tx_dropped",                          NFB_NET_NETDEV_STAT(tx_dropped)},

	{"rxmac_received_octets",               NFB_NET_RXMAC_COUNTER(cnt_octets)},
	{"rxmac_processed",                     NFB_NET_RXMAC_COUNTER(cnt_total)},
	{"rxmac_received",                      NFB_NET_RXMAC_COUNTER(cnt_received)},
	{"rxmac_erroneous",                     NFB_NET_RXMAC_COUNTER(cnt_erroneous)},
	{"rxmac_overflowed",                    NFB_NET_RXMAC_COUNTER(cnt_overflowed)},

	{"rxeth_octets",                        NFB_NET_RXMAC_ETHERSTAT(octets)},
	{"rxeth_pkts",                          NFB_NET_RXMAC_ETHERSTAT(pkts)},
	{"rxeth_broadcastPkts",                 NFB_NET_RXMAC_ETHERSTAT(broadcastPkts)},
	{"rxeth_multicastPkts",                 NFB_NET_RXMAC_ETHERSTAT(multicastPkts)},
	{"rxeth_CRCAlignErrors",                NFB_NET_RXMAC_ETHERSTAT(CRCAlignErrors)},
	{"rxeth_undersizePkts",                 NFB_NET_RXMAC_ETHERSTAT(undersizePkts)},
	{"rxeth_oversizePkts",                  NFB_NET_RXMAC_ETHERSTAT(oversizePkts)},
	{"rxeth_fragments",                     NFB_NET_RXMAC_ETHERSTAT(fragments)},
	{"rxeth_jabbers",                       NFB_NET_RXMAC_ETHERSTAT(jabbers)},
	{"rxeth_pkts64Octets",                  NFB_NET_RXMAC_ETHERSTAT(pkts64Octets)},
	{"rxeth_pkts65to127Octets",             NFB_NET_RXMAC_ETHERSTAT(pkts65to127Octets)},
	{"rxeth_pkts128to255Octets",            NFB_NET_RXMAC_ETHERSTAT(pkts128to255Octets)},
	{"rxeth_pkts256to511Octets",            NFB_NET_RXMAC_ETHERSTAT(pkts256to511Octets)},
	{"rxeth_pkts512to1023Octets",           NFB_NET_RXMAC_ETHERSTAT(pkts512to1023Octets)},
	{"rxeth_pkts1024to1518Octets",          NFB_NET_RXMAC_ETHERSTAT(pkts1024to1518Octets)},

	{"txmac_transmitted_octets",            NFB_NET_TXMAC_COUNTER(cnt_octets)},
	{"txmac_processed",                     NFB_NET_TXMAC_COUNTER(cnt_total)},
	{"txmac_transmitted",                   NFB_NET_TXMAC_COUNTER(cnt_sent)},
	{"txmac_erroneous",                     NFB_NET_TXMAC_COUNTER(cnt_erroneous)},
};

#define NFB_NET_QUEUES_STATS_LEN 0	// TODO: add queues stats too
#define NFB_NET_DEVICE_STATS_LEN ARRAY_SIZE(nfb_net_device_stats)
#define NFB_NET_STATS_LEN (NFB_NET_DEVICE_STATS_LEN + NFB_NET_QUEUES_STATS_LEN)


static void nfb_net_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct nfb_device *nfbdev = priv->nfbdev;

	int fdt_offset, proj_len, rev_len;
	const char *proj_str, *rev_str;

	fdt_offset = fdt_path_offset(nfbdev->fdt, "/firmware/");
	proj_str = fdt_getprop(nfbdev->fdt, fdt_offset, "project-name", &proj_len);
	rev_str = fdt_getprop(nfbdev->fdt, fdt_offset, "build-revision", &rev_len);

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, PACKAGE_VERSION, sizeof(drvinfo->version));

	if (proj_len > 0) {
		strlcpy(drvinfo->fw_version, proj_str, sizeof(drvinfo->fw_version));
		strlcat(drvinfo->fw_version, " ", sizeof(drvinfo->fw_version));
	}

	if (rev_len > 0) {
		strlcat(drvinfo->fw_version, rev_str, sizeof(drvinfo->fw_version));
	}

	strlcpy(drvinfo->bus_info, pci_name(nfbdev->pci), sizeof(drvinfo->bus_info));

	drvinfo->n_stats = NFB_NET_STATS_LEN;
}


static void nfb_net_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	char *p = (char *) data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < NFB_NET_DEVICE_STATS_LEN; i++) {
			memcpy(p, nfb_net_device_stats[i].stat_string, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		BUG_ON(p - (char *) data != NFB_NET_DEVICE_STATS_LEN * ETH_GSTRING_LEN);
		break;
	}
}


static int nfb_net_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return NFB_NET_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}


static void nfb_net_get_ethtool_stats(struct net_device *netdev, struct ethtool_stats *stats, u64 *data)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	const struct rtnl_link_stats64 *netdev_stats;
	struct rtnl_link_stats64 netdev_stats_temp = {0};

	struct nc_rxmac_counters rxmac_counters = {0};
	struct nc_txmac_counters txmac_counters = {0};
	struct nc_rxmac_etherstats rxmac_etherstats = {0};

	char *p = NULL;
	int i;

	netdev_stats = dev_get_stats(netdev, &netdev_stats_temp);
	if (priv->nc_rxmac)
		nc_rxmac_read_counters(priv->nc_rxmac, &rxmac_counters, &rxmac_etherstats);
	if (priv->nc_txmac) {
		nc_txmac_read_counters(priv->nc_txmac, &txmac_counters);
	}

	for (i = 0; i < NFB_NET_DEVICE_STATS_LEN; i++) {
		switch (nfb_net_device_stats[i].stat_type) {
		case NETDEV_STAT:
			p = (char *) netdev_stats + nfb_net_device_stats[i].stat_offset;
			break;
		case RXMAC_COUNTER:
			p = (char *) &rxmac_counters + nfb_net_device_stats[i].stat_offset;
			break;
		case TXMAC_COUNTER:
			p = (char *) &txmac_counters + nfb_net_device_stats[i].stat_offset;
			break;
		case RXMAC_ETHERSTAT:
			p = (char *) &rxmac_etherstats + nfb_net_device_stats[i].stat_offset;
			break;
		default:
			data[i] = 0;
			continue;
		}

		data[i] = (nfb_net_device_stats[i].sizeof_stat == sizeof(u64)) ? *(u64 *) p : *(u32 *) p;
	}
}


static int nfb_net_get_module_info(struct net_device *netdev, struct ethtool_modinfo *modinfo)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (!priv->nc_trstat)
		return -EIO;

	if (!nc_transceiver_statusreg_is_present(priv->nc_trstat))
		return -EIO;

	// TODO: check transceiver type!
	// TODO: expect QSFP28->I2C->SFF_8636

	if (!priv->nc_tri2c)
		return -EIO;

	modinfo->type = ETH_MODULE_SFF_8636;
	modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;

	return 0;
}


static int nfb_net_get_module_eeprom(struct net_device *netdev, struct ethtool_eeprom *ee, u8 *data)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct nc_i2c_ctrl *comp_i2c = priv->nc_tri2c;
	u8 databyte = 0xFF;
	int status, i;

	if (ee->len == 0)
		return -EINVAL;

	if (!priv->nc_trstat)
		return -EIO;

	if (!nc_transceiver_statusreg_is_present(priv->nc_trstat))
		return -EIO;

	// TODO: check transceiver type!
	// TODO: expect QSFP28->I2C->SFF_8636

	if (ee->offset + ee->len > ETH_MODULE_SFF_8636_LEN)
		return -EINVAL;

	if (!priv->nc_tri2c)
		return -EIO;

	nc_i2c_set_addr(comp_i2c, 0xA0);
	for (i = ee->offset; i < ee->offset + ee->len; i++) {
		status = nc_i2c_read_reg(comp_i2c, i, &databyte, 1);
		if (status != 1)
			return -EIO;

		data[i-ee->offset] = databyte;
	}

	return 0;
}


static __u32 nfb_net_mdio_get_speed(struct mdio_if_info *mdio)
{
	const int SS_LSB = 0x2000;
	const int SS_MSB = 0x0040;

	int speedreg = mdio->mdio_read(mdio->dev, mdio->prtad, 1, 0);

	if (speedreg < 0)
		return SPEED_UNKNOWN;

	if ((speedreg & (SS_LSB | SS_MSB)) == (SS_LSB | SS_MSB)) {
		switch ((speedreg >> 2) & 0xF) {
			case 0: return 10000;
			case 2: return 40000;
			case 3: return 100000;
			case 4: return 25000;
			case 5: return 50000;
			default:
				return SPEED_UNKNOWN;
		}
	}

	if (speedreg & SS_MSB)
		return 1000;
	else if (speedreg & SS_LSB)
		return 100;
	else
		return 10;
}


#ifdef CONFIG_HAS_LINK_KSETTINGS
static void nfb_net_mdio_get_pma_types(struct mdio_if_info *mdio,
		struct ethtool_link_ksettings *link_ksettings)
{
	// PMA/PMD extended ability register table: 1.7
	enum nfb_net_pma_type {
		PMA_10GBASE_ER = 0x05,
		PMA_10GBASE_LR = 0x06,
		PMA_10GBASE_SR = 0x07,
		PMA_40GBASE_SR4 = 0x22,
		PMA_40GBASE_LR4 = 0x23,
		PMA_40GBASE_ER4 = 0x25,
		PMA_100GBASE_LR4 = 0x2A,
		PMA_100GBASE_ER4 = 0x2B,
		PMA_100GBASE_SR4 = 0x2F,
	};

	// PMA/PMD extended ability register: 1.7
	enum nfb_net_pma_ext_type {
		PMAE_100GBASE_SR4 = 1 << 7,
		PMAE_100GBASE_LR4 = 1 << 10,
		PMAE_100GBASE_ER4 = 1 << 11,
	};

	int pma_type_reg = mdio->mdio_read(mdio->dev, mdio->prtad, 1, 7);
	int pma_type_ext_reg = mdio->mdio_read(mdio->dev, mdio->prtad, 1, 13);

	if (pma_type_reg < 0)
		return;

	switch ((pma_type_reg) & 0xFF) {
		case PMA_10GBASE_ER:
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 10000baseER_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, 10000baseER_Full);
			break;
		case PMA_10GBASE_LR:
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 10000baseLR_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, 10000baseLR_Full);
			break;
		case PMA_10GBASE_SR:
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 10000baseSR_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, 10000baseSR_Full);
			break;
		case PMA_100GBASE_LR4:
		case PMA_100GBASE_ER4:
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 100000baseLR4_ER4_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, 100000baseLR4_ER4_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, FEC_NONE);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, FEC_NONE);
			break;
		case PMA_100GBASE_SR4:
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 100000baseSR4_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, 100000baseSR4_Full);
			ethtool_link_ksettings_add_link_mode(link_ksettings, supported, FEC_RS);
			ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, FEC_RS);
			break;
	}

	if (pma_type_ext_reg < 0)
		return;

	if (pma_type_ext_reg & PMAE_100GBASE_SR4) {
		ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 100000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(link_ksettings, supported, FEC_RS);
	}

	if (pma_type_ext_reg & (PMAE_100GBASE_LR4 | PMAE_100GBASE_ER4)) {
		ethtool_link_ksettings_add_link_mode(link_ksettings, supported, 100000baseLR4_ER4_Full);
		ethtool_link_ksettings_add_link_mode(link_ksettings, supported, FEC_NONE);
	}
}


static int nfb_net_get_link_ksettings(struct net_device *netdev,
		struct ethtool_link_ksettings *link_ksettings)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct mdio_if_info *mdio = &priv->mdio;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);

	link_ksettings->base.duplex = DUPLEX_FULL;
	link_ksettings->base.mdio_support = mdio->mode_support;
	link_ksettings->base.speed = nfb_net_mdio_get_speed(mdio);

	link_ksettings->base.port = PORT_FIBRE;
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, FIBRE);

	link_ksettings->base.autoneg = AUTONEG_DISABLE;
	ethtool_link_ksettings_del_link_mode(link_ksettings, supported, Autoneg);
	ethtool_link_ksettings_del_link_mode(link_ksettings, advertising, Autoneg);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, lp_advertising);

	nfb_net_mdio_get_pma_types(&priv->mdio, link_ksettings);

	return 0;
}
#else
static int nfb_net_get_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	struct nfb_net_device *priv = netdev_priv(netdev);
	struct mdio_if_info *mdio = &priv->mdio;

	cmd->port = PORT_FIBRE;
	cmd->duplex = DUPLEX_FULL;
	cmd->autoneg = AUTONEG_DISABLE;
	cmd->supported = SUPPORTED_FIBRE;
	cmd->advertising = ADVERTISED_FIBRE;
	cmd->mdio_support = priv->mdio.mode_support;

	ethtool_cmd_speed_set(cmd, nfb_net_mdio_get_speed(mdio));

	return 0;
}
#endif


static void nfb_net_get_channels(struct net_device *netdev, struct ethtool_channels *channels)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	channels->max_rx = priv->module->rxqc;
	channels->max_tx = priv->module->txqc;
	channels->rx_count = priv->rxqs_count;
	channels->tx_count = priv->txqs_count;
}


static int nfb_net_set_channels(struct net_device *netdev, struct ethtool_channels *channels)
{
	struct nfb_net_device *priv = netdev_priv(netdev);

	if (channels->combined_count)
		return -EINVAL;

	if (channels->other_count)
		return -EINVAL;

	if (channels->rx_count > priv->module->rxqc)
		return -EINVAL;

	if (channels->tx_count > priv->module->txqc)
		return -EINVAL;

	if (netif_running(netdev))
		return -EBUSY;

	priv->rxqs_count = channels->rx_count;
	priv->txqs_count = channels->tx_count;

	return 0;
}


static const struct ethtool_ops nfb_net_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_drvinfo = nfb_net_get_drvinfo,
	.get_module_info = nfb_net_get_module_info,
	.get_module_eeprom = nfb_net_get_module_eeprom,
#ifdef CONFIG_HAS_LINK_KSETTINGS
	.get_link_ksettings = nfb_net_get_link_ksettings,
#else
	.get_settings = nfb_net_get_settings,
#endif
	.get_strings = nfb_net_get_strings,
	.get_sset_count = nfb_net_get_sset_count,
	.get_ethtool_stats = nfb_net_get_ethtool_stats,
	.get_channels = nfb_net_get_channels,
	.set_channels = nfb_net_set_channels,
};


void nfb_net_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &nfb_net_ethtool_ops;
}
