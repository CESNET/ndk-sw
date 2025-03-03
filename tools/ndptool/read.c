/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Data transmission tool - read module
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
#include "pcap.h"

static int ndp_mode_read_prepare(struct ndp_tool_params *p);
static int ndp_mode_read_loop(struct ndp_tool_params *p);
static int ndp_mode_read_exit(struct ndp_tool_params *p);

int ndp_mode_read(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = ndp_mode_read_prepare(p);
	if (ret)
		return ret;
	ret = ndp_mode_read_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_read_exit(p);
	return ret;
}

void *ndp_mode_read_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	thread_data->ret = ndp_mode_read_prepare(p);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}
	numa_run_on_node(ndp_queue_get_numa_node(p->rx));

	thread_data->state = TS_RUNNING;
	thread_data->ret = ndp_mode_read_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_read_exit(p);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int ndp_mode_read_prepare(struct ndp_tool_params *p)
{
	int ret;

	p->si.progress_letter = 'R';

	ret = ndp_mode_common_prepare(p, 1, 0);

	gettimeofday(&p->si.startTime, NULL);

	return ret;
}

static int ndp_mode_read_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	ndp_mode_common_close(p, 1, 0);
	return 0;
}

static int ndp_mode_read_loop(struct ndp_tool_params *p)
{
	unsigned cnt;
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
		ndp_rx_burst_put(rx);
	}
	return 0;
}
