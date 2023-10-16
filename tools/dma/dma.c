/* SPDX-License-Identifier: GPL-2.0 */
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
#include <unistd.h>
#include <string.h>

#include <libfdt.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <netcope/nccommon.h>
#include <netcope/rxqueue.h>
#include <netcope/txqueue.h>

/*! Program version */
#define VERSION				"1.0: dmactl.c"

/*! Acceptable command line arguments */
/*! -d path to device file */
/*! -i RX or TX index; -r use only RX; -t use only TX; */
/*! -R reset counters */
/*! -h print help; -v verbose mode; -V version */

#define ARGUMENTS			"d:i:q:rtRS:vh"

enum commands {
	CMD_PRINT_STATUS,
	CMD_USAGE,
	CMD_COUNTER_RESET,
	CMD_COUNTER_READ_AND_RESET,
	CMD_SET_RING_SIZE,
	CMD_QUERY,
};

const char *rx_ctrl_name[] = {
	COMP_NETCOPE_RXQUEUE_SZE,
	COMP_NETCOPE_RXQUEUE_NDP,
};
const char *tx_ctrl_name[] = {
	COMP_NETCOPE_TXQUEUE_SZE,
	COMP_NETCOPE_TXQUEUE_NDP,
};

// this enum need to corespond with queries[] array
enum queries {
	RX_RECEIVED,
	RX_RECEIVED_BYTES,
	RX_DISCARDED,
	RX_DISCARDED_BYTES,
	TX_SENT,
	TX_SENT_BYTES,
};
static const char * const queries[] = {
	"rx_received",
	"rx_received_bytes",
	"rx_discarded",
	"rx_discarded_bytes",
	"tx_sent",
	"tx_sent_bytes"
};

/*!
 * \brief Display usage of program
 */
// TODO add query help
void usage(const char *me, int verbose)
{
	printf("Usage: %s [-rtRvh] [-i index] [-d path]\n", me);
	printf("-d path         Path to device [default: %s]\n", NFB_DEFAULT_DEV_PATH);
	printf("-i indexes      Controllers numbers to use - list or range, e.g. \"0-5,7\" [default: all]\n");
	printf("-r              Use RX DMA controllers\n");
	printf("-t              Use TX DMA controllers\n");
	printf("-R              Resets packet counters (use -RR for read & reset)\n");
	printf("-S size         Set kernel ring buffer size (can be with K/M/G suffix)\n");
	printf("-q query        Get specific informations%s\n", verbose ? "" : " (-v for more info)");
	if (verbose) {
		for (unsigned i = 0; i < NC_ARRAY_SIZE(queries); i++) {
			printf(" * %s\n", queries[i]);
		}
		printf(" example of usage: '-q rx_received,tx_sent'\n");
	}

	printf("-v              Increase verbosity\n");
	printf("-h              Show this text\n");
}

int set_ring_size(struct nfb_device *dev, int dir, int index, const char* csize)
{
	FILE *f;
	char path_buffer[128];

	snprintf(path_buffer, sizeof(path_buffer),
		"/sys/class/nfb/nfb%d/ndp/%cx%d/ring_size",
		nfb_get_system_id(dev), dir == 0 ? 'r' : 't', index);

	f = fopen(path_buffer, "wb");
	if (f == NULL)
		err(errno, "Can't set ring size");
	fwrite(csize, 1, strlen(csize) + 1, f);
	fclose(f);
	return 0;
}

/*!
 * \brief Convert and display number of bits in kB or MB
 *
 * \param size       size of buffer
 */
void print_size(unsigned long size)
{
	static const char *units[] = {
		"B", "KiB", "MiB", "GiB"};
	unsigned int i = 0;
	while (size >= 1024 && i < NC_ARRAY_SIZE(units)) {
		size >>= 10;
		i++;
	}
	printf("%lu %s", size, units[i]);
}

/*!
 * \brief Query print function for RX and TX
 */
int query_print(struct nfb_device *dev, struct list_range index_range,
	char *queries, int size)
{
	struct nc_rxqueue *rxq;
	struct nc_txqueue *txq;
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
						if (ndp_rx_queue_is_available(dev, i)) {
							rxq = nc_rxqueue_open_index(dev, i, QUEUE_TYPE_UNDEF);
							if (rxq == NULL) {
								warnx("problem opening rx_queue");
								return EXIT_FAILURE;
							}
							nc_rxqueue_read_counters(rxq, &cr);
							nc_rxqueue_close(rxq);
						} else {
							warnx("rx_queue doesn't exist");
							return EXIT_FAILURE;
						} break;
					case TX_SENT:
					case TX_SENT_BYTES:
						if (txc_valid)
							break;

						txc_valid = 1;
						if (ndp_tx_queue_is_available(dev, i)) {
							txq = nc_txqueue_open_index(dev, i, QUEUE_TYPE_UNDEF);
							if (txq == NULL) {
								warnx("problem opening tx_queue");
								return EXIT_FAILURE;
							}
							nc_txqueue_read_counters(txq, &ct);
							nc_txqueue_close(txq);
						} else {
							warnx("tx_queue doesn't exist");
							return EXIT_FAILURE;
						} break;
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
					default: break;
				}
			}
		}
	}
	return 0;
}

/*!
 * \brief Print values from TX_DMA_INFO structure
 *
 * \param *tx_dma    structure from which are data read
 */
void rxqueue_print_status(struct nc_rxqueue *q, const char *compatible, int index, int verbose, enum commands cmd)
{
	int packet = strstr(compatible, "_pac_") ? 1 : 0;

	struct nc_rxqueue_status s;
	struct nc_rxqueue_counters c;


	printf("------------------------------ RX%02d %s controller ----\n", index, packet ? "PAC" : "NDP");
	if (verbose > 0) {
		nc_rxqueue_read_status(q, &s);

		printf("Control reg                : 0x%.8X", s._ctrl_raw);
		printf(" | %s | %s",
				s.ctrl_running ? "Run     " : "Stop    ",
				s.ctrl_discard ? "Discard " : "Block   ");

		printf(" | EpMsk %.2X | VFID  %.2X |\n", (s._ctrl_raw >> 16) & 0xFF, (s._ctrl_raw >> 24) & 0xFF);

		printf("Status reg                 : 0x%.8X", s._stat_raw);
		printf(" | %s | %s | %s | %s |\n",
				s.stat_running ? "Running " : "Stopped ",
				s.stat_desc_rdy ? "Desc RDY" : "Desc  - ",
				s.stat_data_rdy ? "Data RDY" : "Data  - ",
				s.stat_ring_rdy ? "SW RDY  " : "SW Full ");

		if (s.have_dp) {
			printf("SW header pointer          : 0x%08lX\n", s.sw_pointer);
			printf("HW header pointer          : 0x%08lX\n", s.hw_pointer);
			printf("Header pointer mask        : 0x%08lX\n", s.pointer_mask);
			printf("* Header buffer size       : "); print_size(s.pointer_mask ? s.pointer_mask + 1 : 0); printf("\n");
			printf("* Fillable headers in HW   : 0x%08lX\n", (s.sw_pointer - s.hw_pointer - 1) & s.pointer_mask);

			printf("SW descriptor pointer      : 0x%08lX\n", s.sd_pointer);
			printf("HW descriptor pointer      : 0x%08lX\n", s.hd_pointer);
			printf("Descriptor pointer mask    : 0x%08lX\n", s.desc_pointer_mask);
			printf("* Descriptor buffer size   : "); print_size(s.desc_pointer_mask ? s.desc_pointer_mask + 1 : 0); printf("\n");
			printf("* Usable descriptors in HW : 0x%08lX\n", (s.sd_pointer - s.hd_pointer) & s.desc_pointer_mask);
		} else {
			printf("SW pointer                 : 0x%08lX\n", s.sw_pointer);
			printf("HW pointer                 : 0x%08lX\n", s.hw_pointer);
			printf("Pointer mask               : 0x%08lX\n", s.pointer_mask);
			printf("* Buffer size              : "); print_size(s.pointer_mask ? s.pointer_mask + 1 : 0); printf("\n");
		}

		//printf("Interrupt reg:          0x%.8X\n", (reg = nfb_comp_read32(comp, 0x14)));
		printf("Timeout reg                : 0x%08lX\n", s.timeout);

		printf("Max request                : 0x%04lX     | ", s.max_request);
		print_size(s.max_request); printf("\n");
	}

	if (cmd == CMD_COUNTER_READ_AND_RESET) {
		if (nc_rxqueue_read_and_reset_counters(q, &c) == -ENXIO) {
			warnx("controller doesn't support atomic read & reset, command will be done non-atomically");
			nc_rxqueue_read_counters(q, &c);
			nc_rxqueue_reset_counters(q);
		}
	} else {
		nc_rxqueue_read_counters(q, &c);
	}
	printf("Received                   : %llu\n", c.received);
	if (c.have_bytes)
		printf("Received bytes             : %llu\n", c.received_bytes);
	printf("Discarded                  : %llu\n", c.discarded);
	if (c.have_bytes)
		printf("Discarded bytes            : %llu\n", c.discarded_bytes);

	if (verbose > 1) {
		printf("Desc base                  : 0x%.16llX\n", s.desc_base);
		printf("Pointer base               : 0x%.16llX\n", s.pointer_base);
	}
}

void txqueue_print_status(struct nc_txqueue *q, const char *compatible, int index, int verbose, enum commands cmd)
{
	int packet = strstr(compatible, "_pac_") ? 1 : 0;

	struct nc_txqueue_status s;
	struct nc_txqueue_counters c;

	printf("------------------------------ TX%02d %s controller ----\n", index, packet ? "PAC" : "NDP");
	if (verbose > 0) {
		nc_txqueue_read_status(q, &s);
		printf("Control reg                : 0x%.8X", s._ctrl_raw);
		printf(" | %s",
				s.ctrl_running ? "Run     " : "Stop    ");

		printf(" | EpMsk %.2X | VFID  %.2X |\n", (s._ctrl_raw >> 16) & 0xFF, (s._ctrl_raw >> 24) & 0xFF);

		printf("Status reg                 : 0x%.8X", s._stat_raw);
		printf(" | %s |\n",
				s.stat_running ? "Running " : "Stopped ");

		if (s.have_dp) {
			printf("SW descriptor pointer      : 0x%08lX\n", s.sd_pointer);
			printf("HW descriptor pointer      : 0x%08lX\n", s.hd_pointer);
			printf("Descriptor pointer mask    : 0x%08lX\n", s.desc_pointer_mask);
			printf("* Descriptor buffer size   : "); print_size(s.desc_pointer_mask ? s.desc_pointer_mask + 1 : 0); printf("\n");
			printf("* Usable descriptors in HW : 0x%08lX\n", (s.sd_pointer - s.hd_pointer) & s.desc_pointer_mask);
		} else {
			printf("SW pointer                 : 0x%08lX\n", s.sw_pointer);
			printf("HW pointer                 : 0x%08lX\n", s.hw_pointer);
			printf("Pointer mask               : 0x%08lX\n", s.pointer_mask);
			printf("* Buffer size              : "); print_size(s.pointer_mask ? s.pointer_mask + 1 : 0); printf("\n");
		}

		//printf("Interrupt reg:          0x%.8X\n", (reg = nfb_comp_read32(comp, 0x14)));
		printf("Timeout reg                : 0x%08lX\n", s.timeout);

		printf("Max request                : 0x%04lX     | ", s.max_request);
		print_size(s.max_request); printf("\n");
	}

	if (cmd == CMD_COUNTER_READ_AND_RESET) {
		if (nc_txqueue_read_and_reset_counters(q, &c) == -ENXIO) {
			warnx("controller doesn't support atomic read & reset, command will be done non-atomically");
			nc_txqueue_read_counters(q, &c);
			nc_txqueue_reset_counters(q);
		}
	} else {
		nc_txqueue_read_counters(q, &c);
	}

	printf("Sent                       : %llu\n", c.sent);
	if (c.have_bytes)
		printf("Sent bytes                 : %llu\n", c.sent_bytes);

	if (verbose > 1) {
		printf("Desc base                  : 0x%.16llX\n", s.desc_base);
		printf("Pointer base               : 0x%.16llX\n", s.pointer_base);
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

	int ret;
	const char *query = NULL;
	const char *csize = NULL;
	char *queries_index;
	int size;

	char c;
	char *file = NFB_DEFAULT_DEV_PATH;

	struct nfb_device *dev;
	struct nc_rxqueue *rxq;
	struct nc_txqueue *txq;
	struct list_range index_range;

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
		case 'S':
			cmd = CMD_SET_RING_SIZE;
			csize = optarg;
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

	if (argc) {
		err(errno, "Stray arguments");
	}

	dev = nfb_open(file);
	if (dev == NULL) {
		err(errno, "Can't open NFB device");
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

	if (dir == -1 || dir == 0) {
		for (ctrl = 0; ctrl < NC_ARRAY_SIZE(rx_ctrl_name); ctrl++) {
			i = 0;
			fdt_for_each_compatible_node(nfb_get_fdt(dev), fdt_offset, rx_ctrl_name[ctrl]) {
				if (list_range_empty(&index_range) || list_range_contains(&index_range, i)) {
					if (ndp_rx_queue_is_available(dev, i)) {
						rxq = nc_rxqueue_open(dev, fdt_offset);
						if (rxq == NULL)
							continue;

						switch (cmd) {
						case CMD_COUNTER_RESET:
							nc_rxqueue_reset_counters(rxq);
							break;
						case CMD_COUNTER_READ_AND_RESET:
						case CMD_PRINT_STATUS:
							rxqueue_print_status(rxq, rx_ctrl_name[ctrl], i, verbose, cmd);
							printf("\n");
							break;
						case CMD_SET_RING_SIZE:
							set_ring_size(dev, 0, i, csize);
							break;

						default:
							break;
						}
						nc_rxqueue_close(rxq);
					}
				}
				i++;
			}
		}
	}
	if (dir == -1 || dir == 1) {
		for (ctrl = 0; ctrl < NC_ARRAY_SIZE(tx_ctrl_name); ctrl++) {
			i = 0;
			fdt_for_each_compatible_node(nfb_get_fdt(dev), fdt_offset, tx_ctrl_name[ctrl]) {
				if (list_range_empty(&index_range) || list_range_contains(&index_range, i)) {
					if (ndp_tx_queue_is_available(dev, i)) {
						txq = nc_txqueue_open(dev, fdt_offset);
						if (txq == NULL)
							continue;
						switch (cmd) {
						case CMD_COUNTER_RESET:
							nc_txqueue_reset_counters(txq);
							break;
						case CMD_COUNTER_READ_AND_RESET:
						case CMD_PRINT_STATUS:
							txqueue_print_status(txq, tx_ctrl_name[ctrl], i, verbose, cmd);
							printf("\n");
							break;
						case CMD_SET_RING_SIZE:
							set_ring_size(dev, 1, i, csize);
							break;
						default:
							break;
						}
						nc_txqueue_close(txq);
					}
				}
				i++;
			}
		}
	}

	nfb_close(dev);

	list_range_destroy(&index_range);

	return EXIT_SUCCESS;
}
