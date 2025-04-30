/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * DMA controller status tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include <libfdt.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <netcope/nccommon.h>
#include <netcope/rxqueue.h>
#include <netcope/txqueue.h>

#include <netcope/ni.h>

/*! Program version */
#define VERSION				"1.0: dmactl.c"

/*! Acceptable command line arguments */
/*! -d path to device file */
/*! -B buffer size */
/*! -C buffer count */
/*! -N netdev command */
/*! -i RX or TX index */
/*! -j JSON output */
/*! -r use only RX; */
/*! -t use only TX; */
/*! -R reset counters */
/*! -S ring size */
/*! -O initial offset*/
/*! -h print help; */
/*! -v verbose mode; */
/*! -V version */


#define ARGUMENTS			"d:i:q:rtRS:B:C:N:O:Tjvh"

enum commands {
	CMD_PRINT_STATUS,
	CMD_USAGE,
	CMD_COUNTER_RESET,
	CMD_COUNTER_READ_AND_RESET,
	CMD_SET_RING_SIZE,
	CMD_SET_BUFFER_SIZE,
	CMD_SET_BUFFER_COUNT,
	CMD_SET_INITIAL_OFFSET,
	CMD_NETDEV,
	CMD_QUERY,
};

const char *rx_ctrl_name[] = {
	COMP_NETCOPE_RXQUEUE_SZE,
	COMP_NETCOPE_RXQUEUE_NDP,
	COMP_NETCOPE_RXQUEUE_CALYPTE,
};
const char *tx_ctrl_name[] = {
	COMP_NETCOPE_TXQUEUE_SZE,
	COMP_NETCOPE_TXQUEUE_NDP,
	COMP_NETCOPE_TXQUEUE_CALYPTE,
};

// this enum need to corespond with queries[] array
enum queries {
	RX_RECEIVED,
	RX_RECEIVED_BYTES,
	RX_DISCARDED,
	RX_DISCARDED_BYTES,
	TX_SENT,
	TX_SENT_BYTES,
	TX_DISCARDED,
	TX_DISCARDED_BYTES
};
static const char * const queries[] = {
	"rx_received",
	"rx_received_bytes",
	"rx_discarded",
	"rx_discarded_bytes",
	"tx_sent",
	"tx_sent_bytes",
	"tx_discarded",
	"tx_discarded_bytes"
};

enum NI_ITEMS {
	NI_SEC_ROOT = 0,
	NI_LIST_ALL,

	NI_LIST_RXQ,
	NI_SEC_RXQ,
	NI_LIST_TXQ,
	NI_SEC_TXQ,

	NI_CTRL_INDEX,
	NI_CTRL_NAME,
	NI_CTRL_REG_CTL,
	NI_CTRL_REG_CTL_R,
	NI_CTRL_REG_CTL_RN,
	NI_CTRL_REG_CTL_D_RX,
	NI_CTRL_REG_CTL_E,
	NI_CTRL_REG_CTL_V,
	NI_CTRL_REG_STA,
	NI_CTRL_REG_STA_R,
	NI_CTRL_REG_STA_RN,
	NI_CTRL_REG_STA_DE,
	NI_CTRL_REG_STA_DA,
	NI_CTRL_REG_STA_RI,

	NI_CTRL_REG_SHP,
	NI_CTRL_REG_HHP,
	NI_CTRL_REG_MHP,
	NI_CTRL_REG_SDP,
	NI_CTRL_REG_HDP,
	NI_CTRL_REG_MDP,
	NI_CTRL_REG_SP,
	NI_CTRL_REG_HP,
	NI_CTRL_REG_MP,
	NI_CTRL_HBS,
	NI_CTRL_FB,
	NI_CTRL_DBS,
	NI_CTRL_BS,
	NI_CTRL_FD,

	NI_CTRL_REG_TO,
	NI_CTRL_REG_MR,
	NI_CTRL_MR,

	NI_CTRL_REG_RECV,
	NI_CTRL_REG_RECV_B,
	NI_CTRL_REG_DISC,
	NI_CTRL_REG_DISC_B,
	NI_CTRL_REG_SENT,
	NI_CTRL_REG_SENT_B,

	NI_CTRL_REG_DESC_B,
	NI_CTRL_REG_HDR_B,
	NI_CTRL_REG_PTR_B,

	NI_SEC_RXSUM,
	NI_SEC_TXSUM,
};

#define NUF_N   (NI_USER_ITEM_F_NO_NEWLINE)
#define NUF_NDA (NI_USER_ITEM_F_NO_NEWLINE | NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN)
#define NUF_DA  (NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN)
#define NUF_DAV (NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN | NI_USER_ITEM_F_NO_VALUE)
#define NUF_SL  (NI_USER_ITEM_F_SEC_LABEL)
#define NUFW(x) ni_user_f_width(x)


struct ni_context_item_default ni_items[] = {
	[NI_SEC_ROOT]           = {ni_json_e,                           ni_user_n},

	[NI_LIST_ALL]           = {ni_json_n,                           ni_user_v(NULL, 0, "\n", NULL)},
	[NI_LIST_RXQ]           = {ni_json_k("rxq"),                    ni_user_f(NULL, NI_USER_LIST_F_NO_LABEL)},
	[NI_SEC_RXQ]            = {ni_json_e,                           ni_user_l("RX")},
	[NI_CTRL_INDEX]         = {ni_json_k("id"),                     ni_user_f(" ", NUF_NDA | NUF_SL)},
	[NI_CTRL_NAME]          = {ni_json_k("type"),                   ni_user_v(" ", NUF_NDA | NUF_SL, NULL, " controller")},
	[NI_CTRL_REG_CTL]       = {ni_json_k("reg_control"),            ni_user_f("Control reg", NUF_N | NUFW(8))},
	[NI_CTRL_REG_CTL_R]     = {ni_json_k("run"),                    ni_user_v(NULL, NUF_DA | NUFW(-8), " | ", " |")},
	[NI_CTRL_REG_CTL_RN]    = {ni_json_k("run"),                    ni_user_v(NULL, NUF_NDA | NUFW(-8), " | ", NULL)},
	[NI_CTRL_REG_CTL_D_RX]  = {ni_json_k("discard"),                ni_user_v(NULL, NUF_NDA | NUFW(-8), " | ", NULL)},
	[NI_CTRL_REG_CTL_E]     = {ni_json_k("pciep_mask"),             ni_user_v(NULL, NUF_NDA | NUFW(2), " | EpMsk ", NULL)},
	[NI_CTRL_REG_CTL_V]     = {ni_json_k("vfid"),                   ni_user_v(NULL, NUF_DA | NUFW(2), " | VFID  ", " |")},
	[NI_CTRL_REG_STA]       = {ni_json_k("reg_status"),             ni_user_f("Status reg", NUF_N | NUFW(8))},
	[NI_CTRL_REG_STA_R]     = {ni_json_k("running"),                ni_user_v(NULL, NUF_DA | NUFW(-8), " | ", " |")},
	[NI_CTRL_REG_STA_RN]    = {ni_json_k("running"),                ni_user_v(NULL, NUF_NDA | NUFW(-8), " | ", NULL)},
	[NI_CTRL_REG_STA_DE]    = {ni_json_k("desc_rdy"),               ni_user_v(NULL, NUF_NDA | NUFW(-8), " | ", NULL)},
	[NI_CTRL_REG_STA_DA]    = {ni_json_k("data_rdy"),               ni_user_v(NULL, NUF_NDA | NUFW(-8), " | ", NULL)},
	[NI_CTRL_REG_STA_RI]    = {ni_json_k("ring_rdy"),               ni_user_v(NULL, NUF_DA | NUFW(-8), " | ", " |")},

	[NI_CTRL_REG_SHP]       = {ni_json_k("shp"),                    ni_user_f("SW header pointer", NUFW(8))},
	[NI_CTRL_REG_HHP]       = {ni_json_k("hhp"),                    ni_user_f("HW header pointer", NUFW(8))},
	[NI_CTRL_REG_MHP]       = {ni_json_k("mhp"),                    ni_user_f("Header pointer mask", NUFW(8))},
	[NI_CTRL_HBS]           = {ni_json_k("hdr_buffer_size"),        ni_user_f("* Header buffer size", NUFW(-8))},
	[NI_CTRL_FB]            = {ni_json_k("hdr_buffer_free"),        ni_user_f("* Fillable headers in HW", NUFW(8))},
	[NI_CTRL_REG_SDP]       = {ni_json_k("sdp"),                    ni_user_f("SW descriptor pointer", NUFW(8))},
	[NI_CTRL_REG_HDP]       = {ni_json_k("hdp"),                    ni_user_f("HW descriptor pointer", NUFW(8))},
	[NI_CTRL_REG_MDP]       = {ni_json_k("mdp"),                    ni_user_f("Descriptor pointer mask", NUFW(8))},
	[NI_CTRL_DBS]           = {ni_json_k("desc_buffer_size"),       ni_user_f("* Descriptor buffer size", NUFW(8))},
	[NI_CTRL_FD]            = {ni_json_k("desc_free"),              ni_user_f("* Usable descriptors in HW", NUFW(8))},

	[NI_CTRL_REG_SP]        = {ni_json_k("sw_ptr"),                 ni_user_f("SW pointer", NUFW(8))},
	[NI_CTRL_REG_HP]        = {ni_json_k("hw_ptr"),                 ni_user_f("HW pointer", NUFW(8))},
	[NI_CTRL_REG_MP]        = {ni_json_k("ptr_mask"),               ni_user_f("Pointer mask", NUFW(8))},
	[NI_CTRL_BS]            = {ni_json_k("buffer_size"),            ni_user_l("* Buffer size")},
	[NI_CTRL_REG_TO]        = {ni_json_k("timeout"),                ni_user_f("Timeout reg", NUFW(8))},
	[NI_CTRL_REG_MR]        = {ni_json_k("max_request_size"),       ni_user_f("Max request", NUF_N | NUFW(8))},
	[NI_CTRL_MR]            = {ni_json_n,                           ni_user_v("", NUF_DA | NUFW(-8), " | ", NULL)},

	[NI_CTRL_REG_RECV]      = {ni_json_k("pass"),                   ni_user_l("Received")},
	[NI_CTRL_REG_RECV_B]    = {ni_json_k("pass_bytes"),             ni_user_l("Received bytes")},
	[NI_CTRL_REG_DISC]      = {ni_json_k("drop"),                   ni_user_l("Discarded")},
	[NI_CTRL_REG_DISC_B]    = {ni_json_k("drop_bytes"),             ni_user_l("Discarded bytes")},
	[NI_CTRL_REG_SENT]      = {ni_json_k("pass"),                   ni_user_l("Sent")},
	[NI_CTRL_REG_SENT_B]    = {ni_json_k("pass_bytes"),             ni_user_l("Sent bytes")},

	[NI_CTRL_REG_DESC_B]    = {ni_json_k("descriptor_base"),        ni_user_f("Desc base", NUFW(16))},
	[NI_CTRL_REG_HDR_B]     = {ni_json_k("hdr_base"),               ni_user_f("Header base", NUFW(16))},
	[NI_CTRL_REG_PTR_B]     = {ni_json_k("ptr_base"),               ni_user_f("Pointer base", NUFW(16))},

	[NI_LIST_TXQ]           = {ni_json_k("txq"),                    ni_user_f(NULL, NI_USER_LIST_F_NO_LABEL)},
	[NI_SEC_TXQ]            = {ni_json_e,                           ni_user_l("TX")},

	[NI_SEC_RXSUM]          = {ni_json_k("rxq_sum"),                ni_user_l("RX SUM")},
	[NI_SEC_TXSUM]          = {ni_json_k("txq_sum"),                ni_user_l("TX SUM")},
};

int fprint_size(FILE * f, unsigned long size);

int fprint_size_user(void *priv, int item, unsigned long size)
{
	struct ni_user_cbp *p = priv;
	(void) item;
	return fprint_size(p->f, size);
}

int fprint_size_json(void *priv, int item, unsigned long size)
{
	struct ni_json_cbp *p = priv;
	(void) item;
	return fprintf(p->f, "%lu", size);
}

int print_ctrl_reg_user(void *priv, int item, int val)
{
	struct ni_user_cbp *p = priv;
	const char *res = NULL;

	switch (item) {
	case NI_CTRL_REG_CTL_RN:
	case NI_CTRL_REG_CTL_R: res = val ? "Run" : "Stop"; break;

	case NI_CTRL_REG_STA_RN:
	case NI_CTRL_REG_STA_R: res = val ? "Running" : "Stopped"; break;

	case NI_CTRL_REG_STA_DE: res = val ? "Desc RDY" : "Desc  -"; break;
	case NI_CTRL_REG_STA_DA: res = val ? "Data RDY" : "Data  -"; break;
	case NI_CTRL_REG_STA_RI: res = val ? "SW RDY"   : "SW full"; break;
	case NI_CTRL_REG_CTL_D_RX: res = val ? "Discard" : "Block"; break;
	}

	if (res)
		return fprintf(p->f, "%*s", p->width, res);
	return 0;
}

int print_ctrl_reg_json(void *priv, int item, int val)
{
	struct ni_user_cbp *p = priv;
	(void) item;
	return fprintf(p->f, val ? "true": "false");
}

static int print_xreg_user(void *priv, int item, uint64_t val)
{
	struct ni_user_cbp *p = priv;
	int ret = 0;
	(void) item;

	ret += fprintf(p->f, "%0*" PRIX64, p->width, val);

	if (p->align > ret)
		ret += fprintf(p->f, "%*s", p->align - ret, "");
	return ret;
}

static int print_xreg_json(void *priv, int item, uint64_t val)
{
	struct ni_json_cbp *p = priv;
	(void) item;
	return fprintf(p->f, "%lu", val);
}

struct ni_dma_item_f_t {
	struct ni_common_item_callbacks c;
	int (*print_size)(void *, int, unsigned long);
	int (*print_xreg)(void *, int, uint64_t);
	int (*print_ctrl_reg)(void *, int, int);
};

struct ni_dma_item_f_t ni_dma_item_f[] = {
	[NI_DRC_USER] = {
		.c = ni_common_item_callbacks[NI_DRC_USER],
		.print_size = fprint_size_user,
		.print_xreg = print_xreg_user,
		.print_ctrl_reg = print_ctrl_reg_user,
	},
	[NI_DRC_JSON] = {
		.c = ni_common_item_callbacks[NI_DRC_JSON],
		.print_size = fprint_size_json,
		.print_xreg = print_xreg_json,
		.print_ctrl_reg = print_ctrl_reg_json,
	},
};

NI_DEFAULT_ITEMS(ni_dma_item_f_t, c.)

#define NI_DMA_ITEM(name, type, cbcall) NI_ITEM_CB(name, type, ni_dma_item_f_t, cbcall)

NI_DMA_ITEM(str_size, long long, print_size)
NI_DMA_ITEM(ctrl_reg, int, print_ctrl_reg)
NI_DMA_ITEM(xreg, uint64_t, print_xreg)


/*!
 * \brief Display usage of program
 */
// TODO add query help
void usage(const char *me, int verbose)
{
	printf("Usage: %s [-rtRvh] [-i index] [-d path]\n", me);
	printf("-d path         Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-i indexes      Controllers numbers to use - list or range, e.g. \"0-5,7\" [default: all]\n");
	printf("-r              Use RX DMA queues\n");
	printf("-t              Use TX DMA queues\n");
	printf("-R              Resets packet counters (use -RR for read & reset)\n");
	printf("-T              Print the sum of all counters of selected queues\n"
	       "                (use -TT to print each queue separately)\n");
	printf("-S ring_size    Set kernel ring buffer size (can be with K/M/G suffix)\n");
	printf("-B buffer_size  Set kernel buffer size (for single packet; DMA Medusa only)\n");
	printf("-C buffer_count Set kernel buffer count (replacement for ring_size; DMA Medusa only)\n");
	printf("-O initial_off  Set initial offset in ring buffer (first buffer offset; DMA Medusa only)\n");
	printf("-q query        Get specific informations%s\n", verbose ? "" : " (-v for more info)");
	if (verbose) {
		for (unsigned i = 0; i < NC_ARRAY_SIZE(queries); i++) {
			printf(" * %s\n", queries[i]);
		}
		printf(" example of usage: '-q rx_received,tx_sent'\n");
	}
	printf("-N netdev_drv   Perform a netdev command (add,del) on the selected indexes\n");
	printf("-j              Print output in JSON\n");
	printf("-v              Increase verbosity\n");
	printf("-h              Show this text\n");
	printf("\nExamples:\n");
	printf("nfb-dma -i0 -N ndp_netdev add               Create NDP based netdev\n");
//	printf("nfb-dma -i0-7 -N xdp add,id=1,eth=0,eth=1   Create XDP based netdev with rxq-txq pairs 0-7 and use also ethernet interface 0+1\n");
//	printf("nfb-dma -i0-7 -N xdp del,id=1               Delete XDP based netdev with id=1\n");
}

int set_ring_size(struct nfb_device *dev, int dir, int index, const char* csize, const char *target)
{
	int fd;
	char path_buffer[128];
	ssize_t ret, sz;

	snprintf(path_buffer, sizeof(path_buffer),
		"/sys/class/nfb/nfb%d/ndp/%cx%d/%s",
		nfb_get_system_id(dev), dir == 0 ? 'r' : 't', index, target);

	fd = open(path_buffer, O_RDWR);
	if (fd == -1)
		err(errno, "Can't set %s", target);
	sz = strlen(csize) + 1;
	ret = write(fd, csize, sz);
	if (ret != sz) {
		err(ret, "Can't set %s", target);
	}
	close(fd);
	return 0;
}

int cmd_ndp_netdev(struct nfb_device *dev, const char *ndd, const char *cmd, struct list_range *index_range)
{
	int fd;
	char buffer[128];
	ssize_t ret = 0, sz = 0;

	snprintf(buffer, sizeof(buffer),
		"/sys/class/nfb/nfb%d/%s/cmd",
		nfb_get_system_id(dev), ndd);

	fd = open(buffer, O_RDWR);
	if (fd == -1)
		err(errno, "Can't perform ndp_netdev %s", cmd);

	if (strcmp(ndd, "ndp_netdev") == 0) {
		if (index_range->items == 1 && index_range->min[0] == index_range->max[0]) {
			sz = snprintf(buffer, sizeof(buffer), "cmd=%s,index=%d", cmd, index_range->min[0]);
		} else {
			ret = -EINVAL;
		}
	}

	if (sz == 0) {
		ret = -ENXIO;
	} else {
		sz++;
		ret = write(fd, buffer, sz);
	}

	if (ret != sz) {
		err(ret, "Can't perform ndp_netdev %s", cmd);
	}
	close(fd);
	return ret < 0 ? ret : 0;
}


/*!
 * \brief Convert and display number of bits in kB or MB
 *
 * \param size       size of buffer
 */
int fprint_size(FILE * f, unsigned long size)
{
	static const char *units[] = {
		"B", "KiB", "MiB", "GiB"};
	unsigned int i = 0;
	while (size >= 1024 && i < NC_ARRAY_SIZE(units)) {
		size >>= 10;
		i++;
	}
	return fprintf(f, "%lu %s", size, units[i]);
}

int print_size(unsigned long size)
{
	return fprint_size(stdout, size);
}

/*!
 * \brief Query print function for RX and TX
 */
int query_print(struct nfb_device *dev, struct list_range index_range,
	char *queries, int size)
{
	struct nc_rxqueue *rxq;
	struct nc_txqueue *txq = NULL;
	struct nc_rxqueue_counters cr = {0, };
	struct nc_txqueue_counters ct = {0, };
	int rx_size = ndp_get_rx_queue_count(dev);
	int tx_size = ndp_get_tx_queue_count(dev);
	int max = tx_size < rx_size ? rx_size : tx_size;
	int rxc_valid, txc_valid;

	for (int i = 0; i < max; ++i) {
		if (list_range_empty(&index_range) || list_range_contains(&index_range, i)) {
			rxc_valid = 0;
			txc_valid = 0;

			for (int j = 0; j < size; ++j) {
				switch (queries[j]) {
					case RX_RECEIVED:
					case RX_DISCARDED:
					case RX_RECEIVED_BYTES:
					case RX_DISCARDED_BYTES:
						if (rxc_valid)
							break;

						rxc_valid = 1;
						rxq = nc_rxqueue_open_index(dev, i, QUEUE_TYPE_UNDEF);
						if (rxq == NULL) {
							if (ndp_rx_queue_is_available(dev, i)) {
								warnx("problem opening rx_queue");
							} else {
								warnx("rx_queue doesn't exist");
							}
							return EXIT_FAILURE;
						}
						nc_rxqueue_read_counters(rxq, &cr);
						nc_rxqueue_close(rxq);
						break;

					case TX_SENT:
					case TX_SENT_BYTES:
					case TX_DISCARDED:
					case TX_DISCARDED_BYTES:
						if (txc_valid)
							break;

						txc_valid = 1;
						txq = nc_txqueue_open_index(dev, i, QUEUE_TYPE_UNDEF);
						if (txq == NULL) {
							if (ndp_tx_queue_is_available(dev, i)) {
								warnx("problem opening tx_queue");

							} else {
								warnx("tx_queue doesn't exist");
							}
							return EXIT_FAILURE;
						}
						nc_txqueue_read_counters(txq, &ct);
						nc_txqueue_close(txq);
						break;

					default: break;
				}

				switch (queries[j]) {
					case RX_RECEIVED:
						printf("%llu\n", cr.received); break;
					case RX_RECEIVED_BYTES:
						if (!cr.have_bytes)
							warnx("queue doesn't have byte counter");
						printf("%llu\n", cr.received_bytes); break;
					case RX_DISCARDED:
						printf("%llu\n", cr.discarded); break;
					case RX_DISCARDED_BYTES:
						if (!cr.have_bytes)
							warnx("queue doesn't have byte counter");
						printf("%llu\n", cr.discarded_bytes); break;
					case TX_SENT:
						printf("%llu\n", ct.sent); break;
					case TX_SENT_BYTES:
						if (!ct.have_bytes)
							warnx("queue doesn't have byte counter");
						printf("%llu\n", ct.sent_bytes); break;
					case TX_DISCARDED:
						if (!ct.have_tx_discard)
							warnx("queue doesn't have TX discard counter");
						printf("%llu\n", ct.discarded); break;
					case TX_DISCARDED_BYTES:
						if (!ct.have_tx_discard)
							warnx("queue doesn't have TX discard counter");
						printf("%llu\n", ct.discarded_bytes); break;
					default: break;
				}
			}
		}
	}
	return 0;
}

void rxqueue_get_status(struct nc_rxqueue *q, struct nc_rxqueue_status *s, struct nc_rxqueue_counters *c, enum commands cmd)
{
	if (s) {
		nc_rxqueue_read_status(q, s);
	}

	if (c) {
		if (cmd == CMD_COUNTER_READ_AND_RESET) {
			if (nc_rxqueue_read_and_reset_counters(q, c) == -ENXIO) {
				warnx("controller doesn't support atomic read & reset, command will be done non-atomically");
				nc_rxqueue_read_counters(q, c);
				nc_rxqueue_reset_counters(q);
			}
		} else {
			nc_rxqueue_read_counters(q, c);
		}
	}
}

/*!
 * \brief Print values from TX_DMA_INFO structure
 *
 * \param *tx_dma    structure from which are data read
 */
void rxqueue_print_status(struct ni_context *ctx, struct nc_rxqueue *q, int index, int verbose, struct nc_rxqueue_status *s, struct nc_rxqueue_counters *c)
{
	ni_item_int(ctx, NI_CTRL_INDEX, index);
	ni_item_str(ctx, NI_CTRL_NAME, q->name);

	if (s && verbose > 0) {
		ni_item_uint64_tx(ctx, NI_CTRL_REG_CTL, s->_ctrl_raw);

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_CTL_R, s->ctrl_running);
		} else if (q->type == QUEUE_TYPE_SZE) {
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_CTL_RN, s->ctrl_running);
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_CTL_D_RX, s->ctrl_discard);
			ni_item_xreg(ctx, NI_CTRL_REG_CTL_E, (s->_ctrl_raw >> 16) & 0xFF);
			ni_item_xreg(ctx, NI_CTRL_REG_CTL_V, (s->_ctrl_raw >> 24) & 0xFF);
		}

		ni_item_uint64_tx(ctx, NI_CTRL_REG_STA, s->_stat_raw);

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_STA_R, s->stat_running);
		} else if (q->type == QUEUE_TYPE_SZE) {
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_STA_RN, s->stat_running);
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_STA_DE, s->stat_desc_rdy);
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_STA_DA, s->stat_data_rdy);
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_STA_RI, s->stat_ring_rdy);
		}

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			ni_item_uint64_tx(ctx, NI_CTRL_REG_SHP, s->sw_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_HHP, s->hw_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MHP, s->pointer_mask);

			ni_item_str_size(ctx, NI_CTRL_HBS, (s->pointer_mask ? s->pointer_mask + 1 : 0));
			ni_item_uint64_tx(ctx, NI_CTRL_FB, (s->sw_pointer - s->hw_pointer - 1) & s->pointer_mask);

			ni_item_uint64_tx(ctx, NI_CTRL_REG_SDP, s->sd_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_HDP, s->hd_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MDP, s->desc_pointer_mask);

			ni_item_str_size(ctx, NI_CTRL_DBS, s->desc_pointer_mask ? s->desc_pointer_mask + 1 : 0);
			ni_item_uint64_tx(ctx, NI_CTRL_FD, (s->sd_pointer - s->hd_pointer) & s->desc_pointer_mask);
		} else {
			ni_item_uint64_tx(ctx, NI_CTRL_REG_SP, s->sw_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_HP, s->hw_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MP, s->pointer_mask);
			ni_item_str_size(ctx, NI_CTRL_BS, s->pointer_mask ? s->pointer_mask + 1 : 0);
		}

		if (q->type == QUEUE_TYPE_SZE || q->type == QUEUE_TYPE_NDP)
			ni_item_uint64_tx(ctx, NI_CTRL_REG_TO, s->timeout);

		if (q->type == QUEUE_TYPE_SZE) {
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MR, s->max_request);
			ni_item_str_size(ctx, NI_CTRL_MR, s->max_request);
		}
	}

	if (c) {
		ni_item_uint64_t(ctx, NI_CTRL_REG_RECV, c->received);
		if (c->have_bytes)
			ni_item_uint64_t(ctx, NI_CTRL_REG_RECV_B, c->received_bytes);
		ni_item_uint64_t(ctx, NI_CTRL_REG_DISC, c->discarded);
		if (c->have_bytes)
			ni_item_uint64_t(ctx, NI_CTRL_REG_DISC_B, c->discarded_bytes);
	}
	if (s && verbose > 1) {
		ni_item_uint64_tx(ctx, NI_CTRL_REG_DESC_B, s->desc_base);
		if (q->type == QUEUE_TYPE_CALYPTE)
			ni_item_uint64_tx(ctx, NI_CTRL_REG_HDR_B, s->hdr_base);
		else
			ni_item_uint64_tx(ctx, NI_CTRL_REG_PTR_B, s->pointer_base);
	}
}

void txqueue_get_status(struct nc_txqueue *q, struct nc_txqueue_status *s, struct nc_txqueue_counters *c, enum commands cmd)
{
	if (s) {
		nc_txqueue_read_status(q, s);
	}

	if (c) {
		if (cmd == CMD_COUNTER_READ_AND_RESET) {
			if (nc_txqueue_read_and_reset_counters(q, c) == -ENXIO) {
				warnx("controller doesn't support atomic read & reset, command will be done non-atomically");
				nc_txqueue_read_counters(q, c);
				nc_txqueue_reset_counters(q);
			}
		} else {
			nc_txqueue_read_counters(q, c);
		}
	}
}

void txqueue_print_status(struct ni_context *ctx, struct nc_txqueue *q, int index, int verbose, struct nc_txqueue_status *s, struct nc_txqueue_counters *c)
{
	ni_item_int(ctx, NI_CTRL_INDEX, index);
	ni_item_str(ctx, NI_CTRL_NAME, q->name);

	if (s && verbose > 0) {
		ni_item_uint64_tx(ctx, NI_CTRL_REG_CTL, s->_ctrl_raw);

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_CTL_R, s->ctrl_running);
		} else if (q->type == QUEUE_TYPE_SZE) {
			ni_item_ctrl_reg(ctx, NI_CTRL_REG_CTL_RN, s->ctrl_running);
			ni_item_xreg(ctx, NI_CTRL_REG_CTL_E, (s->_ctrl_raw >> 16) & 0xFF);
			ni_item_xreg(ctx, NI_CTRL_REG_CTL_V, (s->_ctrl_raw >> 24) & 0xFF);
		}

		ni_item_uint64_tx(ctx, NI_CTRL_REG_STA, s->_stat_raw);
		ni_item_ctrl_reg(ctx, NI_CTRL_REG_STA_R, s->stat_running);

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			if (q->type == QUEUE_TYPE_CALYPTE) {
				ni_item_uint64_tx(ctx, NI_CTRL_REG_SHP, s->sw_pointer);
				ni_item_uint64_tx(ctx, NI_CTRL_REG_HHP, s->hw_pointer);
				ni_item_uint64_tx(ctx, NI_CTRL_REG_MHP, s->pointer_mask);
				ni_item_str_size(ctx, NI_CTRL_HBS, (s->pointer_mask ? s->pointer_mask + 1 : 0));
				ni_item_uint64_tx(ctx, NI_CTRL_FB, (s->sw_pointer - s->hw_pointer - 1) & s->pointer_mask);
			}

			ni_item_uint64_tx(ctx, NI_CTRL_REG_SDP, s->sd_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_HDP, s->hd_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MDP, s->desc_pointer_mask);
			ni_item_str_size(ctx, NI_CTRL_DBS, s->desc_pointer_mask ? s->desc_pointer_mask + 1 : 0);
			ni_item_uint64_tx(ctx, NI_CTRL_FD, (s->sd_pointer - s->hd_pointer) & s->desc_pointer_mask);
		} else {
			ni_item_uint64_tx(ctx, NI_CTRL_REG_SP, s->sw_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_HP, s->hw_pointer);
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MP, s->pointer_mask);
			ni_item_str_size(ctx, NI_CTRL_BS, s->pointer_mask ? s->pointer_mask + 1 : 0);
		}

		if (q->type == QUEUE_TYPE_SZE || q->type == QUEUE_TYPE_NDP)
			ni_item_uint64_tx(ctx, NI_CTRL_REG_TO, s->timeout);

		if (q->type == QUEUE_TYPE_SZE) {
			ni_item_uint64_tx(ctx, NI_CTRL_REG_MR, s->max_request);
			ni_item_str_size(ctx, NI_CTRL_MR, s->max_request);
		}
	}

	if (c) {
		ni_item_uint64_t(ctx, NI_CTRL_REG_SENT, c->sent);
		if (c->have_bytes)
			ni_item_uint64_t(ctx, NI_CTRL_REG_SENT_B, c->sent_bytes);

		if (c->have_tx_discard) {
			ni_item_uint64_t(ctx, NI_CTRL_REG_DISC, c->discarded);
			if (c->have_bytes)
				ni_item_uint64_t(ctx, NI_CTRL_REG_DISC_B, c->discarded_bytes);
		}
	}

	if (s && verbose > 1 && q->type != QUEUE_TYPE_CALYPTE) {
		ni_item_uint64_tx(ctx, NI_CTRL_REG_DESC_B, s->desc_base);
		ni_item_uint64_tx(ctx, NI_CTRL_REG_PTR_B, s->pointer_base);
	}
}

/*!
 * \brief Program main function.
 *
 * \param argc       number of arguments
 * \param *argv[]    array with arguments
 *
 * \return     0 OK<BR>
 *             -1 error
 */
int main(int argc, char *argv[])
{
	unsigned int ctrl;
	int i;
	int fdt_offset;
	int dir = -1;
	enum commands cmd = CMD_PRINT_STATUS;
	int verbose = 0;
	int sum = 0;
	int js = NI_DRC_USER;

	int ret;
	const char *query = NULL;
	const char *csize = NULL;
	char *queries_index;
	int size;

	char c;
	const char *file = nfb_default_dev_path();
	const char *netdev_cmd = "";

	struct nfb_device *dev;
	struct nc_rxqueue *rxq;
	struct nc_txqueue *txq;
	struct list_range index_range;

	struct nc_rxqueue_counters cntr_rx;
	struct nc_txqueue_counters cntr_tx;
	struct nc_rxqueue_counters sum_rx = {0, };
	struct nc_txqueue_counters sum_tx = {0, };

	struct nc_rxqueue_status stat_rx;
	struct nc_txqueue_status stat_tx;

	list_range_init(&index_range);

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'R':
			if (cmd == CMD_COUNTER_RESET)
				cmd = CMD_COUNTER_READ_AND_RESET;
			else
				cmd = CMD_COUNTER_RESET;
			break;
		case 'd':
			file = optarg;
			break;
		case 'h':
			cmd = CMD_USAGE;
			break;
		case 'v':
			verbose++;
			break;
		case 'i':
			if (list_range_parse(&index_range, optarg) < 0)
				errx(EXIT_FAILURE, "Cannot parse interface number.");
			break;
		case 'j':
			js = NI_DRC_JSON;
			break;
		case 'q':
			cmd = CMD_QUERY;
			query = optarg;
			break;
		case 't':
			dir = 1;
			break;
		case 'r':
			dir = 0;
			break;
		case 'T':
			sum++;
			break;
		case 'S':
			cmd = CMD_SET_RING_SIZE;
			csize = optarg;
			break;
		case 'B':
			cmd = CMD_SET_BUFFER_SIZE;
			csize = optarg;
			break;
		case 'C':
			cmd = CMD_SET_BUFFER_COUNT;
			csize = optarg;
			break;
		case 'O':
			cmd = CMD_SET_INITIAL_OFFSET;
			csize = optarg;
			break;
		case 'N':
			cmd = CMD_NETDEV;
			netdev_cmd = optarg;
			break;
		default:
			err(-EINVAL, "Unknown argument -%c", optopt);
		}
	}

	if (cmd == CMD_USAGE) {
		usage(argv[0], verbose);
		list_range_destroy(&index_range);
		return EXIT_SUCCESS;
	}

	argc -= optind;
	argv += optind;

	if (cmd == CMD_NETDEV && argc < 1) {
		err(errno, "Missing netdev command argument");
	} else if ((cmd == CMD_NETDEV && argc > 1) || (cmd != CMD_NETDEV && argc)) {
		err(errno, "Stray arguments %d", argc);
	}

	dev = nfb_open(file);
	if (dev == NULL) {
		err(errno, "Can't open NFB device");
	}

	if (cmd == CMD_NETDEV) {
		return cmd_ndp_netdev(dev, netdev_cmd, argv[0], &index_range);
	}

	if (query) {
		size = nc_query_parse(query, queries, NC_ARRAY_SIZE(queries), &queries_index);
		if (size <= 0) {
			nfb_close(dev);
			return -1;
		}
		ret = query_print(dev, index_range, queries_index, size);
		free(queries_index);
		nfb_close(dev);
		list_range_destroy(&index_range);
		if (ret)
			return ret;
		return EXIT_SUCCESS;
	}

	struct ni_context *ctx = NULL;
	if (cmd == CMD_COUNTER_READ_AND_RESET || cmd == CMD_PRINT_STATUS) {
		ctx = ni_init_root_context_default(js, ni_items, &ni_dma_item_f[js]);
	}

	ni_section(ctx, NI_SEC_ROOT);

	ni_list(ctx, NI_LIST_ALL);
	if (dir == -1 || dir == 0) {
		if (sum != 1)
			ni_list(ctx, NI_LIST_RXQ);
		for (ctrl = 0; ctrl < NC_ARRAY_SIZE(rx_ctrl_name); ctrl++) {
			i = 0;
			fdt_for_each_compatible_node(nfb_get_fdt(dev), fdt_offset, rx_ctrl_name[ctrl]) {
				if (list_range_empty(&index_range) || list_range_contains(&index_range, i)) {
					if (sum != 1)
						ni_section(ctx, NI_SEC_RXQ);
					rxq = nc_rxqueue_open(dev, fdt_offset);
					if (rxq) {
						switch (cmd) {
						case CMD_COUNTER_RESET:
							nc_rxqueue_reset_counters(rxq);
							break;
						case CMD_COUNTER_READ_AND_RESET:
						case CMD_PRINT_STATUS:
							rxqueue_get_status(rxq, verbose ? &stat_rx : NULL, &cntr_rx, cmd);
							if (sum != 1) {
								rxqueue_print_status(ctx, rxq, i, verbose, verbose ? &stat_rx : NULL, &cntr_rx);
							}
							sum_rx.received += cntr_rx.received;
							sum_rx.received_bytes += cntr_rx.received_bytes;

							sum_rx.discarded += cntr_rx.discarded;
							sum_rx.discarded_bytes += cntr_rx.discarded_bytes;
							break;
						case CMD_SET_RING_SIZE:
							set_ring_size(dev, 0, i, csize, "ring_size");
							break;
						case CMD_SET_BUFFER_SIZE:
							set_ring_size(dev, 0, i, csize, "buffer_size");
							break;
						case CMD_SET_BUFFER_COUNT:
							set_ring_size(dev, 0, i, csize, "buffer_count");
							break;
						case CMD_SET_INITIAL_OFFSET:
							set_ring_size(dev, 0, i, csize, "initial_offset");
							break;

						default:
							break;
						}
						nc_rxqueue_close(rxq);
					}
					if (sum != 1)
						ni_endsection(ctx, NI_SEC_RXQ);
				}
				i++;
			}
		}
		if (sum != 1)
			ni_endlist(ctx, NI_LIST_RXQ);
	}
	if (dir == -1 || dir == 1) {
		if (sum != 1)
			ni_list(ctx, NI_LIST_TXQ);
		for (ctrl = 0; ctrl < NC_ARRAY_SIZE(tx_ctrl_name); ctrl++) {
			i = 0;
			fdt_for_each_compatible_node(nfb_get_fdt(dev), fdt_offset, tx_ctrl_name[ctrl]) {
				if (list_range_empty(&index_range) || list_range_contains(&index_range, i)) {
					if (sum != 1)
						ni_section(ctx, NI_SEC_TXQ);
					txq = nc_txqueue_open(dev, fdt_offset);
					if (txq) {
						switch (cmd) {
						case CMD_COUNTER_RESET:
							nc_txqueue_reset_counters(txq);
							break;
						case CMD_COUNTER_READ_AND_RESET:
						case CMD_PRINT_STATUS:
							txqueue_get_status(txq, verbose ? &stat_tx : NULL, &cntr_tx, cmd);
							if (sum != 1) {
								txqueue_print_status(ctx, txq, i, verbose, verbose ? &stat_tx : NULL, &cntr_tx);
							}
							sum_tx.sent += cntr_tx.sent;
							sum_tx.sent_bytes += cntr_tx.sent_bytes;
							break;
						case CMD_SET_RING_SIZE:
							if (txq->type == QUEUE_TYPE_CALYPTE)
								err(errno, "TX Calypte controller does not support setting of ring buffer size.");
							set_ring_size(dev, 1, i, csize, "ring_size");
							break;
						case CMD_SET_BUFFER_SIZE:
							set_ring_size(dev, 1, i, csize, "buffer_size");
							break;
						case CMD_SET_BUFFER_COUNT:
							set_ring_size(dev, 1, i, csize, "buffer_count");
							break;
						case CMD_SET_INITIAL_OFFSET:
							set_ring_size(dev, 1, i, csize, "initial_offset");
							break;
						default:
							break;
						}
						nc_txqueue_close(txq);
					}
					if (sum != 1)
						ni_endsection(ctx, NI_SEC_TXQ);
				}
				i++;
			}
		}
		if (sum != 1)
			ni_endlist(ctx, NI_LIST_TXQ);
	}

	if (sum && (dir == -1 || dir == 0)) {
		ni_section(ctx, NI_SEC_RXSUM);
		ni_item_uint64_t(ctx, NI_CTRL_REG_RECV, sum_rx.received);
		if (cntr_rx.have_bytes)
			ni_item_uint64_t(ctx, NI_CTRL_REG_RECV_B, sum_rx.received_bytes);
		ni_item_uint64_t(ctx, NI_CTRL_REG_DISC, sum_rx.discarded);
		if (cntr_rx.have_bytes)
			ni_item_uint64_t(ctx, NI_CTRL_REG_DISC_B, sum_rx.discarded_bytes);
		ni_endsection(ctx, NI_SEC_RXSUM);
	}

	if (sum && (dir == -1 || dir == 1)) {
		ni_section(ctx, NI_SEC_TXSUM);
		ni_item_uint64_t(ctx, NI_CTRL_REG_SENT, sum_tx.sent);
		if (cntr_tx.have_bytes)
			ni_item_uint64_t(ctx, NI_CTRL_REG_SENT_B, sum_tx.sent_bytes);

		if (cntr_tx.have_tx_discard) {
			ni_item_uint64_t(ctx, NI_CTRL_REG_DISC, sum_tx.discarded);
			if (cntr_tx.have_bytes)
				ni_item_uint64_t(ctx, NI_CTRL_REG_DISC_B, sum_tx.discarded_bytes);
		}
		ni_endsection(ctx, NI_SEC_TXSUM);
	}

	ni_endlist(ctx, NI_LIST_ALL);

	ni_endsection(ctx, NI_SEC_ROOT);
	ni_close_root_context(ctx);

	nfb_close(dev);

	list_range_destroy(&index_range);

	return EXIT_SUCCESS;
}
