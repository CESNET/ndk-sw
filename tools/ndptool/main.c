/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdbool.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <pthread.h>
#include <err.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <numa.h>

#include <nfb/nfb.h>

#include "common.h"

#define ARGUMENTS "d:i:hD:I:Rqp:b:B:"

volatile int stop = 0;
volatile int stats = 0;

unsigned RX_BURST = 64;
unsigned TX_BURST = 64;

extern struct ndptool_module modules[];
struct ndptool_module *module = NULL;

/* function for handling system signals
 * @param signo             Signal number
 */
static void sig_usr(int signo)
{
	if (signo == SIGINT) {
		stop = 1;
	} else if (signo == SIGUSR1) {
		stats = 1;
	}
}

/*
 * Print program help.
 */
static void usage(char *me, int mode_preset)
{
	int i;
	printf("Usage: %s %s[-d path] [-i indexes] [-D dump] [-I interval] [-p packets] [-b bytes] [-B size] [-Rqh]\n", me, mode_preset ? "mode ": "");

	if (!module) {
		i = 0;
		printf("Supported modes:\n");
		while (modules[i].name) {
			printf("  %-14s%s\n", modules[i].name, modules[i].short_help);
			i++;
		}
	}

	printf("Common parameters:\n");
	printf("  -d path       Path to device [default: %s]\n", NFB_DEFAULT_DEV_PATH);
	printf("  -i indexes    Queues numbers to use - list or range, e.g. \"0-5,7\" [default: all]\n");
	printf("  -h            Show this text\n");
	printf("  -p packets    Stop receiving or transmitting after <packets> packets\n");
	printf("  -b bytes      Stop receiving or transmitting after <bytes> bytes\n");
	printf("  -B size       Read and write packets in bursts of <size> [default: RX=%u, TX=%u]\n",
            RX_BURST, TX_BURST);

	printf("Packet output parameters: (available for one queue only)\n");
	printf("  -D dump       Dump packet content to stdout (char, all, header, data)\n");
	printf("  -I interval   Sample each Nth packet\n");

	printf("Statistic output parameters: (exclusive with -D argument)\n");
	printf("  -R            Incremental mode (no counter reset on each output)\n");
	printf("  -I interval   Print stats each N secs, 0 = don't print continuous stats [default: 1]\n");
	printf("  -q            Quiet mode - don't print stats at end\n");

	/* Future work */
/*
	printf("  -C            CSV output mode instead NCurses table\n");
	printf("  -A            Show statistics for each channel separately\n");
	printf("  -P options    Statistical values to print [bytes,packets,speed,average,usage]\n");
	printf("Other parameters:\n");
*/
	if (module && module->print_help) {
		module->print_help();
	}
}

void ndp_loop_thread_create(struct thread_data **pdata, struct ndp_tool_params *params)
{
	struct thread_data *data;

	/* Prevent cacheline crossing */
	if (posix_memalign((void **)pdata, 128, sizeof(struct thread_data))) {
		fprintf(stderr,"Unable to malloc\n");
		exit(-1);
	}

	data = *pdata;

	memset(data, 0, sizeof(*data));
	pthread_spin_init(&data->lock, 0);

	/* Copy params */
	memcpy(&data->params, params, sizeof(*params));
	data->params.si.priv = data;
}

void ndp_loop_thread_destroy(struct thread_data *thread_data)
{
	free(thread_data);
}

int main(int argc, char *argv[])
{
	unsigned i;
	int opt;
	int qri, qrc;
	unsigned rx_queues, tx_queues;
	struct nfb_device *dev;

	struct ndp_tool_params params;

	struct thread_data **thread_data;
	struct list_range queue_range;

	int interval = 1;
	bool quiet = 0;
	unsigned thread_cnt = 0;
	enum ndp_modules mode;

	void *(*thread_func) (void *);
	const char *strmode;
	char *args;

	int ret = 0;

	memset(&params, 0, sizeof(params));

	params.nfb_path = NFB_DEFAULT_DEV_PATH;
	params.queue_index = -1;
	params.si.progress_type = PT_NONE;
	params.si.sampling = 1;
	params.si.progress_counter = 0;
	params.si.incremental = false;
	params.pcap_filename = NULL;
	params.limit_bytes = 0;
	params.limit_packets = 0;

	list_range_init(&queue_range);

	/* Check if tool was executed from symlink */
	if (strncmp(basename(argv[0]), "ndp-", 4) == 0 &&
			strcmp(basename(argv[0]), "ndp-tool") != 0) {
		strmode = basename(argv[0]) + 4;
	} else if (argc < 2) {
		errx(-1, "No mode selected");
	} else {
		strmode = argv[optind];
		optind++;
	}

	if (strcmp(strmode, "-h") == 0) {
		usage(argv[0], strmode == argv[1]);
		exit(0);
	} else {
		mode = 0;
		while (modules[mode].name) {
			if (strcmp(strmode, modules[mode].name) == 0) {
				module = &modules[mode];
				break;
			}
			mode++;
		}
		if (!module) {
			errx(-1, "Unknown mode");
		}
	}

	if (module->init)
		module->init(&params);

	/* Concatenate common arguments with module arguments */
	args = malloc(strlen(module->args) + strlen(ARGUMENTS) + 1);
	strcpy(args, ARGUMENTS);
	strcat(args, module->args);

	/* argument handling */
	while ((opt = getopt(argc, argv, args)) >= 0) {
		switch (opt) {
		case 'd':
			params.nfb_path = optarg;
			break;
		case 'D':
			if      (optarg[0] == 'c') //strcmp(optarg, "char") == 0)
				params.si.progress_type = PT_LETTER;
			else if (optarg[0] == 'a') //strcmp(optarg, "all") == 0)
				params.si.progress_type = PT_ALL;
			else if (optarg[0] == 'h') //strcmp(optarg, "header") == 0)
				params.si.progress_type = PT_HEADER;
			else if (optarg[0] == 'd') //strcmp(optarg, "data") == 0)
				params.si.progress_type = PT_DATA;
			else
				errx(-1, "Unsupported dump type");
			break;
		case 'i':
			if (list_range_parse(&queue_range, optarg) < 0)
				errx(-1, "Cannot parse queue range");
			break;
		case 'I':
			params.si.sampling = interval = atoi(optarg);
			break;
		case 'B':
			RX_BURST = TX_BURST = atoi(optarg);
			if (RX_BURST<1 || RX_BURST>1024)
				errx(-1, "Burst size must be bigger then 0 and smaller or equal then 1024");
			break;
		case 'R':
			params.si.incremental = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'h':
			usage(argv[0], strmode == argv[1]);
			exit(0);
		case 'b':
			params.limit_bytes = atoi(optarg);
			break;
		case 'p':
			params.limit_packets = atoi(optarg);
			break;
		default:
			if (module->parse_opt == NULL || module->parse_opt(&params, opt, optarg))
				errx(-1, "Unknown parameter");
		}
	}

	argc -= optind;
	argv += optind;
	free(args);

	if (argc != 0)
		errx(EXIT_FAILURE, "Stray arguments.");

	dev = nfb_open(params.nfb_path);
	if (dev == NULL) {
		err(EXIT_FAILURE, "nfb_open failed");
	}

	rx_queues = ndp_get_rx_queue_count(dev);
	tx_queues = ndp_get_tx_queue_count(dev);

	if (module->check)
		module->check(&params);

	/* register SIGINT signal */
	signal(SIGINT, sig_usr);
	signal(SIGUSR1, sig_usr);

	/* Use single queue = don't use threads (at this moment) */
	if (queue_range.items == 1 && queue_range.min[0] == queue_range.max[0]) {
		params.queue_index = queue_range.min[0];
		ret = module->run_single(&params);
	} else {
		/* When no queues specified, use all available queues for selected mode */
		if (list_range_empty(&queue_range)) {
			for (i = 0; i < rx_queues || i < tx_queues; i++) {
				switch (mode) {
				case NDP_MODULE_READ:
				case NDP_MODULE_RECEIVE:
					if (ndp_rx_queue_is_available(dev, i))
						list_range_add_number(&queue_range, i);
					break;
				case NDP_MODULE_GENERATE:
				case NDP_MODULE_GENERATE_STATIC:
				case NDP_MODULE_TRANSMIT:
					if (ndp_tx_queue_is_available(dev, i))
						list_range_add_number(&queue_range, i);
					break;
				case NDP_MODULE_LOOPBACK_HW:
				case NDP_MODULE_LOOPBACK:
					if (ndp_rx_queue_is_available(dev, i) && ndp_tx_queue_is_available(dev, i))
						list_range_add_number(&queue_range, i);
					break;
				default:
					break;
				}
			}
		}

		for (i = 0; i < queue_range.items; i++) {
			thread_cnt += queue_range.max[i] - queue_range.min[i] + 1;
		}
		if (thread_cnt == 0)
			errx(-1, "No available queues");

		pthread_t thread[thread_cnt];
		thread_data = (struct thread_data **)malloc(thread_cnt * sizeof(*thread_data));

		/* INFO: This is not precise, it would be better to do this in each thread */
		gettimeofday(&params.si.startTime, NULL);

		/* Create and initialize each thread */
		for (qri = 0, qrc = 0, i = 0; i < thread_cnt; i++) {
			if (queue_range.min[qri] + qrc > queue_range.max[qri]) {
				qri++;
				qrc = 0;
			}

			thread_func = module->run_thread;
			params.queue_index = queue_range.min[qri] + qrc++;
			ndp_loop_thread_create(&thread_data[i], &params);
			thread_data[i]->thread_id = i;
			pthread_create(&thread[i], NULL, thread_func, thread_data[i]);
		}

		/* Run loop for printing aggregated stats */
		update_stats_loop_thread(interval, thread_data, thread_cnt, &queue_range, &params.si);

		for (i = 0; i < thread_cnt; i++) {
			pthread_join(thread[i], NULL);
			if (thread_data[i]->ret != 0)
				ret = thread_data[i]->ret;
			gather_stats_info(&params.si, &thread_data[i]->params.si);
			ndp_loop_thread_destroy(thread_data[i]);
		}
		gettimeofday(&params.si.endTime, NULL);
		free(thread_data);
	}

	nfb_close(dev);

	if (!quiet) {
		print_stats(&params.si);
	}

	if (module->destroy)
		module->destroy(&params);

	list_range_destroy(&queue_range);

	return ret;
}
