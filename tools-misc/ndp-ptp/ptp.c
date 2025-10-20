/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Experimental tool connecting pairs of (RX,TX) channels in a Peer-to-Peer manner
 *
 * Copyright (C) 2025-2025 Universitaet Heidelberg, Institut fuer Technische Informatik (ZITI)
 * Author(s):
 *     Vladislav Valek <vladislav.valek@stud.uni-heidelberg.de>
 */

// TODO: Currently, this tool supports only one-endpoint devices. A support
// for multi-endpoint devices (like bifurcated ones) is a subject of future development.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <signal.h>
#include <libfdt.h>
#include <pci/pci.h>

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

#define ARGUMENTS	"i:r:t:p:hg:ua:"

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
	printf("-g size         Enables local MFB generator to generate packets of specified size\n");
	printf("-a size         Enables remote MFB generator to generate packets of specified size\n");
	printf("-i indexes      List of communicating channels\n");
	printf("-r path         Path to a transmitting device [default: %s]\n", nfb_default_dev_path());
	printf("-t path         Path to a receiving device [default: %s]\n", nfb_default_dev_path());
	printf("-p packets      Stop tranfering after <packets> packets\n");
	printf("-u              Enable loopback on the remote end (no effect if -r and -t are the same device)");
	printf("-h              Show this text\n");
}

int find_dev_bar_addrs(struct nfb_device *dev, uint64_t *bars)
{
	int len;
	unsigned int domain, bus, dev_idx, func;
	const char *bdf_str;
	const void *fdt = nfb_get_fdt(dev);
	int fdt_offset = fdt_path_offset(fdt, "/system/device/endpoint0");
	struct pci_access *pacc = pci_alloc();

	pci_init(pacc);
	pci_scan_bus(pacc);

	bdf_str = fdt_getprop(fdt, fdt_offset, "pci-slot", &len);
	if (len < 0) {
		fprintf(stderr, "Fail to locate pci-slot property on Endpoint 0\n");
		return -3;
	}

	if (sscanf(bdf_str, "%x:%x:%x.%x", &domain, &bus, &dev_idx, &func) != 4) {
		fprintf(stderr, "Invalid BDF format: %s\n", bdf_str);
		return -4;
	}

	// Get PCIe device handle
	struct pci_dev *devptr = pci_get_dev(pacc, 0, bus, dev_idx, func);
	if (!devptr) {
		fprintf(stderr, "Device %s not found\n", bdf_str);
		return -5;
	}

	pci_fill_info(devptr, PCI_FILL_IDENT | PCI_FILL_BASES);

	for (int i = 0; i < 3; i++) {
		uint32_t bar = pci_read_long(devptr, PCI_BASE_ADDRESS_0 + i*4);

		if ((bar & PCI_BASE_ADDRESS_MEM_TYPE_64) == PCI_BASE_ADDRESS_MEM_TYPE_64) {
			uint32_t bar_high = pci_read_long(devptr, PCI_BASE_ADDRESS_0 + (i+1)*4);
			uint64_t bar64 = ((uint64_t)bar_high << 32) | (bar & PCI_BASE_ADDRESS_MEM_MASK);
			printf("BAR%d (64-bit): 0x%016llx\n", i, (unsigned long long)bar64);

			bars[i/2] = bar64;
			i++; // Skip next BAR
		} else {
			printf("BAR%d (Memory 32-bit): 0x%08lx\n", i, bar & PCI_BASE_ADDRESS_MEM_MASK);
			bars[i/2] = bar;
		}
	}

	if (bars[0] == 0 || bars[1] == 0)
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

int stop_chan_pair (struct nfb_device *rxdev, struct nfb_device *txdev, int idx)
{
	int ret = 0;
	int node;
	struct nfb_comp *rx_chan;
	struct nfb_comp *tx_chan;

	node = nfb_comp_find(rxdev, RX_CTRL_CP, idx);
	rx_chan = nfb_comp_open(rxdev, node);
	if (rx_chan == NULL) {
		err(errno, "Fail to open RX Control component %d\n", idx);
		return -1;
	}
	nfb_comp_write8(rx_chan, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_STOP);

	usleep(500);

	node = nfb_comp_find(txdev, TX_CTRL_CP, idx);
	tx_chan = nfb_comp_open(txdev, node);
	if (tx_chan == NULL) {
		err(errno, "Fail to open TX Control component %d\n", idx);
		nfb_comp_close(rx_chan);
		return -1;
	}
	nfb_comp_write8(tx_chan, NDP_CTRL_REG_CONTROL, NDP_CTRL_REG_CONTROL_STOP);

	ret = chan_wait_stop(tx_chan);
	if (ret) {
		warn("Unable to stop TX channel %d\n", idx);
	}

	ret = chan_wait_stop(rx_chan);
	if (ret) {
		warn("Unable to stop RX channel %d\n", idx);
	}
	nfb_comp_close(tx_chan);
	nfb_comp_close(rx_chan);
	return 0;
}

int mfb_lbk_ctrl (struct nfb_device *dev, uint8_t en) {
	int node_offs = nfb_comp_find(dev, "cesnet,mfb_loopback", 0);
	struct nfb_comp *node = nfb_comp_open(dev, node_offs);

	if (node == NULL) {
		fprintf(stderr, "Failed to open MFB loopback.\n");
		return -1;
	}

	nfb_comp_write8(node, 0, en & 0x01);
	nfb_comp_close(node);
	return 0;
}

int disable_channels(struct ptp_probe_ctx *ctx, struct list_range* lr, bool rcv_lbk_en)
{
	int rx_size = ndp_get_rx_queue_count(ctx->rxdev);
	int tx_size = ndp_get_tx_queue_count(ctx->txdev);
	int max = tx_size < rx_size ? rx_size : tx_size;

	int ret = 0;

	for (int i = 0; i < max; ++i) {
		if (list_range_empty(lr) || list_range_contains(lr, i)) {
			ret = stop_chan_pair(ctx->rxdev, ctx->txdev, i);
			if (ret) {
				fprintf(stderr, "Failed to stop egress channels %d\n", i);
			}

			if (rcv_lbk_en) {
				ret = stop_chan_pair(ctx->txdev, ctx->rxdev, i);
				if (ret) {
					fprintf(stderr, "Failed to stop ingress channels %d\n", i);
				}
			}
		}
	}

	return ret;
}

int receiver_enable (struct nfb_device *dev, int idx, uint64_t rx_bar_addr)
{
	int node;
	struct nfb_comp *comp;
	uint64_t ptr_offs;

	node = nfb_comp_find(dev, TX_CTRL_CP, idx);
	comp = nfb_comp_open(dev, node);
	if (comp == NULL) {
		err(errno, "Fail to open TX Control component %d\n", idx);
		return -1;
	}
	ptr_offs = rx_bar_addr + DMA_COMP_OFFS + RXDMA_OFFS + idx*REGS_SIZE + NDP_CTRL_REG_SDP;
	printf("Writing RX UPD Buff addr: %lx to TX channel %d\n", ptr_offs, idx);
	nfb_comp_write64(comp, NDP_CTRL_REG_UPDATE_BASE, ptr_offs);
	nfb_comp_write8(comp, NDP_CTRL_REG_EXPER, 1);
	nfb_comp_write32(comp, NDP_CTRL_REG_TIMEOUT, 0x4000);
	nfb_comp_write64(comp, NDP_CTRL_REG_SDP, 0);

	if (chan_start(comp)) {
		warn("Unable to start TX channel %d\n", idx);
		nfb_comp_close(comp);
		return -1;
	}

	nfb_comp_close(comp);
	return 0;
}

int transmitter_enable (struct nfb_device *dev, int idx, uint64_t tx_ctrl_bar_addr, uint64_t tx_buff_bar_addr, int tx_queue_count)
{
	int node;
	struct nfb_comp *comp;
	uint64_t ptr_offs;
	uint64_t data_buff_offs;
	uint64_t hdr_buff_offs;

	node = nfb_comp_find(dev, RX_CTRL_CP, idx);
	comp = nfb_comp_open(dev, node);
	if (comp == NULL) {
		err(errno, "Fail to open RX Control component %d\n", idx);
		return -1;
	}
	ptr_offs = tx_ctrl_bar_addr + DMA_COMP_OFFS + TXDMA_OFFS + idx*REGS_SIZE + NDP_CTRL_REG_SDP;
	printf("Writing TX UPD Buff addr: %lx to RX channel %d\n", ptr_offs, idx);
	nfb_comp_write64(comp, NDP_CTRL_REG_UPDATE_BASE, ptr_offs);

	data_buff_offs = tx_buff_bar_addr + idx*TX_BUFF_SIZE*2;
	printf("Writing TX DATA Buff addr: %lx to RX channel %d\n", data_buff_offs, idx);
	nfb_comp_write64(comp, NDP_CTRL_REG_DESC_BASE, data_buff_offs);

	hdr_buff_offs = tx_buff_bar_addr + TX_HDR_BUFF_OFFS(tx_queue_count) + idx*TX_BUFF_SIZE*2;
	printf("Writing TX HDR Buff addr: %lx to RX channel %d\n", hdr_buff_offs, idx);
	nfb_comp_write64(comp, NDP_CTRL_REG_HDR_BASE, hdr_buff_offs);

	// NOTE: It should be probably copied from the TX Channel's configuration registers
	nfb_comp_write16(comp, NDP_CTRL_REG_MDP, 0x003F);
	nfb_comp_write16(comp, NDP_CTRL_REG_MHP, 0x03FF);
	nfb_comp_write8(comp, NDP_CTRL_REG_EXPER, 1);
	nfb_comp_write32(comp, NDP_CTRL_REG_TIMEOUT, 0x4000);
	nfb_comp_write64(comp, NDP_CTRL_REG_SDP, 0);

	if (chan_start(comp)) {
		warn("Unable to start RX channel %d\n", idx);
		nfb_comp_close(comp);
		return -1;
	}

	nfb_comp_close(comp);
	return 0;
}

int configure_channels(struct ptp_probe_ctx *ctx, struct list_range* lr, bool full_dpx_en)
{
	int rx_size = ndp_get_rx_queue_count(ctx->rxdev);
	int tx_size = ndp_get_tx_queue_count(ctx->txdev);
	int max = tx_size < rx_size ? rx_size : tx_size;

	int ret = 0;
	ctx->chan_min = 0;
	ctx->chan_max = 0;

	for (int i = 0; i < max; ++i) {
		if (list_range_empty(lr) || list_range_contains(lr, i)) {

			if (i == ctx->chan_max + 1)
				ctx->chan_max = i;

			ret = receiver_enable(ctx->txdev, i, ctx->rx_bars[0]);
			if (ret) {
				err(errno, "Failed to start remote receiver %d\n", i);
				return -1;
			}

			if (full_dpx_en) {
				ret = receiver_enable(ctx->rxdev, i, ctx->tx_bars[0]);
				if (ret) {
					err(errno, "Failed to start local receiver %d\n", i);
					return -1;
				}
			}

			ret = transmitter_enable(ctx->rxdev, i, ctx->tx_bars[0], ctx->tx_bars[1], tx_size);
			if (ret) {
				err(errno, "Failed to start local transmitter %d\n", i);
				return -1;
			}

			if (full_dpx_en) {
				ret = transmitter_enable(ctx->txdev, i, ctx->rx_bars[0], ctx->rx_bars[1], tx_size);
				if (ret) {
					err(errno, "Failed to start remote transmitter  %d\n", i);
					return -1;
				}
			}
		}
	}

	return 0;
}

#define GEN_REG_CTRL          0x0
#define GEN_REG_LEN           0x04
#define GEN_REG_CHAN_INCR     0x08
#define GEN_REG_CHAN_MIN_MAX  0x0C
#define GEN_REG_CNTR_L        0x20
#define GEN_REG_CNTR_H        0x24

int disable_mfb_gen(struct nfb_device *dev)
{
	int node_offs;
	const void *fdt = nfb_get_fdt(dev);
	struct nfb_comp *gen_comp;
	struct nfb_comp *gen_mux_comp;

	node_offs = fdt_path_offset(fdt, "/firmware/mi_pci0_bar0/dbg_gls0/mfb_gen2dma");
	gen_comp = nfb_comp_open(dev, node_offs);
	if (gen_comp == NULL) {
		fprintf(stderr, "Fail to open MFB Generator component\n");
		return -1;
	}
	node_offs = nfb_comp_find(dev, "cesnet,ofm,gen_loop_switch", 0);
	gen_mux_comp = nfb_comp_open(dev, node_offs);
	if (gen_mux_comp == NULL) {
		fprintf(stderr, "Fail to open GLS component\n");
		nfb_comp_close(gen_comp);
		return -1;
	}

	nfb_comp_write8(gen_comp, GEN_REG_CTRL, 0);
	nfb_comp_close(gen_comp);
	usleep(1);
	nfb_comp_write8(gen_mux_comp, 0x8, 0);
	nfb_comp_close(gen_mux_comp);
	return 0;
}

int run_mfb_gen(struct nfb_device *dev, unsigned long long pkt_len, unsigned long long limit_packets, uint16_t chan_min, uint16_t chan_max)
{
	int node_offs;
	const void *fdt = nfb_get_fdt(dev);
	uint32_t chan_min_max_reg;
	uint32_t chan_incr_reg;
	struct nfb_comp *gen_comp;
	struct nfb_comp *gen_mux_comp;

	node_offs = fdt_path_offset(fdt, "/firmware/mi_pci0_bar0/dbg_gls0/mfb_gen2dma");
	gen_comp = nfb_comp_open(dev, node_offs);
	if (gen_comp == NULL) {
		fprintf(stderr, "Fail to open MFB Generator component\n");
		return -1;
	}
	node_offs = nfb_comp_find(dev, "cesnet,ofm,gen_loop_switch", 0);
	gen_mux_comp = nfb_comp_open(dev, node_offs);
	if (gen_mux_comp == NULL) {
		fprintf(stderr, "Fail to open GLS component\n");
		nfb_comp_close(gen_comp);
		return -1;
	}
	// Switch multiplexer so the generator is connected to the RX stream
	nfb_comp_write8(gen_mux_comp, 0x8, 1);
	usleep(1);

	nfb_comp_write32(gen_comp, GEN_REG_LEN, pkt_len);

	chan_min_max_reg = ((uint32_t)chan_max << 16) | chan_min;
	nfb_comp_write32(gen_comp, GEN_REG_CHAN_MIN_MAX, chan_min_max_reg);

	// Burst of 64 packets and increment channel by 1
	chan_incr_reg = ((uint32_t) limit_packets << 16) | 0x00000001;
	nfb_comp_write32(gen_comp, GEN_REG_CHAN_INCR, chan_incr_reg);
	nfb_comp_write8(gen_comp, GEN_REG_CTRL, 1);

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	char c;
	const char *rx_file = nfb_default_dev_path();
	const char *tx_file = nfb_default_dev_path();
	struct list_range index_range;
	unsigned long long limit_packets = 8;
	unsigned long long local_packet_size = 64;
	unsigned long long remote_packet_size = 64;
	struct ptp_probe_ctx ctx = {0};
	bool local_gen_en = false;
	bool remote_gen_en = false;
	bool rcv_lbk_en = false;

	list_range_init(&index_range);

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'u':
			rcv_lbk_en = true;
			break;
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
		case 'g':
			local_gen_en = true;
			if (nc_strtoull(optarg, &local_packet_size))
				errx(-1, "Cannot parse -g size parameter");
			break;
		case 'a':
			remote_gen_en = true;
			if (nc_strtoull(optarg, &remote_packet_size))
				errx(-1, "Cannot parse -a size parameter");
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
			fprintf(stderr, "Failed to open receiving device.");
			goto txdev_not_found;
		}
	} else {
		ctx.txdev = ctx.rxdev;
		rcv_lbk_en = false;
		remote_gen_en = false;
	}

	ret |= find_dev_bar_addrs(ctx.txdev, ctx.tx_bars);
	ret |= find_dev_bar_addrs(ctx.rxdev, ctx.rx_bars);
	if (ret) {
		fprintf(stderr, "Unable to retrieve BAR addresses. -> ret: %d\n", ret);
		goto tx_bar_search_fail;
	}

	printf("BARs found: TX_BAR0: %lx, TX_BAR2: %lx\n", ctx.tx_bars[0], ctx.tx_bars[1]);
	printf("BARs found: RX_BAR0: %lx, RX_BAR2: %lx\n", ctx.rx_bars[0], ctx.rx_bars[1]);

	// Remove enabling of loopback to separate function and call it here
	ret = configure_channels(&ctx, &index_range, rcv_lbk_en || remote_gen_en);
	if (ret) {
		fprintf(stderr, "Unable to configure channels.\n");
		goto tx_bar_search_fail;
	}

	if (rcv_lbk_en) {
		ret = mfb_lbk_ctrl(ctx.txdev, 1);
		if (ret) {
			fprintf(stderr, "Failed to enable loopback on the remote end.");
			goto mfb_lbk_en_fail;
		}
	}

	if (local_gen_en) {
		ret |= run_mfb_gen(ctx.rxdev, local_packet_size, limit_packets, ctx.chan_min, ctx.chan_max);
		if (ret) {
			fprintf(stderr, "Unable to start local MFB generator.\n");
			goto local_gen_start_fail;
		}
	}

	if (remote_gen_en && !rcv_lbk_en) {
		ret |= run_mfb_gen(ctx.txdev, remote_packet_size, limit_packets, ctx.chan_min, ctx.chan_max);
		if (ret) {
			fprintf(stderr, "Unable to start remote MFB generator.\n");
			goto remote_gen_start_fail;
		}
	}

	signal(SIGINT, sig_usr);
	signal(SIGTERM, sig_usr);

	while (!stop) sleep(1);

	if (remote_gen_en && !rcv_lbk_en) {
		ret = disable_mfb_gen(ctx.txdev);
		if (ret)
			fprintf(stderr, "Failed to stop remote MFB generator.");
	}

remote_gen_start_fail:
	if (local_gen_en) {
		ret = disable_mfb_gen(ctx.rxdev);
		if (disable_mfb_gen(ctx.rxdev))
			fprintf(stderr, "Failed to stop local MFB generator.");
	}

local_gen_start_fail:
	if (rcv_lbk_en) {
		ret = mfb_lbk_ctrl(ctx.txdev, 0);
		if (ret) {
			fprintf(stderr, "Failed to disable loopback on the remote end.");
		}
	}

mfb_lbk_en_fail:
	ret = disable_channels(&ctx, &index_range, rcv_lbk_en || remote_gen_en);
	if (ret)
		fprintf(stderr, "Failed to stop channels.");

tx_bar_search_fail:
	if (ctx.txdev != ctx.rxdev)
		nfb_close(ctx.txdev);
txdev_not_found:
	nfb_close(ctx.rxdev);
rxdev_not_found:
	list_range_destroy(&index_range);
	return ret;
}
