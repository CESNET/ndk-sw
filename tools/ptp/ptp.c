/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Experimental tool connecting pairs of (RX,TX) channels in a Peer-to-Peer manner
 *
 * Copyright (C) 2025-2025 Universitaet Heidelberg, Institut fuer Technische Informatik (ZITI)
 * Author(s):
 *     Vladislav Valek <vladislav.valek@stud.uni-heidelberg.de>
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <signal.h>
#include <libfdt.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <netcope/nccommon.h>
#include <netcope/dma_ctrl_ndp.h>

#define DMA_COMP_OFFS  0x1000000
#define RXDMA_OFFS     0x0
#define TXDMA_OFFS     0x200000

#define REGS_SIZE    0x80
#define TX_BUFF_SIZE 8192

#define TX_HDR_BUFF_OFFS(ch) ((ch) * TX_BUFF_SIZE * 2)

#define RX_CTRL_CP       "cesnet,dma_ctrl_calypte_rx"
#define TX_CTRL_CP       "cesnet,dma_ctrl_calypte_tx"
#define TX_DATA_BUFF_CP  "cesnet,dma_calypte_tx_data_buff"
#define TX_HDR_BUFF_CP   "cesnet,dma_calypte_tx_hdr_buff"

#define ARGUMENTS	"i:r:t:p:hs:"

struct ptp_probe_ctx {
	struct nfb_device *rxdev;
	uint64_t rx_bars[2];

	struct nfb_device *txdev;
	uint64_t tx_bars[2];

	struct nfb_comp *rx_gen;
	struct nfb_comp *rx_gen_mux;
	uint16_t chan_min;
	uint16_t chan_max;
};

volatile int stop = 0;

static void sig_usr(int signo)
{
	if (signo == SIGINT || signo == SIGTERM) {
		stop = 1;
	}
}

void usage(const char *me)
{
	printf("Usage: %s [-r path] [-t path] [-i index]\n", me);
	printf("-s size         Packet size - one specific packet size\n");
	printf("-i indexes      List of communicating channels\n");
	printf("-r path         Path to a transmitting device [default: %s]\n", nfb_default_dev_path());
	printf("-t path         Path to a receiving device [default: %s]\n", nfb_default_dev_path());
	printf("-p packets      Stop tranfering after <packets> packets");
	printf("-h              Show this text\n");
}

int find_dev_bar_addrs(struct nfb_device *dev, uint64_t *bar0_addr, uint64_t *bar2_addr)
{
	const void *fdt = nfb_get_fdt(dev);
	int bar0_fdt_off = fdt_path_offset(fdt, "/drivers/mi/PCI0,BAR0");
	int bar2_fdt_off = fdt_path_offset(fdt, "/drivers/mi/PCI0,BAR2");
	*bar0_addr = fdt_getprop_u64(fdt, bar0_fdt_off, "phys_base", NULL);
	*bar2_addr = fdt_getprop_u64(fdt, bar2_fdt_off, "phys_base", NULL);

	if (*bar0_addr == 0 || *bar2_addr == 0)
		return -2;

	return 0;
}

int chan_start(struct nfb_comp *chan_ctrl)
{
	nfb_comp_write8(chan_ctrl, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_START);

	for (int attempts = 0; attempts < 100; attempts++) {
		if (nfb_comp_read8(chan_ctrl, NDP_CTRL_REG_STATUS) == NDP_CTRL_REG_STATUS_RUNNING)
			return 0;
		usleep(1);
	}

	return -1;
}

int chan_wait_stop(struct nfb_comp *chan_ctrl)
{
	for (int attempts = 0; attempts < 100; attempts++) {
		if (nfb_comp_read8(chan_ctrl, NDP_CTRL_REG_STATUS) != NDP_CTRL_REG_STATUS_RUNNING) {
			nfb_comp_write8(chan_ctrl, NDP_CTRL_REG_EXPER, 0);
			return 0;
		}
		usleep(1);
	}

	return -1;
}

int disable_channels(struct ptp_probe_ctx *ctx, struct list_range* lr)
{
	int rx_size = ndp_get_rx_queue_count(ctx->rxdev);
	int tx_size = ndp_get_tx_queue_count(ctx->txdev);
	int max = tx_size < rx_size ? rx_size : tx_size;

	int ret = 0;
	int node;
	struct nfb_comp *rx_chan;
	struct nfb_comp *tx_chan;

	for (int i = 0; i < max; ++i) {
		if (list_range_empty(lr) || list_range_contains(lr, i)) {
			node = nfb_comp_find(ctx->rxdev, RX_CTRL_CP, i);
			rx_chan = nfb_comp_open(ctx->rxdev, node);
			if (rx_chan == NULL) {
				err(errno, "Fail to open RX Control component %d\n", i);
				return -1;
			}
			nfb_comp_write8(rx_chan, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_STOP);

			usleep(500);

			node = nfb_comp_find(ctx->txdev, TX_CTRL_CP, i);
			tx_chan = nfb_comp_open(ctx->txdev, node);
			if (tx_chan == NULL) {
				err(errno, "Fail to open TX Control component %d\n", i);
				return -1;
			}
			nfb_comp_write8(tx_chan, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_STOP);

			ret = chan_wait_stop(tx_chan);
			if (ret) {
				warn("Unable to stop TX channel %d\n", i);
			}

			ret = chan_wait_stop(rx_chan);
			if (ret) {
				warn("Unable to stop RX channel %d\n", i);
			}
			nfb_comp_close(tx_chan);
			nfb_comp_close(rx_chan);
		}
	}
	return 0;
}

int configure_channels(struct ptp_probe_ctx *ctx, struct list_range* lr)
{
	int rx_size = ndp_get_rx_queue_count(ctx->rxdev);
	int tx_size = ndp_get_tx_queue_count(ctx->txdev);
	int max = tx_size < rx_size ? rx_size : tx_size;

	int ret = 0;
	int node;
	struct nfb_comp *comp;
	uint64_t ptr_offs;
	uint64_t data_buff_offs;
	uint64_t hdr_buff_offs;
	ctx->chan_min = 0;
	ctx->chan_max = 0;

	for (int i = 0; i < max; ++i) {
		if (list_range_empty(lr) || list_range_contains(lr, i)) {

			if (i == ctx->chan_max + 1)
				ctx->chan_max = i;

			node = nfb_comp_find(ctx->txdev, TX_CTRL_CP, i);
			comp = nfb_comp_open(ctx->txdev, node);
			if (comp == NULL) {
				err(errno, "Fail to open TX Control component %d\n", i);
				return -1;
			}
			ptr_offs = ctx->rx_bars[0] + DMA_COMP_OFFS + RXDMA_OFFS + i*REGS_SIZE + NDP_CTRL_REG_SDP;
			printf("Writing RX UPD Buff addr: %lx to TX channel %d\n", ptr_offs, i);
			nfb_comp_write64(comp, NDP_CTRL_REG_UPDATE_BASE, ptr_offs);
			nfb_comp_write8(comp, NDP_CTRL_REG_EXPER, 1);
			nfb_comp_write32(comp, NDP_CTRL_REG_TIMEOUT, 0x4000);
			nfb_comp_write64(comp, NDP_CTRL_REG_SDP, 0);

			ret = chan_start(comp);
			if (ret) {
				warn("Unable to start TX channel %d\n", i);
			}
			nfb_comp_close(comp);

			node = nfb_comp_find(ctx->rxdev, RX_CTRL_CP, i);
			comp = nfb_comp_open(ctx->rxdev, node);
			if (comp == NULL) {
				err(errno, "Fail to open RX Control component %d\n", i);
				return -1;
			}
			ptr_offs = ctx->tx_bars[0] + DMA_COMP_OFFS + TXDMA_OFFS + i*REGS_SIZE + NDP_CTRL_REG_SDP;
			printf("Writing TX UPD Buff addr: %lx to RX channel %d\n", ptr_offs, i);
			nfb_comp_write64(comp, NDP_CTRL_REG_UPDATE_BASE, ptr_offs);

			data_buff_offs = ctx->tx_bars[1] + i*TX_BUFF_SIZE*2;
			printf("Writing TX DATA Buff addr: %lx to RX channel %d\n", data_buff_offs, i);
			nfb_comp_write64(comp, NDP_CTRL_REG_DESC_BASE, data_buff_offs);

			hdr_buff_offs = ctx->tx_bars[1] + TX_HDR_BUFF_OFFS(tx_size) + i*TX_BUFF_SIZE*2;
			printf("Writing TX HDR Buff addr: %lx to RX channel %d\n", hdr_buff_offs, i);
			nfb_comp_write64(comp, NDP_CTRL_REG_HDR_BASE, hdr_buff_offs);

			// NOTE: It should be probably copied from the TX Channel's configuration registers
			nfb_comp_write16(comp, NDP_CTRL_REG_MDP, 0x003F);
			nfb_comp_write16(comp, NDP_CTRL_REG_MHP, 0x03FF);
			nfb_comp_write8(comp, NDP_CTRL_REG_EXPER, 1);
			nfb_comp_write32(comp, NDP_CTRL_REG_TIMEOUT, 0x4000);
			nfb_comp_write64(comp, NDP_CTRL_REG_SDP, 0);

			ret = chan_start(comp);
			if (ret) {
				warn("Unable to start RX channel %d\n", i);
			}
			nfb_comp_close(comp);
		}
	}
	printf("Maximal channel is %d\n", ctx->chan_max);
	return 0;
}

#define GEN_REG_CTRL          0x0
#define GEN_REG_LEN           0x04
#define GEN_REG_CHAN_INCR     0x08
#define GEN_REG_CHAN_MIN_MAX  0x0C
#define GEN_REG_CNTR_L        0x20
#define GEN_REG_CNTR_H        0x24

void disable_mfb_gen(struct ptp_probe_ctx *ctx)
{
	nfb_comp_write8(ctx->rx_gen, GEN_REG_CTRL, 0);
	nfb_comp_close(ctx->rx_gen);
	nfb_comp_write8(ctx->rx_gen_mux, 0x8, 0);
	nfb_comp_close(ctx->rx_gen_mux);
}

int run_mfb_gen(struct ptp_probe_ctx *ctx, unsigned long long pkt_len, unsigned long long limit_packets)
{
	int node_offs;
	const void *fdt = nfb_get_fdt(ctx->rxdev);
	uint32_t chan_min_max_reg;
	uint32_t chan_incr_reg;

	node_offs = fdt_path_offset(fdt, "/firmware/mi_pci0_bar0/dbg_gls0/mfb_gen2dma");
	ctx->rx_gen = nfb_comp_open(ctx->rxdev, node_offs);
	if (ctx->rx_gen == NULL) {
		err(errno, "Fail to open MFB Generator component\n");
		return -1;
	}
	node_offs = nfb_comp_find(ctx->rxdev, "cesnet,ofm,gen_loop_switch", 0);
	ctx->rx_gen_mux = nfb_comp_open(ctx->rxdev, node_offs);
	if (ctx->rx_gen_mux == NULL) {
		err(errno, "Fail to open GLS component\n");
		return -1;
	}
	// Switch multiplexer so the generator is connected to the RX stream
	nfb_comp_write8(ctx->rx_gen_mux, 0x8, 1);

	nfb_comp_write32(ctx->rx_gen, GEN_REG_LEN, pkt_len);

	chan_min_max_reg = ((uint32_t)ctx->chan_max << 16) | ctx->chan_min;
	nfb_comp_write32(ctx->rx_gen, GEN_REG_CHAN_MIN_MAX, chan_min_max_reg);

	// Burst of 64 packets and increment channel by 1
	chan_incr_reg = ((uint32_t) limit_packets << 16) | 0x00000001;
	nfb_comp_write32(ctx->rx_gen, GEN_REG_CHAN_INCR, chan_incr_reg);
	nfb_comp_write8(ctx->rx_gen, GEN_REG_CTRL, 1);

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	char c;
	const char *rx_file = nfb_default_dev_path();
	const char *tx_file = nfb_default_dev_path();
	struct list_range index_range;
	unsigned long long limit_packets = 0;
	unsigned long long packet_size = 64;
	struct ptp_probe_ctx ctx = {0};

	list_range_init(&index_range);

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'r':
			tx_file = optarg;
			break;
		case 't':
			rx_file = optarg;
			break;
		case 'i':
			if (list_range_parse(&index_range, optarg) < 0)
				errx(EXIT_FAILURE, "Cannot parse interface number.");
			break;
		case 'h':
			usage(argv[0]);
			list_range_destroy(&index_range);
			return EXIT_SUCCESS;
			break;
		case 'p':
			if (nc_strtoull(optarg, &limit_packets))
				errx(-1, "Cannot parse packet limit parameter");
			break;
		case 's':
			if (nc_strtoull(optarg, &packet_size))
				errx(-1, "Cannot parse size parameter");
			break;
		default:
			err(-EINVAL, "Unknown argument -%c", optopt);
		}
	}

	ctx.rxdev = nfb_open(rx_file);
	if (ctx.rxdev == NULL) {
		err(errno, "Failed to open transmitting device.");
		goto rxdev_not_found;
	}

	if (strcmp(rx_file, tx_file)) {
		ctx.txdev = nfb_open(tx_file);
		if (ctx.txdev == NULL) {
			err(errno, "Failed to open receiving device.");
			goto txdev_not_found;
		}
	} else {
		ctx.txdev = ctx.rxdev;
	}

	ret |= find_dev_bar_addrs(ctx.txdev, &ctx.tx_bars[0], &ctx.tx_bars[1]);
	ret |= find_dev_bar_addrs(ctx.rxdev, &ctx.rx_bars[0], &ctx.rx_bars[1]);
	if (ret) {
		err(errno, "Unable to retrieve BAR addresses.\n");
		goto tx_bar_search_fail;
	}

	printf("BARs found: TX_BAR0: %lx, TX_BAR2: %lx\n", ctx.tx_bars[0], ctx.tx_bars[1]);
	printf("BARs found: RX_BAR0: %lx, RX_BAR2: %lx\n", ctx.rx_bars[0], ctx.rx_bars[1]);

	ret |= configure_channels(&ctx, &index_range);
	if (ret) {
		err(errno, "Unable to configure channels.\n");
		goto tx_bar_search_fail;
	}

	ret |= run_mfb_gen(&ctx, packet_size, limit_packets);
	if (ret) {
		err(errno, "Unable to start MFB generator.\n");
		goto tx_bar_search_fail;
	}

	signal(SIGINT, sig_usr);
	signal(SIGTERM, sig_usr);

	while (!stop) sleep(1);

	disable_channels(&ctx, &index_range);
	disable_mfb_gen(&ctx);

tx_bar_search_fail:
	if (ctx.txdev != ctx.rxdev)
		nfb_close(ctx.txdev);
txdev_not_found:
	nfb_close(ctx.rxdev);
rxdev_not_found:
	list_range_destroy(&index_range);
	return ret;
}
