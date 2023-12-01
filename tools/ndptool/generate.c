/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - generate module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <err.h>
#include <numa.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include "common.h"

static int ndp_mode_generate_prepare(struct ndp_tool_params *p);
static int ndp_mode_generate_loop(struct ndp_tool_params *p);
static int ndp_mode_generate_exit(struct ndp_tool_params *p);

int ndp_mode_generate(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = ndp_mode_generate_prepare(p);
	if (ret)
		return ret;
	ret = ndp_mode_generate_loop(p);
	ndp_mode_generate_exit(p);
	return ret;
}

void *ndp_mode_generate_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	thread_data->ret = ndp_mode_generate_prepare(p);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}
	numa_run_on_node(ndp_queue_get_numa_node(p->tx));

	thread_data->state = TS_RUNNING;
	thread_data->ret = ndp_mode_generate_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_generate_exit(p);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int ndp_mode_generate_prepare(struct ndp_tool_params *p)
{
	int ret = -1;

	p->si.progress_letter = 'G';
	/* Open device and queues */
	p->dev = nfb_open(p->nfb_path);
	if (p->dev == NULL){
		warnx("nfb_open() for queue %d failed.", p->queue_index);
		goto err_nfb_open;
	}

	p->tx = ndp_open_tx_queue_ext(p->dev, p->queue_index, p->use_userspace_flag ? NDP_OPEN_FLAG_USERSPACE : 0);
	if (p->tx == NULL) {
		warnx("ndp_open_tx_queue(%d) failed.", p->queue_index);
		goto err_ndp_open_tx;
	}

	/* Start queues */
	ret = ndp_queue_start(p->tx);
	if (ret != 0) {
		warnx("ndp_tx_queue_start(%d) failed.", p->queue_index);
		goto err_ndp_start_tx;
	}

	gettimeofday(&p->si.startTime, NULL);

	return 0;

	/* Error handling */
err_ndp_start_tx:
 	ndp_close_tx_queue(p->tx);
err_ndp_open_tx:
	nfb_close(p->dev);
err_nfb_open:
	return ret;
}

static int ndp_mode_generate_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	ndp_queue_stop(p->tx);
	ndp_close_tx_queue(p->tx);
	nfb_close(p->dev);
	return 0;
}

static int ndp_mode_generate_loop(struct ndp_tool_params *p)
{
	unsigned cnt;
	unsigned burst_size = TX_BURST;
	struct ndp_packet packets[burst_size];
	struct ndp_queue *tx = p->tx;
	struct stats_info *si = &p->si;

	update_stats_t update_stats = p->update_stats;

	unsigned i;

	int gen_index = 0;

	unsigned long long bytes_cnt = 0;
	unsigned long long packets_rem = p->limit_packets;

	const bool clear_data    = p->mode.generate.clear_data;
	const bool limit_bytes   = p->limit_bytes > 0 ? true : false;
	const bool limit_packets = p->limit_packets > 0 ? true : false;

	/* Clear length of packet header */
	for (i = 0; i < burst_size; i++) {
		packets[i].flags = 0;
		packets[i].header_length = 0;
	}

	/* OPT: Set the constant values in packet for one packet length */
	if (p->mode.generate.range.items == 1 && p->mode.generate.range.max[0] == 0) {
		gen_index = -1;
		for (i = 0; i < burst_size; i++) {
			packets[i].data_length = p->mode.generate.range.min[0];
		}
	}

	while (!stop) {
		if (limit_packets > 0) {
			/* Packet limit was reached in previous burst */
			if (packets_rem == 0) {
				break;
			}
			/* Packet limit will be reached in one burst */
			if (packets_rem < burst_size) {
				burst_size = packets_rem;
			}
		}

		if (limit_bytes > 0) {
			if (bytes_cnt >= p->limit_bytes) {
				break;
			}
		}

		/* Fill parameters for packets to send */
		if (gen_index != -1) {
			for (i = 0; i < burst_size; i++) {
				packets[i].data_length = p->mode.generate.range.min[gen_index];
				if (p->mode.generate.range.max[gen_index])
					packets[i].data_length += nc_fast_rand(&p->mode.generate.srand) % p->mode.generate.range.max[gen_index];
				if ((unsigned) ++gen_index == p->mode.generate.range.items)
					gen_index = 0;
			}
		}

		/* Request packet descriptors */
		cnt = ndp_tx_burst_get(tx, packets, burst_size);
		while (cnt != burst_size) {
			if (stop)
				return 0;
			delay_nsecs(1);
			cnt = ndp_tx_burst_get(tx, packets, burst_size);
		}

		if (clear_data) {
			for (i = 0; i < cnt; i++) {
				memset(packets[i].data, 0, packets[i].data_length);
				memset(packets[i].header, 0, packets[i].header_length);
			}
		}

		/* Update limits */
		packets_rem -= cnt;
		if (limit_bytes) {
			for (i = 0; i < cnt; i++) {
				bytes_cnt += packets[i].data_length;
			}
		}

		/* Update stats */
		update_stats(packets, cnt, si);

		/* Release packet descriptors */
		ndp_tx_burst_put(tx);
	}

	return 0;
}

int ndp_mode_generate_init(struct ndp_tool_params *p)
{
	list_range_init(&p->mode.generate.range);
	return 0;
}

void ndp_mode_generate_print_help()
{
	printf("Generate parameters:\n");
	printf("  -s size       Packet size - list or random from range, e.g \"64,128-256\"\n");
	printf("  -C            Clear packet data before send\n");
/*	printf("  -T content    Packet content [george, dns]\n"); */
}

int ndp_mode_generate_parseopt(struct ndp_tool_params *p, int opt, char *optarg)
{
	switch (opt) {
	case 's':
		if (list_range_parse(&p->mode.generate.range, optarg) < 0)
			errx(-1, "Cannot parse size range");
		break;
	case 'C':
		p->mode.generate.clear_data = 1;
		break;
	default:
		return -1;
	}
	return 0;
}

int ndp_mode_generate_check(struct ndp_tool_params *p)
{
	unsigned int i;
	if (list_range_empty(&p->mode.generate.range)) {
		errx(-1, "Unspecified size parameter");
	}

	for (i = 0; i < p->mode.generate.range.items; i++) {
		p->mode.generate.range.max[i] -= p->mode.generate.range.min[i];
		/* Increase by 1 if it's a real range (not one value) for random values including max */
		if (p->mode.generate.range.max[i])
			p->mode.generate.range.max[i]++;
	}

	return 0;
}

void ndp_mode_generate_destroy(struct ndp_tool_params *p)
{
	list_range_destroy(&p->mode.generate.range);
}
