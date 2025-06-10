/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Ethernet interface configuration tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include <nfb/nfb.h>
#include <libfdt.h>

#include <netcope/idcomp.h>
#include <netcope/nccommon.h>

#include "eth.h"

#define ARGUMENTS	":hd:i:q:e:rtRSl:L:p:m:u:a:c:M:ojvPT"

#define RXMAC       1
#define TXMAC       2
#define PCSPMA      4
#define TRANSCEIVER 8


#define NUF_N   (NI_USER_ITEM_F_NO_NEWLINE)
#define NUF_NDA (NI_USER_ITEM_F_NO_NEWLINE | NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN)
#define NUF_DA  (NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN)
#define NUF_VE  (NI_USER_LIST_F_NO_VALUE | NI_USER_LIST_F_ENDLINE)
#define NUF_SL  (NI_USER_ITEM_F_SEC_LABEL)

#define NUFA(x) ni_user_f_align(x)
#define NUFW(x) ni_user_f_width(x)
#define NUFD(x) ni_user_f_decim(x)
#define NUFC    NUFW(20)

#define NJFD(x) ni_json_f_decim(x)

static struct ni_context_item_default ni_items[] = {
	[NI_SEC_ROOT]           = {ni_json_e,                           ni_user_n},
	[NI_LIST_ETH]           = {ni_json_k("eth"),                    ni_user_v(NULL, 0, "\n", NULL)}, /* FIXME: no newline for 128 */
	[NI_SEC_ETH]            = {ni_json_e,                           ni_user_f("Ethernet interface", NUF_SL | NUFW(-4))},
	[NI_SEC_ETH_ID]         = {ni_json_k("id"),                     ni_user_v(" ", NUF_NDA | NUF_SL | NUFW(0), NULL, NULL)},
	[NI_SEC_PMA]            = {ni_json_k("pma"),                    ni_user_l("PMA regs")},
	[NI_PMA_LINK_STA0]      = {ni_json_k("link_status_latch"),      ni_user_f("Link status", NUF_N | NUFW(-4))},
	[NI_PMA_LINK_STA1]      = {ni_json_k("link_status"),            ni_user_v(NULL, NUF_DA | NUFW(-4), " | ", NULL)},
	[NI_PMA_SPEED]          = {ni_json_k("speed_str"),              ni_user_l("Speed")},
	[NI_SEC_PCS]            = {ni_json_k("pcs"),                    ni_user_l("PCS regs")},
	[NI_PCS_LINK_STA0]      = {ni_json_k("link_status_latch"),      ni_user_f("Link status", NUF_N | NUFW(-4))},
	[NI_PCS_LINK_STA1]      = {ni_json_k("link_status"),            ni_user_v(NULL, NUF_DA | NUFW(-4), " | ", NULL)},
	[NI_PCS_SPEED]          = {ni_json_k("speed_str"),              ni_user_l("Speed")},

	[NI_ETH_REPEATER]       = {ni_json_k("repeater"),               ni_user_l("Repeater status")},

	[NI_PCS_RFAULT]         = {ni_json_k("receive_fault"),          ni_user_l("Receive fault")},
	[NI_PCS_TFAULT]         = {ni_json_k("transmit_fault"),         ni_user_l("Transmit fault")},
	[NI_PMA_RFAULT]         = {ni_json_k("receive_fault"),          ni_user_l("Receive fault")},
	[NI_PMA_TFAULT]         = {ni_json_k("transmit_fault"),         ni_user_l("Transmit fault")},
	[NI_PMA_TYPE]           = {ni_json_k("type"),                   ni_user_l("PMA type")},
	[NI_LIST_PMA_FEATS_AV]  = {ni_json_k("available_features"),     ni_user_f("Supported PMA features ->", 0)},

	[NI_LIST_PCS_FEATS_AV]  = {ni_json_k("available_features"),     ni_user_f("Supported PCS features ->", 0)},


	[NI_PCS_GLB_BLK_LCK0]   = {ni_json_k("global_block_lock_latch"),ni_user_f("Global block lock", NUF_N | NUFW(-4))},
	[NI_PCS_GLB_BLK_LCK1]   = {ni_json_k("global_block_lock"),      ni_user_v(NULL, NUF_DA | NUFW(-4), " | ", NULL)},

	[NI_PCS_GLB_HIGH_BER0]  = {ni_json_k("global_high_ber_latch"),  ni_user_f("Global high BER", NUF_N | NUFW(-4))},
	[NI_PCS_GLB_HIGH_BER1]  = {ni_json_k("global_high_ber"),        ni_user_v(NULL, NUF_DA | NUFW(-4), " | ", NULL)},
	[NI_PCS_BER_CNT]        = {ni_json_k("ber_counter"),            ni_user_l("BER counter")},
	[NI_PCS_BLK_ERR]        = {ni_json_k("error_blocks"),           ni_user_l("Errored blocks")},
	[NI_PCS_LANES_ALIGNED]  = {ni_json_k("lanes_aligned"),          ni_user_l("PCS lanes aligned")},

	[NI_LIST_PCS_BLK_LCKS]  = {ni_json_k("block_lock"),             ni_user_v("Block lock for lanes", NUF_VE | NUFA(10), "", NULL)},
	[NI_PCS_BLK_LCK]        = {ni_json_e,                           ni_user_f(NULL, NUF_NDA | NUFW(6))},

	[NI_LIST_AM_LCKS]       = {ni_json_k("am_lock"),                ni_user_v("AM lock", NUF_VE | NUFA(10), "", NULL)},
	[NI_PCS_AM_LCK]         = {ni_json_e,                           ni_user_f(NULL, NUF_NDA | NUFW(6))},

	[NI_LIST_LANE_MAP]      = {ni_json_k("lane_map"),               ni_user_f("Lane mapping", NUF_VE | NUFA(10))},
	[NI_PCS_LANE_MAP]       = {ni_json_e,                           ni_user_f(NULL, NUF_NDA | NUFW(6))},

	[NI_LIST_BIP_ERR_CNT]   = {ni_json_k("bip_error_counters"),     ni_user_f("BIP error counters", NUF_VE | NUFA(10))},
	[NI_BIP_ERR_CNT]        = {ni_json_e,                           ni_user_f(NULL, NUF_NDA | NUFW(6))},

	[NI_LIST_PMA_TYPES_AV]  = {ni_json_k("available_types"),        ni_user_f("Supported PMA types ->", 0)},
	[NI_SEC_PMA_TYPES]      = {ni_json_n,                           ni_user_f(NULL, 256)},
	[NI_PMA_TYPES_NAME]     = {ni_json_k("name"),                   ni_user_l("")},
	[NI_PMA_TYPES_ACTIVE]   = {ni_json_n,                           ni_user_f(" * ", NUF_NDA)},

	[NI_SEC_PMA_FEAT]       = {ni_json_e,                           ni_user_n},
	[NI_PMA_FEAT_NAME]      = {ni_json_k("name"),                   ni_user_l("")},
	[NI_PMA_FEAT_ACTIVE]    = {ni_json_k("active"),                 ni_user_f(" * ", NUF_NDA)},

	[NI_SEC_RXMAC]          = {ni_json_k("rxmac"),                  ni_user_l("RXMAC Status")},
	[NI_RXM_ENABLED]        = {ni_json_k("enabled"),                ni_user_l("RXMAC status")},
	[NI_RXM_LINK]           = {ni_json_k("link"),                   ni_user_l("Link status")},
	[NI_RXM_HFIFO_OVF]      = {ni_json_k("hfifo_overflow"),         ni_user_l("HFIFO overflow occurred")},

	[NI_SEC_RXMAC_S]        = {ni_json_k("stats"),                  ni_user_n},
	[NI_RXM_RECV_O]         = {ni_json_k("pass_octets"),            ni_user_f("Received octets", NUFC)},
	[NI_RXM_PROCESSED]      = {ni_json_k("total"),                  ni_user_f("Processed", NUFC)},
	[NI_RXM_RECEIVED]       = {ni_json_k("pass"),                   ni_user_f("Received", NUFC)},
	[NI_RXM_ERRONEOUS]      = {ni_json_k("erroneous"),              ni_user_f("Erroneous", NUFC)},
	[NI_RXM_OVERFLOWED]     = {ni_json_k("overflowed"),             ni_user_f("Overflowed", NUFC)},

	[NI_SEC_RXMAC_CONF]     = {ni_json_k("config"),                 ni_user_l("RXMAC configuration")},
	[NI_RXM_ERR_MASK_REG]   = {ni_json_k("err_mask_reg"),           ni_user_l("Error mask register")},

	[NI_RXM_ERR_FRAME]      = {ni_json_k("err_mask_frame_err"),     ni_user_l(" * Frame error from MII [0]")},
	[NI_RXM_ERR_CRC]        = {ni_json_k("err_mask_crc_check"),     ni_user_l(" * CRC check            [1]")},
	[NI_RXM_ERR_MIN_LEN]    = {ni_json_k("err_mask_min_length"),    ni_user_l(" * Minimal frame length [2]")},
	[NI_RXM_MIN_LEN]        = {ni_json_k("pkt_min_length"),         ni_user_l("   * length")},
	[NI_RXM_ERR_MAX_LEN]    = {ni_json_k("err_mask_max_length"),    ni_user_l(" * Maximal frame length [3]")},
	[NI_RXM_MAX_LEN]        = {ni_json_k("pkt_max_length"),         ni_user_l("   * length")},
	[NI_RXM_MAX_LEN_CAP]    = {ni_json_k("pkt_max_length_capable"), ni_user_l("   * capable length")},
	[NI_RXM_ERR_MAC_CHECK]  = {ni_json_k("err_mask_mac_addr_check"),ni_user_l(" * MAC address check    [4]")},
	[NI_RXM_ERR_MAC_MODE]   = {ni_json_k("err_mask_mac_addr_mode"), ni_user_l("   * mode")},
	[NI_RXM_MAC_MAX_COUNT]  = {ni_json_k("mac_addr_count"),         ni_user_l("MAC address table size")},

	[NI_SEC_RXMAC_ES]       = {ni_json_k("etherstats"),             ni_user_l("RXMAC etherStatsTable")},
	[NI_RXM_ES_OCTS]        = {ni_json_k("octets"),                 ni_user_f("etherStatsOctets", NUFC)},
	[NI_RXM_ES_PKTS]        = {ni_json_k("pkts"),                   ni_user_f("etherStatsPkts", NUFC)},
	[NI_RXM_ES_BCST]        = {ni_json_k("broadcast"),              ni_user_f("etherStatsBroadcastPkts", NUFC)},
	[NI_RXM_ES_MCST]        = {ni_json_k("multicast"),              ni_user_f("etherStatsMulticastPkts", NUFC)},
	[NI_RXM_ES_CRCE]        = {ni_json_k("crc_align_errors"),       ni_user_f("etherStatsCRCAlignErrors", NUFC)},
	[NI_RXM_ES_UNDR]        = {ni_json_k("undersize"),              ni_user_f("etherStatsUndersizePkts", NUFC)},
	[NI_RXM_ES_OVER]        = {ni_json_k("oversize"),               ni_user_f("etherStatsOversizePkts", NUFC)},
	[NI_RXM_ES_FRAG]        = {ni_json_k("fragments"),              ni_user_f("etherStatsFragments", NUFC)},
	[NI_RXM_ES_JABB]        = {ni_json_k("jabbers"),                ni_user_f("etherStatsJabbers", NUFC)},
	[NI_RXM_ES_64]          = {ni_json_k("pkts64"),                 ni_user_f("etherStatsPkts64Octets", NUFC)},
	[NI_RXM_ES_65_127]      = {ni_json_k("pkts65to127"),            ni_user_f("etherStatsPkts65to127Octets", NUFC)},
	[NI_RXM_ES_128_255]     = {ni_json_k("pkts128to255"),           ni_user_f("etherStatsPkts128to255Octets", NUFC)},
	[NI_RXM_ES_256_511]     = {ni_json_k("pkts256to511"),           ni_user_f("etherStatsPkts256to511Octets", NUFC)},
	[NI_RXM_ES_512_1023]    = {ni_json_k("pkts512to1023"),          ni_user_f("etherStatsPkts512to1023Octets", NUFC)},
	[NI_RXM_ES_1024_1518]   = {ni_json_k("pkts1024to1518"),         ni_user_f("etherStatsPkts1024to1518Octets", NUFC)},
	[NI_RXM_ES_UNDR_SET]    = {ni_json_k("conf_undersize"),         ni_user_f("underMinPkts", NUFC)},
	[NI_RXM_ES_OVER_SET]    = {ni_json_k("conf_oversize"),          ni_user_f("overMaxPkts", NUFC)},

	[NI_SEC_TXMAC]          = {ni_json_k("txmac"),                  ni_user_l("TXMAC status")},
	[NI_TXM_ENABLED]        = {ni_json_k("enabled"),                ni_user_l("TXMAC status")},
	[NI_SEC_TXMAC_S]        = {ni_json_k("stats"),                  ni_user_n},
	[NI_TXM_SENT_O]         = {ni_json_k("sent_octets"),            ni_user_f("Transmitted octets", NUFC)},
	[NI_TXM_PROCESSED]      = {ni_json_k("processed"),              ni_user_f("Processed", NUFC)},
	[NI_TXM_SENT]           = {ni_json_k("sent"),                   ni_user_f("Transmitted", NUFC)},
	[NI_TXM_ERRONEOUS]      = {ni_json_k("erroneous"),              ni_user_f("Erroneous", NUFC)},

	/* Still Eth section */
	[NI_TRANS_PRSNT]        = {ni_json_k("present"),                ni_user_l("Transceiver status")},
	[NI_TRANS_PRSNT_UNK]    = {ni_json_n,                           ni_user_l("Transceiver status")},
	[NI_TRANS_CAGE_TYPE]    = {ni_json_k("cage_type"),              ni_user_f("Transceiver cage", NUF_N)},
	[NI_TRANS_CAGE_ID]      = {ni_json_k("cage_id"),                ni_user_v(NULL, NUF_DA, "-", NULL)},
	[NI_LIST_TRN_LANES]     = {ni_json_k("lanes"),                  ni_user_v("Transceiver lane(s)", NUF_VE, "|", "\n")},
	[NI_TRANS_LANE]         = {ni_json_e,                           ni_user_v(NULL, NUF_NDA, NULL, NULL)},

	/* Trans section */
	[NI_LIST_TRANS]         = {ni_json_k("transceivers"),           ni_user_v(NULL, 0, "\n", NULL)},

	[NI_SEC_TRN]            = {ni_json_n,                           ni_user_l("")},
	[NI_TRN_INDEX]          = {ni_json_k("id"),                     ni_user_f("", NUF_NDA | NUF_SL | NUFW(2))},
	[NI_TRN_NAME]           = {ni_json_k("name"),                   ni_user_v("", NUF_NDA | NUF_SL, NULL, "")},
	[NI_MOD_IDENT]          = {ni_json_k("identifier"),             ni_user_l("Module identifier")},
	[NI_TRN_COMPLIANCE]     = {ni_json_k("compliance"),             ni_user_l("Compliance")},
	[NI_TRN_CONNECTOR]      = {ni_json_k("connector"),              ni_user_l("Connector")},
	[NI_SFF8636_VNDR_NAME]  = {ni_json_k("vendor_name"),            ni_user_l("Vendor name")},
	[NI_SFF8636_VNDR_SN]    = {ni_json_k("vendor_serial_number"),   ni_user_l("Vendor serial number")},
	[NI_SFF8636_VNDR_PN]    = {ni_json_k("vendor_part_number"),     ni_user_l("Vendor part number")},
	[NI_SFF8636_REVISION]   = {ni_json_k("revision"),               ni_user_l("Revision")},
	[NI_SFF8636_TEMP]       = {ni_json_f("temperature", NJFD(2)),   ni_user_v("Temperature", NUFD(2), NULL, " C")},
	[NI_SFF8636_WL]         = {ni_json_f("wavelength", NJFD(2)),    ni_user_v("Wavelength", NUF_N | NUFD(2) , NULL, " nm")},
	[NI_SFF8636_WL_TOL]     = {ni_json_f("wavelength_tolerance", NJFD(2)), ni_user_v(" ", NUF_DA |NUFD(2), "+-", " nm")},

	[NI_LIST_TRN_RX_IN_PWR] = {ni_json_k("rx_input_power"),         ni_user_l("RX input power")},
	[NI_TRANS_RX_IN_PWR_L]  = {ni_json_n,                           ni_user_f(" * Lane ", NUF_NDA)},
	[NI_TRANS_RX_IN_PWR_V]  = {ni_json_e,                           ni_user_f("", NUFD(2) | NUFW(1))},

	[NI_LIST_TRN_STX_DIS]   = {ni_json_k("stx_disable"),            ni_user_l("Software TX disable")},
	[NI_TRANS_STX_DIS_L]    = {ni_json_n,                           ni_user_f(" * Lane ", NUF_NDA)},
	[NI_TRANS_STX_DIS_V]    = {ni_json_e,                           ni_user_l("")},

	[NI_TRN_CMIS_VER_MAJ]   = {ni_json_k("cmis_version_major"),     ni_user_f("CMIS version", NUF_N)},
	[NI_TRN_CMIS_VER_MIN]   = {ni_json_k("cmis_version_minor"),     ni_user_v("", NUF_DA, ".", NULL)},
	[NI_TRN_CMIS_GLB_STAT]  = {ni_json_k("cmis_module_state"),      ni_user_l("Module state")},
	[NI_TRN_CMIS_VNDR_NAME] = {ni_json_k("vendor_name"),            ni_user_l("Vendor name")},
	[NI_TRN_CMIS_VNDR_SN]   = {ni_json_k("vendor_serial_number"),   ni_user_l("Vendor serial number")},
	[NI_TRN_CMIS_VNDR_PN]   = {ni_json_k("vendor_part_number"),     ni_user_l("Vendor part number")},
	[NI_TRN_CMIS_MED_T]     = {ni_json_k("media_type"),             ni_user_l("Media type")},
	[NI_TRN_CMIS_IFC_T]     = {ni_json_k("interface_type"),         ni_user_l("Media interface technology")},
	[NI_MDIO_VNDR_NAME]     = {ni_json_k("vendor_name"),            ni_user_l("Vendor name")},
	[NI_MDIO_SN]            = {ni_json_k("vendor_serial_number"),   ni_user_l("Vendor serial number")},
	[NI_MDIO_PN]            = {ni_json_k("vendor_part_number"),     ni_user_l("Vendor part number")},
	[NI_MDIO_HW_REV]        = {ni_json_k("hw_spec_rev"),            ni_user_f("HW spec. rev.", NUFD(1))},
	[NI_MDIO_MGMT_REV]      = {ni_json_k("mgmt_spec_rev"),          ni_user_f("Management ifc. spec. rev.", NUFD(1))},

	[NI_SEC_RSFEC_STATUS]   = {ni_json_k("rsfec"),                  ni_user_l("RS-FEC status")},
	[NI_SEC_RSFEC119_STATUS]= {ni_json_k("rsfec_cl119"),            ni_user_l("RS-FEC status")},
	[NI_RSFEC_STATUS_BCA]   = {ni_json_k("bypass_correction"),      ni_user_l("RS-FEC bypass correction ability")},
	[NI_RSFEC_STATUS_BIA]   = {ni_json_k("bypass_indication"),      ni_user_l("RS-FEC bypass indication ability")},
	[NI_RSFEC_STATUS_SER]   = {ni_json_k("high_ser"),               ni_user_l("RS-FEC high SER")},
	[NI_RSFEC_STATUS_FLA]   = {ni_json_k("lanes_aligned"),          ni_user_l("RS-FEC lanes aligned")},
	[NI_RSFEC_STATUS_PLA]   = {ni_json_k("pcs_lanes_aligned"),      ni_user_l("PCS lanes aligned")},
	[NI_RSFEC_STATUS_DSER]  = {ni_json_k("degraded_ser"),           ni_user_l("RS-FEC degraded SER")},
	[NI_RSFEC_STATUS_RDSER] = {ni_json_k("remote_degraded_ser"),    ni_user_l("Remote degraded SER")},
	[NI_RSFEC_STATUS_LDSER] = {ni_json_k("local_degraded_ser"),     ni_user_l("Local degraded SER")},

	[NI_RSFEC_CORRECTED]    = {ni_json_k("corrected_cws"),          ni_user_l("RS-FEC corrected cws")},
	[NI_RSFEC_UNCORRECTED]  = {ni_json_k("uncorrected_cws"),        ni_user_l("RS-FEC uncorrected cws")},

	[NI_LIST_RSFEC_SYM_ERR] = {ni_json_k("symbol_errors"),          ni_user_l("RS-FEC symbol errors ->")},
	[NI_RSFEC_SYM_ERR_L]    = {ni_json_n,                           ni_user_f(" * Lane ", NUF_NDA)},
	[NI_RSFEC_SYM_ERR_V]    = {ni_json_e,                           ni_user_l("")},

	[NI_LIST_RSFEC_LANE_MAP]= {ni_json_k("lane_map"),               ni_user_v("RS-FEC lane mapping", NUF_VE, " ", NULL)},
	[NI_RSFEC_LANE_MAP]     = {ni_json_e,                           ni_user_f("", NUF_NDA)},

	[NI_LIST_RSFEC_AM_LOCK] = {ni_json_k("am_lock"),                ni_user_v("RS-FEC AM lock", NUF_VE, " ", NULL)},
	[NI_RSFEC_AM_LOCK]      = {ni_json_e,                           ni_user_f(NULL, NUF_NDA)},
};

int print_ctrl_reg_json(void *priv, int item, int val)
{
	struct ni_json_cbp *p = priv;
	const char *res = NULL;

	switch (item) {
	case NI_PCS_LANE_MAP:
		if (val == -1)
			return fprintf(p->f, "null");
		return fprintf(p->f, "%d", val);
	case NI_ETH_REPEATER:
		switch (val) {
		case IDCOMP_REPEATER_NORMAL: res = "\"normal\""; break;
		case IDCOMP_REPEATER_REPEAT: res = "\"repeat\""; break;
		case IDCOMP_REPEATER_IDLE:   res = "\"idle\""; break;
		default:                     res = "\"unknown\""; break;
		}
		break;
	case NI_RXM_ERR_MAC_MODE:
		switch(val) {
		case RXMAC_MAC_FILTER_PROMISCUOUS:      res = "\"promiscuous\""; break;
		case RXMAC_MAC_FILTER_TABLE:            res = "\"normal\""; break;
		case RXMAC_MAC_FILTER_TABLE_BCAST:      res = "\"broadcast\""; break;
		case RXMAC_MAC_FILTER_TABLE_BCAST_MCAST:res = "\"multicast\""; break;
		}
		break;
	default:
		res = val ? "true": "false";
		break;
	}
	return fprintf(p->f, res);
}

int print_ctrl_reg_user(void *priv, int item, int val)
{
	struct ni_user_cbp *p = priv;
	const char *res = NULL;

	switch (item) {
	case NI_PCS_GLB_BLK_LCK0:
	case NI_PCS_GLB_BLK_LCK1:
	case NI_PCS_GLB_HIGH_BER0:
	case NI_PCS_GLB_HIGH_BER1:
	case NI_PCS_LANES_ALIGNED:
	case NI_PMA_RFAULT:
	case NI_PMA_TFAULT:
	case NI_PCS_RFAULT:
	case NI_PCS_TFAULT:             res = val ? "Yes" : "No"; break;

	case NI_RXM_ENABLED:
	case NI_TXM_ENABLED:            res = val ? "ENABLED" : "DISABLED"; break;

	case NI_PMA_LINK_STA0:
	case NI_PMA_LINK_STA1:
	case NI_PCS_LINK_STA0:
	case NI_PCS_LINK_STA1:
	case NI_RXM_LINK:               res = val ? "UP" : "DOWN"; break;

	case NI_RSFEC_STATUS_BCA:
	case NI_RSFEC_STATUS_BIA:
	case NI_RSFEC_STATUS_SER:
	case NI_RSFEC_STATUS_FLA:
	case NI_RSFEC_STATUS_PLA:
	case NI_RSFEC_STATUS_DSER:
	case NI_RSFEC_STATUS_RDSER:
	case NI_RSFEC_STATUS_LDSER:
	case NI_RXM_HFIFO_OVF:          res = val ? "True" : "False"; break;

	case NI_TRANS_PRSNT:            res = val ? "OK" : "Not plugged"; break;

	case NI_TRANS_STX_DIS_V:        res = val ? "active" : "inactive"; break;

	case NI_PMA_TYPES_ACTIVE:
	case NI_PMA_FEAT_ACTIVE:        res = val ? "[active]" : ""; break;

	case NI_RXM_ERR_FRAME:
	case NI_RXM_ERR_CRC:
	case NI_RXM_ERR_MIN_LEN:
	case NI_RXM_ERR_MAX_LEN:
	case NI_RXM_ERR_MAC_CHECK:      res = val ? "enabled" : "disabled"; break;

	case NI_PCS_AM_LCK:
	case NI_RSFEC_AM_LOCK:
	case NI_PCS_BLK_LCK:            res = val ? "L" : "X"; break;

	case NI_PCS_LANE_MAP:
		if (val == -1) {
			res = "U";
		} else {
			return fprintf(p->f, "%*d", p->width, val);
		}
		break;

	case NI_ETH_REPEATER:
		switch (val) {
		case IDCOMP_REPEATER_NORMAL: res = "Normal  (transmit data from application)"; break;
		case IDCOMP_REPEATER_REPEAT: res = "Repeat  (transmit data from RXMAC)"; break;
		case IDCOMP_REPEATER_IDLE:   res = "Idle    (transmit disabled)"; break;
		default:                     res = "Unknown (use the PCS/PMA features)"; break;
		}
		break;
	case NI_RXM_ERR_MAC_MODE:
		switch(val) {
		case RXMAC_MAC_FILTER_PROMISCUOUS:	res = "Promiscuous mode"; break;
		case RXMAC_MAC_FILTER_TABLE:		res = "Filter by MAC address table"; break;
		case RXMAC_MAC_FILTER_TABLE_BCAST:	res = "Filter by MAC address table, allow broadcast"; break;
		case RXMAC_MAC_FILTER_TABLE_BCAST_MCAST:res = "Filter by MAC address table, allow broadcast + multicast"; break;
		}
		break;
	}
	if (res)
		return fprintf(p->f, "%*s", p->width, res);

	return 0;
}

int print_pwr_json(void *priv, int item, double val)
{
	struct ni_json_cbp *p = priv;
	(void) item;
	return fprintf(p->f, "%.8f", val);
}

int print_pwr_user(void *priv, int item, double val)
{
	struct ni_user_cbp *p = priv;
	int uw;
	(void) item;

	val *= 1000; /* from W to mW */
	uw = val < 1;
	return fprintf(p->f, "%.2f %s (%.2f dBm)",
			uw ? val * 1000 : val,
			uw ? "uW" : "mW",
			10 * log10(val));
}

struct ni_eth_item_f_t ni_eth_item_f[] = {
	[NI_DRC_JSON] = {
		.c = ni_common_item_callbacks[NI_DRC_JSON],
		.print_ctrl_reg = print_ctrl_reg_json,
		.print_qsfp_i2c_text = print_json_qsfp_i2c_text,
		.print_mdio_text = print_mdio_text_json,
		.print_pwr = print_pwr_json,
	},
	[NI_DRC_USER] = {
		.c = ni_common_item_callbacks[NI_DRC_USER],
		.print_ctrl_reg = print_ctrl_reg_user,
		.print_qsfp_i2c_text = print_user_qsfp_i2c_text,
		.print_mdio_text = print_mdio_text_user,
		.print_pwr = print_pwr_user,
	},
};


// TODO add usage for queries
void usage(const char *progname, int verbose)
{
	printf("Usage: %s  [-rtPTvhRS] [-d path] [-i index] [-e 1|0] [-p repeater_cfg]\n"
		   "                [-l min_length] [-L max_length] [-m err_mask]\n"
		   "                [-M mac_cmd] [opt_param]\n", progname);

	printf("Only one command may be used at a time.\n");
	printf("-d path         Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-i indexes      Interfaces numbers to use - list or range, e.g. \"0-5,7\" [default: all]\n");
	printf("-r              Use RXMAC [default]\n");
	printf("-t              Use TXMAC [default]\n");
	printf("-P              Use PCS/PMA\n");
	printf("-T              Use transceiver\n");
	printf("-e 1|0          Enable [1] / disable [0] interface\n");
	printf("-R              Reset frame counters\n");
	printf("-S              Show etherStats counters\n");
	printf("-l length       Minimal allowed frame length\n");
	printf("-L length       Maximal allowed frame length\n");
	printf("-m mask         Set RXMAC error bitmask value (integer; use -v to view current configuration)\n");
	printf("-c type         Set PMA type/mode by name or enable/disable feature (+feat/-feat)\n");
	printf("-p repeater_cfg Set transmit data source%s\n", verbose ? "" : " (-hv for more info)");
	if (verbose) {
		printf(" * normal       Transmit data from application\n");
		printf(" * repeat       Transmit data from RXMAC\n");
		printf(" * idle         Transmit disabled\n");
	}
	printf("-M command      MAC filter settings (RXMAC only)%s\n", verbose ? "" : " (-hv for more info)");
	if (verbose) {
		printf(" * add          Add MAC address specified in [opt_param] to table\n");
		printf(" * remove       Remove MAC address specified in [opt_param] from table\n");
		printf(" * show         Show content of MAC address table\n");
		printf(" * clear        Clear content of MAC address table\n");
		printf(" * fill         Fill MAC address table with values from stdin\n");
		printf(" * promiscuous  Pass all traffic\n");
		printf(" * normal       Pass only MAC addresses present in table\n");
		printf(" * broadcast    Pass MAC addresses present in table and broadcast traffic\n");
		printf(" * multicast    Pass MAC addresses present in table, broadcast and multicast traffic\n");
	}
	printf("-q query        Get specific informations%s\n", verbose ? "" : " (-v for more info)");
	if (verbose) {
        	printf(" * rx_status\n");
        	printf(" * rx_octets\n");
        	printf(" * rx_processed\n");
        	printf(" * rx_erroneous\n");
        	printf(" * rx_link\n");
        	printf(" * rx_received\n");
        	printf(" * rx_overflowed\n");
        	printf(" * tx_status\n");
        	printf(" * tx_octets\n");
        	printf(" * tx_processed\n");
        	printf(" * tx_erroneous\n");
        	printf(" * tx_transmitted\n");
        	printf(" * pma_type\n");
        	printf(" * pma_speed\n");
		printf(" example of usage: '-q rx_link,tx_octets,pma_speed'\n");
	}

	printf("-j              Print output in JSON\n");
	printf("-v              Increase verbosity (including help)\n");
	printf("-h              Show this text\n");
	printf("\n");
	printf("Examples:\n");
	printf("%s -Pv                         "   "Print all supported PCS/PMA types/modes and features\n", progname);
	printf("%s -Pc 100GBASE-SR4            "   "Change the link type/mode\n", progname);
	printf("%s -Pc \"+25G RS-FEC Enable\"    " "Enable the RS-FEC feature (can affect the link type/mode)\n", progname);
	printf("%s -Pc \"+PMA local loopback\"   " "Receive exactly the same data sent by the device (for transceiver-less testing)\n", progname);
	printf("%*s                             "  "(discards data from the link, far-end still should receive the sent data)\n", (int)strlen(progname), "");
	printf("%s -Pc \"+PCS reverse loopback\" " "Transmit received data back to far-end (\"repeater\" functionality)\n", progname);
	printf("%*s                             "  "(application still receives the data from the link)\n", (int)strlen(progname), "");
	printf("%s -Pc -Reset                  "   "Unreset the PCS/PMA\n", progname);
	if (verbose) {
		printf("\n");
		printf("Loopback cheatsheet:                "   "App -> Tx MAC ->  /--> Tx PCS --o-->  /--> Tx PMA --o-->  Link\n");
		printf("(A) \"PCS reverse loopback\"          " "                  ^             |     ^             |         \n");
		printf("(B) \"PCS local loopback\"            " "                 (A)           (B)   (C)           (D)        \n");
		printf("(C) \"PMA remote loopback\"           " "                  |             v     |             v         \n");
		printf("(D) \"PMA local loopback\"            " "App <- Rx MAC  <--o--- Rx PCS <-/  <--o--- Rx PMA <-/  <- Link\n");
	}
}

int main(int argc, char *argv[])
{
	const char *file = nfb_default_dev_path();
	struct nfb_device *dev;
	const char *query = NULL;
	char *queries_index = NULL;
	int size = 0;
	const void *fdt;

	int i, c;
	int ret;
	char cmds = 0;
	int used = 0;
	int js = NI_DRC_USER;

	unsigned mv[6];
	int use = 0;
	int node;
	int fdt_offset;
	struct nc_rxmac *rxmac;
	struct nc_txmac *txmac;
	struct eth_params p, p2;
	struct list_range index_range;

	enum nc_idcomp_repeater repeater_status = IDCOMP_REPEATER_NORMAL;

	p.command = CMD_PRINT_STATUS;
	p.verbose = 0;
	p.ether_stats = false;
	p.index = 0;

	list_range_init(&index_range);

	opterr = 0;
	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch(c) {
		/* Common parameters */
		case 'v':
			p.verbose++;
			break;
		case 'j':
			js = NI_DRC_JSON;
			break;
		case 'h':
			p.command = CMD_USAGE;
			break;
		case 'd':
			file = optarg;
			break;
		case 'q':
			p.command = CMD_QUERY;
			query = optarg;
			break;
		case 'i':
			if (list_range_parse(&index_range, optarg) < 0)
				errx(EXIT_FAILURE, "Cannot parse interface number.");
			break;

		/* Modules */
		case 'r':
			use |= RXMAC;
			break;
		case 't':
			use |= TXMAC;
			break;
		case 'P':
			use |= PCSPMA;
			break;
		case 'T':
			use |= TRANSCEIVER;
			break;

		/* Commands */
		case 'e':
			if (nc_strtol(optarg, &p.param) ||
					(p.param != 0 && p.param != 1))
				errx(EXIT_FAILURE, "Wrong enable value [0|1].");
			p.command = CMD_ENABLE;
			cmds++;
			break;
		case 'R':
			p.command = CMD_RESET;
			cmds++;
			break;

		case 'S':
			p.ether_stats = true;
			break;

		case 'l':
			if (nc_strtol(optarg, &p.param) || p.param <= 0) {
				errx(EXIT_FAILURE, "Wrong minimal frame length.");
			}
			p.command = CMD_SET_MIN_LENGTH;
			cmds++;
			break;
		case 'L':
			if (nc_strtol(optarg, &p.param) ||
					p.param <= 0) {
				errx(EXIT_FAILURE, "Wrong maximal frame length.");
			}
			p.command = CMD_SET_MAX_LENGTH;
			cmds++;
			break;
		case 'm':
			if (nc_strtol(optarg, &p.param) ||
					p.param < 0 || p.param > 31) {
				errx(EXIT_FAILURE, "Wrong error mask.");
			}
			p.command = CMD_SET_ERROR_MASK;
			cmds++;
			break;
		case 'M':
			switch (tolower(optarg[0])) {
			case 's': p.command = CMD_SHOW_MACS; break;
			case 'f': p.command = CMD_FILL_MACS; break;
			case 'c': p.command = CMD_CLEAR_MACS; break;
			case 'a': p.command = CMD_ADD_MAC; break;
			case 'r': p.command = CMD_REMOVE_MAC; break;
			case 'p': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_PROMISCUOUS; break;
			case 'n': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_TABLE; break;
			case 'b': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_TABLE_BCAST; break;
			case 'm': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_TABLE_BCAST_MCAST; break;
			default:
				errx(EXIT_FAILURE, "Wrong MAC filter settings.");
			break;
			}
			cmds++;
			break;
		case 'c':
			if (optarg[0] == '+' || optarg[0] == '-') {
				p.command = CMD_SET_PMA_FEATURE;
				p.string = optarg + 1;
				p.param = optarg[0] == '+' ? 1 : 0;
			} else {
				p.command = CMD_SET_PMA_TYPE;
				p.string = optarg;
			}
			break;
		case 'p':
			switch (tolower(optarg[0])) {
			case 'n': repeater_status = IDCOMP_REPEATER_NORMAL; break;
			case 'r': repeater_status = IDCOMP_REPEATER_REPEAT; break;
			case 'i': repeater_status = IDCOMP_REPEATER_IDLE; break;
			default:
				errx(EXIT_FAILURE, "Wrong repeater settings.");
			}
			p.command = CMD_SET_REPEATER;
			cmds++;
			break;
		case '?':
			errx(EXIT_FAILURE, "Unknown argument '%c'", optopt);
		case ':':
			errx(EXIT_FAILURE, "Missing parameter for argument '%c'", optopt);
		default:
			errx(EXIT_FAILURE, "Unknown error");
		}
	}

	if (p.command == CMD_USAGE) {
		usage(argv[0], p.verbose);
		return EXIT_SUCCESS;
	}

	argc -= optind;
	argv += optind;

	if (p.command == CMD_ADD_MAC || p.command == CMD_REMOVE_MAC) {
		if (argc < 1) {
			errx(EXIT_FAILURE, "Missing MAC address for for argument 'M'");
		}
		if (6 == sscanf(argv[0], "%x:%x:%x:%x:%x:%x%*c", mv+0, mv+1, mv+2, mv+3, mv+4, mv+5)) {
			p.mac_address = 0;
			for (i = 0; i < 6; i++) {
				p.mac_address <<= 8;
				p.mac_address |= mv[i] & 0xFF;
			}
		} else {
			errx(EXIT_FAILURE, "Can't parse MAC address, excepted in format AA:BB:CC:DD:EE:FF");
		}
		argc--;
		argv++;
	}

	if (argc > 0) {
		errx(EXIT_FAILURE, "Stray arguments");
	}

	if (cmds > 1) {
		errx(EXIT_FAILURE, "More than one operation requested. Please select just one.");
	}

	dev = nfb_open(file);
	if (!dev) {
		err(EXIT_FAILURE, "nfb_open failed");
	}

	fdt = nfb_get_fdt(dev);

	if (p.command == CMD_QUERY) {
		size = nc_query_parse(query, queries, NC_ARRAY_SIZE(queries), &queries_index);
		if (size <= 0) {
			nfb_close(dev);
			return -1;
		}
	} else {
		if (use == 0)
			use = RXMAC | TXMAC;
		else if (use & TRANSCEIVER)
			use &= TRANSCEIVER;
	}

	struct ni_context *ctx = NULL;
	if (p.command == CMD_PRINT_STATUS) {
		ctx = ni_init_root_context_default(js, ni_items, &ni_eth_item_f[js]);
	}

	ni_section(ctx, NI_SEC_ROOT);

	if ((use & TRANSCEIVER) == 0) {
	ni_list(ctx, NI_LIST_ETH);
	fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
		if (list_range_empty(&index_range) || list_range_contains(&index_range, p.index)) {
			ni_section(ctx, NI_SEC_ETH);
			ni_item_int(ctx, NI_SEC_ETH_ID, p.index);

			if (p.command == CMD_SET_REPEATER) {
				used++;
				if (nc_idcomp_repeater_set(dev, p.index, repeater_status)) {
					errx(EXIT_FAILURE, "Can't set repeater mode. Use loopback features in PCS/PMA section");
				}
			} else {
				if (p.command == CMD_PRINT_STATUS) {
					used++;
					p2 = p;
					p2.command = CMD_PRINT_SPEED;
					pcspma_execute_operation(ctx, dev, node, &p2);
					transceiver_print_short_info(ctx, dev, node, &p);

					if (p.verbose) {
						repeater_status = nc_idcomp_repeater_get(dev, p.index);
						ni_item_ctrl_reg(ctx, NI_ETH_REPEATER, repeater_status);
					}
				}

				if (p.command == CMD_QUERY) {
					used++;
					ret = query_print(fdt, node, queries_index, size, dev, p.index);
					if (ret) {
						free(queries_index);
						list_range_destroy(&index_range);
						nfb_close(dev);
						return EXIT_FAILURE;
					}
				}

				if (use & RXMAC) {
					used++;
					fdt_offset = nc_eth_get_rxmac_node(fdt, node);
					rxmac = nc_rxmac_open(dev, fdt_offset);
					if (!rxmac) {
						warnx("Cannot open RXMAC for ETH%d", p.index);
					} else {
						if (rxmac_execute_operation(ctx, rxmac, &p) != 0)
							warnx("Cannot perform a command on RXMAC%d", p.index);
						nc_rxmac_close(rxmac);
					}
				}

				if (use & TXMAC) {
					used++;
					fdt_offset = nc_eth_get_txmac_node(fdt, node);
					txmac = nc_txmac_open(dev, fdt_offset);
					if (!txmac) {
						warnx("Cannot open TXMAC for ETH%d", p.index);
					} else {
						if (txmac_execute_operation(ctx, txmac, &p) != 0)
							warnx("Cannot perform a command on TXMAC%d", p.index);
						nc_txmac_close(txmac);
					}
				}

				if (p.command == CMD_PRINT_STATUS) {
					used++;
					if (use & PCSPMA)
						pcspma_execute_operation(ctx, dev, node, &p);
				} else if (use & PCSPMA) {
					used++;
					ret = pcspma_execute_operation(ctx, dev, node, &p);
					if (ret)
						warnx("PCS/PMA command failed");
				}
			}
			ni_endsection(ctx, NI_SEC_ETH);
		}
		p.index++;
	}
	ni_endlist(ctx, NI_LIST_ETH);
	}

	if (use & TRANSCEIVER) {
		ni_list(ctx, NI_LIST_TRANS);
		fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
			if (list_range_empty(&index_range) || list_range_contains(&index_range, p.index)) {
				ni_section(ctx, NI_SEC_TRN);
				if (p.command == CMD_PRINT_STATUS) {
					used++;
					transceiver_print(ctx, dev, node, p.index);
				} else {
					used++;
					ret = transceiver_execute_operation(dev, node, &p);
					if (ret)
						warnx("Transceiver command failed");
				}
				ni_endsection(ctx, NI_SEC_TRN);
			}
			p.index++;
		}
		ni_endlist(ctx, NI_LIST_TRANS);
	}

	ni_endsection(ctx, NI_SEC_ROOT);
	ni_close_root_context(ctx);

	if (!used) {
		warnx("No such interface");
	}

	if (query)
		free(queries_index);
	list_range_destroy(&index_range);

	nfb_close(dev);
	return EXIT_SUCCESS;
}
