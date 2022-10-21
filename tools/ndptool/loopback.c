/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - loopback module
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

static int ndp_mode_loopback_prepare(struct ndp_tool_params *p);
static int ndp_mode_loopback_loop(struct ndp_tool_params *p);
static int ndp_mode_loopback_exit(struct ndp_tool_params *p);

int ndp_mode_loopback(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = ndp_mode_loopback_prepare(p);
	if (ret)
		return ret;
	ret = ndp_mode_loopback_loop(p);
	ndp_mode_loopback_exit(p);
	return ret;
}

void *ndp_mode_loopback_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	thread_data->ret = ndp_mode_loopback_prepare(p);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}
	numa_run_on_node(ndp_queue_get_numa_node(p->rx));

	thread_data->state = TS_RUNNING;
	thread_data->ret = ndp_mode_loopback_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_loopback_exit(p);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int ndp_mode_loopback_prepare(struct ndp_tool_params *p)
{
	int ret = -1;

	p->si.progress_letter = 'L';
	/* Open device and queues */
	p->dev = nfb_open(p->nfb_path);
	if (p->dev == NULL){
		warnx("nfb_open() for queue %d failed.", p->queue_index);
		goto err_nfb_open;
	}

	p->rx = ndp_open_rx_queue(p->dev, p->queue_index);
	if (p->rx == NULL) {
		warnx("ndp_open_rx_queue(%d) failed.", p->queue_index);
		goto err_ndp_open_rx;
	}

	p->tx = ndp_open_tx_queue(p->dev, p->queue_index);
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
	ret = ndp_queue_start(p->rx);
	if (ret != 0) {
		warnx("ndp_rx_queue_start(%d) failed.", p->queue_index);
		goto err_ndp_start_rx;
	}

	gettimeofday(&p->si.startTime, NULL);

	return 0;

	/* Error handling */
err_ndp_start_rx:
	ndp_queue_stop(p->tx);
err_ndp_start_tx:
 	ndp_close_tx_queue(p->tx);
err_ndp_open_tx:
 	ndp_close_rx_queue(p->rx);
err_ndp_open_rx:
	nfb_close(p->dev);
err_nfb_open:
	return ret;
}

static int ndp_mode_loopback_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	ndp_queue_stop(p->rx);
	ndp_queue_stop(p->tx);
 	ndp_close_rx_queue(p->rx);
	ndp_close_tx_queue(p->tx);
	nfb_close(p->dev);
	return 0;
}

static int ndp_mode_loopback_loop(struct ndp_tool_params *p)
{
	unsigned cnt_rx, cnt_tx;
	unsigned burst_size = RX_BURST;
	struct ndp_packet packets_rx[burst_size];
	struct ndp_packet packets_tx[burst_size];
	struct ndp_queue *rx = p->rx;
	struct ndp_queue *tx = p->tx;
	struct stats_info *si = &p->si;

	update_stats_t update_stats = p->update_stats;

	unsigned i;
	for (i = 0; i < burst_size; i++) {
		packets_tx[i].flags = 0;
		packets_tx[i].header_length = 0;
	}

	while (!stop) {
		/* check limits if there is one (0 means loop forever) */
		if (p->limit_packets>0){
			/* limit reached */
			if (si->packet_cnt==p->limit_packets){
				break;
			}
			/* limit will be reached in one burst */
			if (si->packet_cnt+burst_size>p->limit_packets){
				burst_size = p->limit_packets - si->packet_cnt;
			}
		}

		if (p->limit_bytes>0 && si->bytes_cnt>p->limit_bytes){
			break;
		}

		cnt_rx = ndp_rx_burst_get(rx, packets_rx, burst_size);
		update_stats(packets_rx, cnt_rx, si);

		if (cnt_rx == 0) {
			ndp_tx_burst_flush(tx);
			delay_usecs(200);
			continue;
		}

		for (i = 0; i < cnt_rx; i++)
			packets_tx[i].data_length = packets_rx[i].data_length;
		cnt_tx = ndp_tx_burst_get(tx, packets_tx, cnt_rx);
		while (cnt_tx != cnt_rx && !stop) {
			delay_usecs(200);
			cnt_tx = ndp_tx_burst_get(tx, packets_tx, cnt_rx);
		}
		for (i = 0; i < cnt_tx; i++)
			memcpy(packets_tx[i].data, packets_rx[i].data, packets_rx[i].data_length);

		ndp_rx_burst_put(rx);
		ndp_tx_burst_put(tx);
	}
	ndp_tx_burst_flush(tx);

	return 0;
}
