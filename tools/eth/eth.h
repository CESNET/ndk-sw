/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Ethernet interface configuration tool - private header
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFBTOOL_ETH_H
#define NFBTOOL_ETH_H

#include <stdbool.h>
#include <inttypes.h>

#include <nfb/nfb.h>

#include <netcope/ni.h>
#include <netcope/rxmac.h>
#include <netcope/txmac.h>
#include <netcope/i2c_ctrl.h>

#include "mdio.h"


// this enum need to corespond with queries[] array
enum queries {
	RX_STATUS,
	RX_OCTETS,
	RX_PROCESSED,
	RX_ERRONEOUS,
	RX_LINK,
	RX_RECEIVED,
	RX_OVERFLOWED,
	TX_STATUS,
	TX_OCTETS,
	TX_PROCESSED,
	TX_ERRONEOUS,
	TX_TRANSMITTED,
	PMA_TYPE,
	PMA_SPEED,
};

static const char * const queries[] = {
	"rx_status",
	"rx_octets",
	"rx_processed",
	"rx_erroneous",
	"rx_link",
	"rx_received",
	"rx_overflowed",
	"tx_status",
	"tx_octets",
	"tx_processed",
	"tx_erroneous",
	"tx_transmitted",
	"pma_type",
	"pma_speed",
};

enum commands {
	CMD_PRINT_STATUS,
	CMD_PRINT_SPEED,
	CMD_USAGE,
	CMD_ENABLE,
	CMD_RESET,
	CMD_SET_ERROR_MASK,
	CMD_SET_PMA_TYPE,
	CMD_SET_PMA_FEATURE,
	CMD_SET_MAX_LENGTH,
	CMD_SET_MIN_LENGTH,
	CMD_SET_REPEATER,
	CMD_MAC_CHECK_MODE,
	CMD_SHOW_MACS,
	CMD_CLEAR_MACS,
	CMD_FILL_MACS,
	CMD_ADD_MAC,
	CMD_REMOVE_MAC,
	CMD_QUERY,
};

struct eth_params {
	enum commands command;
	long param;
	int index;
	int verbose;
	bool ether_stats;
	unsigned long long mac_address;
	const char *string;
};

struct mdio_if_mdev {
	struct nc_mdio *mdio;
	int mdev;
};

int rxmac_execute_operation(struct ni_context *ctx, struct nc_rxmac *rxmac, struct eth_params *p);
int txmac_execute_operation(struct ni_context *ctx, struct nc_txmac *txmac, struct eth_params *p);
int pcspma_execute_operation(struct ni_context *ctx, struct nfb_device *dev, int eth_node, struct eth_params *p);
int transceiver_execute_operation(struct nfb_device *dev, int node_transceiver, struct eth_params *p);
int transceiver_execute_operation_for_eth(struct nfb_device *dev, int node_eth, struct eth_params *p);
int transceiver_print(struct ni_context *ctx, struct nfb_device *dev, int transceiver_node, int index);
void transceiver_print_short_info(struct ni_context *ctx, struct nfb_device *dev, int node, struct eth_params *p);

int print_json_qsfp_i2c_text(void *, int item, struct nc_i2c_ctrl*);
int print_user_qsfp_i2c_text(void *, int item, struct nc_i2c_ctrl*);
int print_mdio_text_user(void *priv, int item, struct mdio_if_mdev *mdio_if);
int print_mdio_text_json(void *priv, int item, struct mdio_if_mdev *mdio_if);


int query_print(const void *fdt, int node, char *queries, int size,
	struct nfb_device *dev, int index);

static inline struct mdio_if_info nfb_eth_create_mdio_info(struct nc_mdio *mdio, int port_address)
{
	return (struct mdio_if_info) {
		.mdio_read = (mdio_read_t) nc_mdio_read,
		.mdio_write = (mdio_write_t) nc_mdio_write,
		.dev = (mdio_if_info_priv_t) mdio,
		.prtad = port_address,
	};
}


enum NI_ITEMS {
	NI_SEC_ROOT = 0,
	NI_LIST_ETH,
	NI_SEC_ETH,
	NI_SEC_ETH_ID,

	NI_ETH_REPEATER,
	NI_SEC_PMA,
	NI_PMA_LINK_STA0,
	NI_PMA_LINK_STA1,
	NI_PMA_SPEED,
	NI_PMA_TFAULT,
	NI_PMA_RFAULT,
	NI_PMA_TYPE,
	NI_LIST_PMA_TYPES_AV,

	NI_SEC_PMA_TYPES,
	NI_PMA_TYPES_NAME,
	NI_PMA_TYPES_ACTIVE,

	NI_LIST_PMA_FEATS_AV,
	NI_SEC_PMA_FEAT,
	NI_PMA_FEAT_NAME,
	NI_PMA_FEAT_ACTIVE,
	NI_SEC_PCS,
	NI_PCS_LINK_STA0,
	NI_PCS_LINK_STA1,
	NI_PCS_SPEED,
	NI_PCS_TFAULT,
	NI_PCS_RFAULT,

	NI_LIST_PCS_FEATS_AV,
	NI_PCS_GLB_BLK_LCK0,
	NI_PCS_GLB_BLK_LCK1,

	NI_PCS_GLB_HIGH_BER0,
	NI_PCS_GLB_HIGH_BER1,
	NI_PCS_BER_CNT,
	NI_PCS_LANES_ALIGNED,
	NI_PCS_BLK_ERR,
	NI_LIST_PCS_BLK_LCKS,
	NI_PCS_BLK_LCK,
	NI_LIST_AM_LCKS,
	NI_PCS_AM_LCK,
	NI_LIST_LANE_MAP,
	NI_PCS_LANE_MAP,
	NI_LIST_BIP_ERR_CNT,
	NI_BIP_ERR_CNT,

	NI_SEC_RXMAC,

	NI_MAC_TOTAL,
	NI_MAC_TOTAL_O,
	NI_MAC_DROP,

	NI_RXM_ENABLED,
	NI_RXM_LINK,
	NI_RXM_HFIFO_OVF,
	NI_RXM_PASS_O,
	NI_RXM_PASS,
	NI_RXM_OVERFLOWED,

	NI_SEC_RXMAC_CONF,
	NI_RXM_ERR_MASK_REG,
	NI_RXM_ERR_FRAME,
	NI_RXM_ERR_CRC,
	NI_RXM_ERR_MIN_LEN,
	NI_RXM_MIN_LEN,
	NI_RXM_ERR_MAX_LEN,
	NI_RXM_MAX_LEN,
	NI_RXM_MAX_LEN_CAP,
	NI_RXM_ERR_MAC_CHECK,
	NI_RXM_ERR_MAC_MODE,
	NI_RXM_MAC_MAX_COUNT,

	NI_SEC_MAC_S,
	NI_SEC_RXMAC_ES,
	NI_RXM_ES_OCTS,
	NI_RXM_ES_PKTS,
	NI_RXM_ES_BCST,
	NI_RXM_ES_MCST,
	NI_RXM_ES_CRCE,
	NI_RXM_ES_UNDR,
	NI_RXM_ES_OVER,
	NI_RXM_ES_FRAG,
	NI_RXM_ES_JABB,
	NI_RXM_ES_64,
	NI_RXM_ES_65_127,
	NI_RXM_ES_128_255,
	NI_RXM_ES_256_511,
	NI_RXM_ES_512_1023,
	NI_RXM_ES_1024_1518,
	NI_RXM_ES_1519_2047,
	NI_RXM_ES_2048_4095,
	NI_RXM_ES_4096_8191,
	NI_RXM_ES_OVER_BINS,
	NI_RXM_ES_UNDR_SET,
	NI_RXM_ES_OVER_SET,

	NI_SEC_TXMAC,
	NI_TXM_ENABLED,
	NI_TXM_PASS_O,
	NI_TXM_PASS,
	NI_TXM_DROP,

	NI_MAC_DROP_DISABLED,
	NI_MAC_DROP_FILTERED,
	NI_MAC_DROP_LINK,
	NI_MAC_DROP_ERR,
	NI_MAC_DROP_ERR_LEN,
	NI_MAC_DROP_ERR_CRC,
	NI_MAC_DROP_ERR_MII,

	NI_TRANS_PRSNT,
	NI_TRANS_PRSNT_UNK,
	NI_TRANS_CAGE_TYPE,
	NI_TRANS_CAGE_ID,
	NI_LIST_TRN_LANES,
	NI_TRANS_LANE,

	NI_LIST_TRN_RX_IN_PWR,
	NI_TRANS_RX_IN_PWR_L,
	NI_TRANS_RX_IN_PWR_V,
	NI_LIST_TRN_STX_DIS,
	NI_TRANS_STX_DIS_L,
	NI_TRANS_STX_DIS_V,

	NI_LIST_TRANS,
	NI_SEC_TRN,
	NI_TRN_NAME,
	NI_TRN_INDEX,
	NI_MOD_IDENT,
	NI_SFF8636_TEMP,
	NI_SFF8636_VNDR_NAME,
	NI_SFF8636_VNDR_SN,
	NI_SFF8636_VNDR_PN,
	NI_SFF8636_REVISION,
	NI_SFF8636_WL,
	NI_SFF8636_WL_TOL,
	NI_TRN_COMPLIANCE,
	NI_TRN_CONNECTOR,
	NI_TRN_CMIS_VER_MAJ,
	NI_TRN_CMIS_VER_MIN,
	NI_TRN_CMIS_GLB_STAT,
	NI_TRN_CMIS_VNDR_NAME,
	NI_TRN_CMIS_VNDR_SN,
	NI_TRN_CMIS_VNDR_PN,
	NI_TRN_CMIS_MED_T,
	NI_TRN_CMIS_IFC_T,

	NI_MDIO_VNDR_NAME,
	NI_MDIO_SN,
	NI_MDIO_PN,
	NI_MDIO_HW_REV,
	NI_MDIO_MGMT_REV,

	NI_SEC_RSFEC_STATUS,
	NI_SEC_RSFEC119_STATUS,
	NI_RSFEC_STATUS_BCA,
	NI_RSFEC_STATUS_BIA,
	NI_RSFEC_STATUS_SER,
	NI_RSFEC_STATUS_FLA,
	NI_RSFEC_STATUS_PLA,
	NI_RSFEC_STATUS_DSER,
	NI_RSFEC_STATUS_RDSER,
	NI_RSFEC_STATUS_LDSER,
	NI_RSFEC_CORRECTED,
	NI_RSFEC_UNCORRECTED,
	NI_LIST_RSFEC_SYM_ERR,
	NI_RSFEC_SYM_ERR_L,
	NI_RSFEC_SYM_ERR_V,

	NI_LIST_RSFEC_LANE_MAP,
	NI_RSFEC_LANE_MAP,
	NI_LIST_RSFEC_AM_LOCK,
	NI_RSFEC_AM_LOCK,
};

struct ni_eth_item_f_t {
	struct ni_common_item_callbacks c;
	int (*print_ctrl_reg)(void *, int, int);
	int (*print_qsfp_i2c_text)(void *, int, struct nc_i2c_ctrl*);
	int (*print_pwr)(void *, int, double);
	int (*print_mdio_text)(void *, int, struct mdio_if_mdev *mdio);
};

NI_DEFAULT_ITEMS(ni_eth_item_f_t, c.)

#define NI_ETH_ITEM(name, type, cbcall) NI_ITEM_CB(name, type, ni_eth_item_f_t, cbcall)

NI_ETH_ITEM(ctrl_reg, int, print_ctrl_reg)
NI_ETH_ITEM(qsfp_i2c_text, struct nc_i2c_ctrl *, print_qsfp_i2c_text)
NI_ETH_ITEM(mdio_text, struct mdio_if_mdev*, print_mdio_text)
NI_ETH_ITEM(pwr, double, print_pwr)

#endif /* NFBTOOL_ETH_H */
