/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - receive module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <numa.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include "common.h"
#include "pcap.h"

static int ndp_mode_receive_prepare(struct ndp_tool_params *p);
static int ndp_mode_receive_loop(struct ndp_tool_params *p);
static int ndp_mode_receive_exit(struct ndp_tool_params *p);

int ndp_mode_receive(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = ndp_mode_receive_prepare(p);
	if (ret)
		return ret;
	ret = ndp_mode_receive_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_receive_exit(p);
	return ret;
}

void *ndp_mode_receive_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	/* append queue number to PCAP filename for each thread */
	char *pcap_fn = malloc(strlen(p->pcap_filename) + 8);
	if (pcap_fn == NULL) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}
	sprintf(pcap_fn, "%s.%d", p->pcap_filename, p->queue_index);
	p->pcap_filename = pcap_fn;

	thread_data->ret = ndp_mode_receive_prepare(p);
	if (thread_data->ret) {
		free(pcap_fn);
		thread_data->state = TS_FINISHED;
		return NULL;
	}
	numa_run_on_node(ndp_queue_get_numa_node(p->rx));

	thread_data->state = TS_RUNNING;
	thread_data->ret = ndp_mode_receive_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_receive_exit(p);
	thread_data->state = TS_FINISHED;

	free(pcap_fn);
	return NULL;
}

static int ndp_mode_receive_prepare(struct ndp_tool_params *p)
{
	int ret = -1;

	p->si.progress_letter = 'R';

	/* Open device and queues */
	p->dev = nfb_open(p->nfb_path);
	if (p->dev == NULL){
		warnx("nfb_open() for queue %d failed.", p->queue_index);
		goto err_nfb_open;
	}

	p->rx = ndp_open_rx_queue_ext(p->dev, p->queue_index, p->use_userspace_flag ? NDP_OPEN_FLAG_USERSPACE : 0);
	if (p->rx == NULL) {
		warnx("ndp_open_rx_queue(%d) failed.", p->queue_index);
		goto err_ndp_open_rx;
	}

	/* Start queues */
	ret = ndp_queue_start(p->rx);
	if (ret != 0) {
		warnx("ndp_rx_queue_start(%d) failed.", p->queue_index);
		goto err_ndp_start_rx;
	}

	p->pcap_file = pcap_write_begin(p->pcap_filename);
	if (p->pcap_file == NULL) {
		warnx("initializing PCAP file '%s' failed", p->pcap_filename);
		ret = -1;
		goto err_init_pcap_file;
	}

	gettimeofday(&p->si.startTime, NULL);

	return 0;

	/* Error handling */
	fclose(p->pcap_file);
	p->pcap_file = NULL;
err_init_pcap_file:
	ndp_queue_stop(p->rx);
err_ndp_start_rx:
 	ndp_close_rx_queue(p->rx);
err_ndp_open_rx:
	nfb_close(p->dev);
err_nfb_open:
	return ret;
}

static int ndp_mode_receive_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	ndp_queue_stop(p->rx);
	ndp_close_rx_queue(p->rx);
	nfb_close(p->dev);
	fclose(p->pcap_file);
	return 0;
}

static int ndp_mode_receive_loop(struct ndp_tool_params *p)
{
	unsigned cnt;
	int ret = 0;
	unsigned burst_size = RX_BURST;
	struct ndp_packet packets[burst_size];
	struct ndp_queue *rx = p->rx;
	struct stats_info *si = &p->si;
	update_stats_t update_stats = p->update_stats;

	while (!stop) {
		/* check limits if there is one (0 means loop forever) */
		if (p->limit_packets > 0) {
			/* limit reached */
			if (si->packet_cnt == p->limit_packets) {
				break;
			}
			/* limit will be reached in one burst */
			if (si->packet_cnt + burst_size > p->limit_packets) {
				burst_size = p->limit_packets - si->packet_cnt;
			}
		}

		if (p->limit_bytes > 0 && si->bytes_cnt > p->limit_bytes) {
			break;
		}

		cnt = ndp_rx_burst_get(rx, packets, burst_size);
		update_stats(packets, cnt, si);

		if (cnt == 0) {
			if (p->use_delay_nsec)
				delay_nsecs(1);
			continue;
		}

		ret = pcap_write_packet_burst(packets, cnt, p->pcap_file, p->mode.receive.ts_mode, p->mode.receive.trim);
		if (ret) {
			ndp_rx_burst_put(rx);
			return ret;
		}

		ndp_rx_burst_put(rx);
	}

	return 0;
}

int ndp_mode_receive_init(struct ndp_tool_params *p)
{
	p->mode.receive.ts_mode = TS_MODE_NONE;
	p->mode.receive.trim = (unsigned)-1;
	return 0;
}

void ndp_mode_receive_print_help()
{
	printf("Receive parameters:\n");
	printf("  -f file       Write data to PCAP file <file> (<file>.<queue> for multiple queues)\n");
	printf("  -t timestamp  Timestamp source for PCAP packet header: (system, header:X)\n");
	printf("                (X is bit offset in NDP header of 64b timestamp value)\n");
	printf("  -r trim       Maximum number of bytes per packet to save\n");
}

int ndp_mode_receive_parseopt(struct ndp_tool_params *p, int opt, char *optarg)
{
	switch (opt) {
	case 'f':
		p->pcap_filename = optarg;
		break;
	case 't':
		if (strcmp("system", optarg) == 0) {
			p->mode.receive.ts_mode = TS_MODE_SYSTEM;
		} else if (sscanf(optarg, "header:%d", &p->mode.receive.ts_mode) == 1) {
			if (p->mode.receive.ts_mode < 0) {
				errx(-1, "Wrong value for parameter -t");
			}
		} else {
			errx(-1, "Wrong value for parameter -t");
		}
		break;
	case 'r':
		if (sscanf(optarg, "%u", &p->mode.receive.trim) != 1) {
				errx(-1, "Wrong value for parameter -r");
		}
		break;
	default:
		return -1;
	}
	return 0;
}

int ndp_mode_receive_check(struct ndp_tool_params *p)
{
	if (p->pcap_filename == NULL) {
		errx(EXIT_FAILURE, "Parameter -f is mandatory");
	}
	return 0;
}
