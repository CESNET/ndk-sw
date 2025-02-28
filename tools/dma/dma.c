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
/*! -B buffer size */
/*! -C buffer count */
/*! -i RX or TX index */
/*! -r use only RX; */
/*! -t use only TX; */
/*! -R reset counters */
/*! -S ring size */
/*! -O initial offset*/
/*! -h print help; */
/*! -v verbose mode; */
/*! -V version */

#define ARGUMENTS			"d:i:q:rtRS:B:C:O:Tvh"

enum commands {
	CMD_PRINT_STATUS,
	CMD_USAGE,
	CMD_COUNTER_RESET,
	CMD_COUNTER_READ_AND_RESET,
	CMD_SET_RING_SIZE,
	CMD_SET_BUFFER_SIZE,
	CMD_SET_BUFFER_COUNT,
	CMD_SET_INITIAL_OFFSET,
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

	printf("-v              Increase verbosity\n");
	printf("-h              Show this text\n");
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
void rxqueue_print_status(struct nc_rxqueue *q, int index, int verbose, struct nc_rxqueue_status *s, struct nc_rxqueue_counters *c)
{
	printf("------------------------------ RX%02d %s controller ----\n", index, q->name);

	if (s && verbose > 0) {
		printf("Control reg                : 0x%.8X", s->_ctrl_raw);
		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			printf(" | %s |",
					s->ctrl_running ? "Run     " : "Stop    ");
		} else if (q->type == QUEUE_TYPE_SZE) {
			printf(" | %s | %s | EpMsk %.2X | VFID  %.2X |",
					s->ctrl_running ? "Run     " : "Stop    ",
					s->ctrl_discard ? "Discard " : "Block   ",
					(s->_ctrl_raw >> 16) & 0xFF,
					(s->_ctrl_raw >> 24) & 0xFF);
		}
		printf("\n");

		printf("Status reg                 : 0x%.8X", s->_stat_raw);
		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			printf(" | %s |",
					s->stat_running ? "Running " : "Stopped ");
		} else if (q->type == QUEUE_TYPE_SZE) {
			printf(" | %s | %s | %s | %s |",
					s->stat_running ? "Running " : "Stopped ",
					s->stat_desc_rdy ? "Desc RDY" : "Desc  - ",
					s->stat_data_rdy ? "Data RDY" : "Data  - ",
					s->stat_ring_rdy ? "SW RDY  " : "SW Full ");
		}
		printf("\n");

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			printf("SW header pointer          : 0x%08lX\n", s->sw_pointer);
			printf("HW header pointer          : 0x%08lX\n", s->hw_pointer);
			printf("Header pointer mask        : 0x%08lX\n", s->pointer_mask);
			printf("* Header buffer size       : "); print_size(s->pointer_mask ? s->pointer_mask + 1 : 0); printf("\n");
			printf("* Fillable headers in HW   : 0x%08lX\n", (s->sw_pointer - s->hw_pointer - 1) & s->pointer_mask);

			printf("SW descriptor pointer      : 0x%08lX\n", s->sd_pointer);
			printf("HW descriptor pointer      : 0x%08lX\n", s->hd_pointer);
			printf("Descriptor pointer mask    : 0x%08lX\n", s->desc_pointer_mask);
			printf("* Descriptor buffer size   : "); print_size(s->desc_pointer_mask ? s->desc_pointer_mask + 1 : 0); printf("\n");
			printf("* Usable descriptors in HW : 0x%08lX\n", (s->sd_pointer - s->hd_pointer) & s->desc_pointer_mask);
		} else {
			printf("SW pointer                 : 0x%08lX\n", s->sw_pointer);
			printf("HW pointer                 : 0x%08lX\n", s->hw_pointer);
			printf("Pointer mask               : 0x%08lX\n", s->pointer_mask);
			printf("* Buffer size              : "); print_size(s->pointer_mask ? s->pointer_mask + 1 : 0); printf("\n");
		}

		if (q->type == QUEUE_TYPE_SZE || q->type == QUEUE_TYPE_NDP)
			printf("Timeout reg                : 0x%08lX\n", s->timeout);

		if (q->type == QUEUE_TYPE_SZE) {
			printf("Max request                : 0x%04lX     | ", s->max_request);
			print_size(s->max_request); printf("\n");
		}
	}

	if (c) {
		printf("Received                   : %llu\n", c->received);
		if (c->have_bytes)
			printf("Received bytes             : %llu\n", c->received_bytes);
		printf("Discarded                  : %llu\n", c->discarded);
		if (c->have_bytes)
			printf("Discarded bytes            : %llu\n", c->discarded_bytes);
	}
	if (s && verbose > 1) {
		printf("Desc base                  : 0x%.16llX\n", s->desc_base);
		if (q->type == QUEUE_TYPE_CALYPTE)
			printf("Header base                : 0x%.16llX\n", s->hdr_base);
		else
			printf("Pointer base               : 0x%.16llX\n", s->pointer_base);
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

void txqueue_print_status(struct nc_txqueue *q, int index, int verbose, struct nc_txqueue_status *s, struct nc_txqueue_counters *c)
{
	printf("------------------------------ TX%02d %s controller ----\n", index, q->name);

	if (s && verbose > 0) {
		printf("Control reg                : 0x%.8X", s->_ctrl_raw);

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			printf(" | %s |",
					s->ctrl_running ? "Run     " : "Stop    ");
		} else if (q->type == QUEUE_TYPE_SZE) {
			printf(" | %s | EpMsk %.2X | VFID  %.2X |",
					s->ctrl_running ? "Run     " : "Stop    ",
					(s->_ctrl_raw >> 16) & 0xFF,
					(s->_ctrl_raw >> 24) & 0xFF);
		}
		printf("\n");

		printf("Status reg                 : 0x%.8X", s->_stat_raw);
		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			printf(" | %s |",
					s->stat_running ? "Running " : "Stopped ");
		} else if (q->type == QUEUE_TYPE_SZE) {
			printf(" | %s |",
					s->stat_running ? "Running " : "Stopped ");
		}
		printf("\n");

		if (q->type == QUEUE_TYPE_NDP || q->type == QUEUE_TYPE_CALYPTE) {
			if (q->type == QUEUE_TYPE_CALYPTE) {
				printf("SW header pointer          : 0x%08lX\n", s->sw_pointer);
				printf("HW header pointer          : 0x%08lX\n", s->hw_pointer);
				printf("Header pointer mask        : 0x%08lX\n", s->pointer_mask);
				printf("* Header buffer size       : "); print_size(s->pointer_mask ? s->pointer_mask + 1 : 0); printf("\n");
				printf("* Fillable headers in HW   : 0x%08lX\n", (s->sw_pointer - s->hw_pointer - 1) & s->pointer_mask);
			}

			printf("SW descriptor pointer      : 0x%08lX\n", s->sd_pointer);
			printf("HW descriptor pointer      : 0x%08lX\n", s->hd_pointer);
			printf("Descriptor pointer mask    : 0x%08lX\n", s->desc_pointer_mask);
			printf("* Descriptor buffer size   : "); print_size(s->desc_pointer_mask ? s->desc_pointer_mask + 1 : 0); printf("\n");
			printf("* Usable descriptors in HW : 0x%08lX\n", (s->sd_pointer - s->hd_pointer) & s->desc_pointer_mask);
		} else {
			printf("SW pointer                 : 0x%08lX\n", s->sw_pointer);
			printf("HW pointer                 : 0x%08lX\n", s->hw_pointer);
			printf("Pointer mask               : 0x%08lX\n", s->pointer_mask);
			printf("* Buffer size              : "); print_size(s->pointer_mask ? s->pointer_mask + 1 : 0); printf("\n");
		}

		if (q->type == QUEUE_TYPE_SZE || q->type == QUEUE_TYPE_NDP)
			printf("Timeout reg                : 0x%08lX\n", s->timeout);

		if (q->type == QUEUE_TYPE_SZE) {
			printf("Max request                : 0x%04lX     | ", s->max_request);
			print_size(s->max_request); printf("\n");
		}
	}

	if (c) {
		printf("Sent                       : %llu\n", c->sent);
		if (c->have_bytes)
			printf("Sent bytes                 : %llu\n", c->sent_bytes);

		if (c->have_tx_discard) {
			printf("Discarded                  : %llu\n", c->discarded);
			if (c->have_bytes)
				printf("Discarded bytes            : %llu\n", c->discarded_bytes);
		}
	}

	if (s && verbose > 1 && q->type != QUEUE_TYPE_CALYPTE) {
		printf("Desc base                  : 0x%.16llX\n", s->desc_base);
		printf("Pointer base               : 0x%.16llX\n", s->pointer_base);
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

	int ret;
	const char *query = NULL;
	const char *csize = NULL;
	char *queries_index;
	int size;

	char c;
	const char *file = nfb_default_dev_path();

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
								rxqueue_print_status(rxq, i, verbose, verbose ? &stat_rx : NULL, &cntr_rx);
								printf("\n");
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
								txqueue_print_status(txq, i, verbose, verbose ? &stat_tx : NULL, &cntr_tx);
								printf("\n");
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
				}
				i++;
			}
		}
	}

	if (sum && (dir == -1 || dir == 0)) {
		printf("------------------------------ RX SUM -----------------\n");
		printf("Received                   : %llu\n", sum_rx.received);
		if (cntr_rx.have_bytes)
			printf("Received bytes             : %llu\n", sum_rx.received_bytes);
		printf("Discarded                  : %llu\n", sum_rx.discarded);
		if (cntr_rx.have_bytes)
			printf("Discarded bytes            : %llu\n", sum_rx.discarded_bytes);
		printf("\n");
	}

	if (sum && (dir == -1 || dir == 1)) {
		printf("------------------------------ TX SUM -----------------\n");
		printf("Sent                       : %llu\n", sum_tx.sent);
		if (cntr_tx.have_bytes)
			printf("Sent bytes                 : %llu\n", sum_tx.sent_bytes);

		if (cntr_tx.have_tx_discard) {
			printf("Discarded                  : %llu\n", sum_tx.discarded);
			if (sum_tx.have_bytes)
				printf("Discarded bytes            : %llu\n", sum_tx.discarded_bytes);
		}
		printf("\n");
	}

	nfb_close(dev);

	list_range_destroy(&index_range);

	return EXIT_SUCCESS;
}
