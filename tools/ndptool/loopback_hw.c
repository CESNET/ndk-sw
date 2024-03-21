/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - loopback module
 *
 * Copyright (C) 2021-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jan Kubalek <kubalek@cesnet.cz>
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <err.h>
#include <numa.h>
#include <endian.h>
#include <time.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include "common.h"
#include "structured_packet.h"

#define MAX_PACKET_SIZE 4096

static int ndp_mode_loopback_hw_prepare(struct ndp_tool_params *p);
static int ndp_mode_loopback_hw_loop   (struct ndp_tool_params *p);
static int ndp_mode_loopback_hw_exit   (struct ndp_tool_params *p);

int ndp_mode_loopback_hw(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = ndp_mode_loopback_hw_prepare(p);
	if (ret)
		return ret;
	ret = ndp_mode_loopback_hw_loop(p);
	ndp_mode_loopback_hw_exit(p);
	return ret;
}

void *ndp_mode_loopback_hw_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	thread_data->ret = ndp_mode_loopback_hw_prepare(p);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}
	numa_run_on_node(ndp_queue_get_numa_node(p->tx));

	thread_data->state = TS_RUNNING;
	thread_data->ret = ndp_mode_loopback_hw_loop(p);
	p->update_stats(0, 0, &p->si);
	ndp_mode_loopback_hw_exit(p);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int pregenerate(struct ndp_mode_loopback_hw_params *p)
{
	uint16_t i, e;
	int32_t min, max;

	srand(p->srand);

	e = 0;
	for (i = 0; i < PREGEN_SEQ_SIZE; i++) {
		min = p->range.min[e];
		max = p->range.max[e];

		if (max == 0)
			p->pregen_sizes[i] = (uint16_t)min;
		else
			p->pregen_sizes[i] = (uint16_t)(rand() % (max - min + 1)) + min;

		p->pregen_ids[i] = (uint32_t)rand();

		e++;
		if (e >= p->range.items)
			e = 0;
	}

	// Create a shadow of the arrays
	memcpy(&(p->pregen_sizes[PREGEN_SEQ_SIZE]),p->pregen_sizes,sizeof(p->pregen_sizes[PREGEN_SEQ_SIZE]) * PREGEN_SEQ_SIZE);
	memcpy(&(p->pregen_ids  [PREGEN_SEQ_SIZE]),p->pregen_ids  ,sizeof(p->pregen_ids  [PREGEN_SEQ_SIZE]) * PREGEN_SEQ_SIZE);

	p->pregen_ptr = 0;

	return 0;
}

static int ndp_mode_loopback_hw_prepare(struct ndp_tool_params *p)
{
	int ret = -1;
	struct timeval seed_time;

	/* Pregenerate packet sizes and IDs */
	gettimeofday(&seed_time, NULL);
	p->mode.loopback_hw.srand = (uint32_t)seed_time.tv_usec + (uint32_t)p->queue_index;
	ret = pregenerate(&p->mode.loopback_hw);
	if (ret) {
		warnx("pregenerate() for queue %d failed.", p->queue_index);
		goto err_pregen;
	}
	ret = -1;

	p->si.progress_letter = 'L';
	/* Open device and queues */
	p->dev = nfb_open(p->nfb_path);
	if (p->dev == NULL) {
		warnx("nfb_open() for queue %d failed.", p->queue_index);
		goto err_nfb_open;
	}

	p->rx = ndp_open_rx_queue_ext(p->dev, p->queue_index, p->use_userspace_flag ? NDP_OPEN_FLAG_USERSPACE : 0);
	if (p->rx == NULL) {
		warnx("ndp_open_rx_queue(%d) failed.", p->queue_index);
		goto err_ndp_open_rx;
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
err_pregen:
	return ret;
}

static int ndp_mode_loopback_hw_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	ndp_queue_stop(p->rx);
	ndp_queue_stop(p->tx);
	ndp_close_tx_queue(p->rx);
	ndp_close_tx_queue(p->tx);
	//nfb_close(p->dev);
	return 0;
}

// Generates a Burst of packets
// Puts packet sizes in 'packets' and packet data to 'packet_data'
static int generate_burst(struct ndp_mode_loopback_hw_params *p, struct ndp_packet *packets, uint8_t **packet_data, uint32_t pkt_count, uint16_t queue_index, uint16_t burst_index)
{
	uint32_t i;
	uint16_t ptr;
	uint16_t size;
	uint8_t packet_id = burst_index+1;
	struct ndp_packet *pkt;
	static int32_t cnt = 0;

	struct timeval tv;
	uint64_t usec_time = 0;

	if(module->flags & LATENCY_FLAG){
		gettimeofday(&tv,NULL);
		usec_time = 1000000 * tv.tv_sec + tv.tv_usec;
	}

	structured_packet_t sp;

	ptr = p->pregen_ptr;

	__builtin_prefetch(&(p->pregen_sizes[ptr]));

	if (0 && queue_index==0)
		printf("TX %d: 0x%x 0x%x\n",queue_index,burst_index,packet_id+1);

	for (i = 0; i < pkt_count; i++) {
		pkt = &packets[i];

		size = p->pregen_sizes[ptr];

//      Not used currently
//		id = p->pregen_ids[ptr];
//		p->pregen_ids[ptr]++;

		ptr++;

		pkt->flags = 0;
		pkt->header_length = 0;
		pkt->data_length   = size;

		sp_init(&sp,queue_index,size,burst_index,packet_id, usec_time);
		packet_id++;

		sp_generate_data_fast(&sp,(uint32_t*)packet_data[i]);

		if (0 && (cnt % (64*1024))==0)
			sp_print(stdout,&sp,"TX: Generated Packet",packet_data[i]);
	}

	if (ptr >= PREGEN_SEQ_SIZE)
		ptr -= PREGEN_SEQ_SIZE;

	p->pregen_ptr = ptr;

	cnt++;

	return 0;
}

static int check_burst(struct ndp_packet *packets, uint32_t pkt_count, struct ndp_tool_params *p, uint16_t *rx_burst_index)
{
	uint16_t i;
	int ret;
	uint32_t *data_ptr;
	uint32_t *data_ptr_prev = NULL;
	uint16_t burst_id_diff;
	uint8_t packet_id_diff;
	uint16_t size;
	char description[512];

	structured_packet_t sp0_data;
	structured_packet_t sp1_data;

	structured_packet_t *sp = &sp0_data;
	structured_packet_t *sp_prev = &sp1_data;
	structured_packet_t *sp_ptr_tmp;

	struct timeval tv;
	uint64_t usec_time0 = 0;
	uint64_t usec_time0_host = 0;
	uint64_t usec_time1 = 0;
	uint64_t *time_ptr;

	sp_init(sp_prev,p->queue_index,0,*rx_burst_index,-1, 0);

	if(module->flags & LATENCY_FLAG){
		gettimeofday(&tv, NULL);
		usec_time1 = 1000000 * tv.tv_sec + tv.tv_usec;
	}

	ret = 0;
	for (i = 0; i < pkt_count; i++) {
		size = packets[i].data_length;

		data_ptr = (uint32_t*)packets[i].data;
		if(module->flags & LATENCY_FLAG){
			time_ptr = (uint64_t*)&(data_ptr[0]);

			usec_time0 = *time_ptr;
			usec_time0_host = be64toh(usec_time0);

			p->si.latency_sum += usec_time1 - usec_time0_host;
		}

		sp_reconstruct(sp,data_ptr[2],p->queue_index,size, usec_time0);

		if (0 && p->queue_index == 0) {
			snprintf(description,512,"latency %"PRIu64" - %"PRIu64" = %"PRIu64"\n",usec_time1,usec_time0_host,usec_time1-usec_time0_host);
			sp_print(stdout,sp,description,data_ptr);
		}

		burst_id_diff  = sp->burst_id  - sp_prev->burst_id;
		packet_id_diff = sp->packet_id - sp_prev->packet_id;
		if (0 && ((burst_id_diff > 1) || (burst_id_diff == 0 && packet_id_diff > 1 && data_ptr_prev != NULL))) {
			snprintf(description,512,"WARNING: Very big gap of (burst_id,packet_id) between pakcets: (%d,%d)!",burst_id_diff,packet_id_diff);
			sp_print(stderr,sp,description,data_ptr);
			sp_print(stderr,sp_prev,"Previous packet",data_ptr_prev);
		}
		if (0 && burst_id_diff && p->queue_index==0)
			printf("             RX %d: 0x%x 0x%x; burst change: %d\n",p->queue_index,sp->burst_id,sp->packet_id,burst_id_diff);

		ret = sp_check_data_fast(sp,(uint32_t*)data_ptr);
		if (ret) {
			sp_print(stderr,sp_prev,"Previous packet",data_ptr_prev);
			ret = ENOMSG;
			break;
		}

		data_ptr_prev = data_ptr;

		// Swap
		sp_ptr_tmp = sp_prev;
		sp_prev = sp;
		sp = sp_ptr_tmp;
	}

	*rx_burst_index = sp_prev->burst_id;

	return ret;
}

static int ndp_mode_loopback_hw_loop(struct ndp_tool_params *p)
{
	int ret = 0;
	unsigned cnt;
	unsigned zero_cnt_cnt = 0;
	unsigned tx_burst_size = TX_BURST;
	const unsigned rx_burst_size = TX_BURST;
	struct ndp_packet packets[TX_BURST];
	uint8_t *packet_data[TX_BURST];
	struct ndp_queue *tx = p->tx;
	struct ndp_queue *rx = p->rx;
	struct stats_info *si = &p->si;
	bool finish = false;

	update_stats_t update_stats = p->update_stats;

	unsigned i;

	uint16_t burst_index = 0;
	uint16_t rx_burst_index = 0xffff;

	uint64_t bytes_cnt = 0;
	uint64_t packets_rem = p->limit_packets;

	uint64_t rx_pkt_cnt = 0;

	const bool limit_bytes   = p->limit_bytes > 0 ? true : false;
	const bool limit_packets = p->limit_packets > 0 ? true : false;

	/* Clear length of packet header */
	for (i = 0; i < rx_burst_size; i++) {
		packets[i].header_length = 0;
		packet_data[i] = malloc(MAX_PACKET_SIZE);
		if (packet_data[i] == NULL) {
			warnx("malloc() for packet %u space on queue %d failed.",i,p->queue_index);
			ret = ENOMEM;
			goto err_pkt_alloc;
		}
	}

	while (!stop) {
		/*
		 * Transmit side
		 */

		if (limit_packets) {
			/* Packet limit was reached in previous burst */
			if (packets_rem == 0) {
				finish = true;
			}
			/* Packet limit will be reached in one burst */
			if (packets_rem < tx_burst_size) {
				tx_burst_size = packets_rem;
			}
		}

		if (limit_bytes) {
			if (bytes_cnt >= p->limit_bytes && !finish) {
				finish = true;
				ndp_tx_burst_flush(tx);
			}
		}

		if (!finish) {
			/* Fill packets to send */
			generate_burst(&p->mode.loopback_hw,packets,packet_data,tx_burst_size,p->queue_index,burst_index);

			/* Request packet descriptors */
			cnt = ndp_tx_burst_get(tx, packets, tx_burst_size);
			while (cnt != tx_burst_size) {
				if (stop)
					return ret;
				if (0 && p->queue_index==0)
					printf("TX stall\n");
				if (p->use_delay_nsec)
					delay_nsecs(1);
				cnt = ndp_tx_burst_get(tx,packets,tx_burst_size);
			}

			/* Copy data of generated packets to received pointers */
			for (i = 0; i < tx_burst_size; i++)
				memcpy(packets[i].data,packet_data[i],packets[i].data_length);

			/* Update limits */
			packets_rem -= cnt;
			if (limit_bytes) {
				for (i = 0; i < cnt; i++) {
					bytes_cnt += packets[i].data_length;
				}
			}

			/* Update stats */
			update_stats(packets,cnt,si);

			/* Release packet descriptors */
			ndp_tx_burst_put(tx);
			ndp_tx_burst_flush(tx);
		}

		/*
		 * Receive side
		 */

		/* Request packet descriptors with data */
		cnt = ndp_rx_burst_get(rx,packets,rx_burst_size);
		if (0 && cnt != rx_burst_size && p->queue_index==0)
			printf("RX stall\n");

		if (cnt) {
			zero_cnt_cnt = 0;
			ret = check_burst(packets,cnt,p,&rx_burst_index);
			if (ret) {
				stop = 1;
				ndp_tx_burst_flush(tx);
				break;
			}
		} else {
			zero_cnt_cnt++;
		}

		rx_pkt_cnt += cnt;

		/* Release packet descriptors */
		ndp_rx_burst_put(rx);

		burst_index++;

		if (finish && zero_cnt_cnt>=1000)
			break;
	}

	if (0 )
		printf("%s: queue %u: RX CNT %"PRIu64"\n",__func__,p->queue_index,rx_pkt_cnt);

err_pkt_alloc:
	for (i = 0; i < rx_burst_size ; i++) {
		if (packet_data[i] == NULL)
			break;
		free(packet_data[i]);
	}
	return ret;
}

int ndp_mode_loopback_hw_init(struct ndp_tool_params *p)
{
	list_range_init(&p->mode.loopback_hw.range);
	p->mode.loopback_hw.pregen_ptr = 0;
	return 0;
}

void ndp_mode_loopback_hw_print_help()
{
	printf("Generate parameters:\n");
	printf("  -s size       Packet size - list or random from range, e.g \"64,128-256\"\n");
	printf("Loopback Hardware parameters:\n");
	printf("  -l            Latency mode - prints latency of hardware loopback\n");
}

void ndp_mode_loopback_hw_print_latency(struct stats_info *si){
	printf("Avg latency (ms)           : % 24.3f\n", (si->latency_sum/si->packet_cnt)/1000.0f);
}

int ndp_mode_loopback_hw_parseopt(struct ndp_tool_params *p, int32_t opt, char *optarg,
		int option_index __attribute__((unused)))
{
	switch (opt) {
	case 's':
		if (list_range_parse(&p->mode.loopback_hw.range, optarg) < 0)
			errx(-1, "Cannot parse size range");
		break;
	case 'l':
		module->stats_cb = ndp_mode_loopback_hw_print_latency;
		module->flags |= LATENCY_FLAG;
		break;
	default:
		return -1;
	}
	return 0;
}

int ndp_mode_loopback_hw_check(struct ndp_tool_params *p)
{
	if (list_range_empty(&p->mode.loopback_hw.range)) {
		errx(-1, "Unspecified size parameter");
	}

	return 0;
}

void ndp_mode_loopback_hw_destroy(struct ndp_tool_params *p)
{
	list_range_destroy(&p->mode.loopback_hw.range);
}
