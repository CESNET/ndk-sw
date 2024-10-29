/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Data transmission tool - statistics
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include "common.h"

#define CNT_FMT "20llu"

#define NDP_PACKET_HEADER_SIZE 4

#define CL_DUMP_CHARS_PER_LINE 32
#define CL_DUMP_CHARS_PER_WORD 8

void clear_stats_info(struct stats_info *si)
{
	if (!si->incremental) {
		si->packet_cnt = 0;
		si->bytes_cnt = 0;
		si->startTime = si->endTime;
		si->latency_sum = 0;
	}
}

void gather_stats_info(struct stats_info *si, struct stats_info *thread)
{
	si->packet_cnt += thread->thread_packet_cnt;
	si->bytes_cnt += thread->thread_bytes_cnt;
	if (!si->incremental) {
		thread->thread_packet_cnt = 0;
		thread->thread_bytes_cnt = 0;
	}

}

void update_stats_loop_thread(int interval, struct thread_data **pdata, int thread_cnt, struct list_range *qr, struct stats_info *si)
{
	int i;
	int finished;
	int qri, qrc;

	time_t next_tick;
	struct timeval now;
	unsigned long long prev, diff;
	unsigned long long pkts, bytes;
	unsigned long long thread_pkts, thread_bytes;
	unsigned long long brl1, brl2, brpcie;

	struct thread_data *thread_data;

	if (interval == 0) {
		while (!stop) {
			usleep(25000);
			finished = 0;

			/* Print stats on signal */
			if (stats) {
				gettimeofday(&si->endTime, NULL);

				for (i = 0; i < thread_cnt; i++) {
					thread_data = pdata[i];
					if (thread_data->state == TS_RUNNING) {
						pthread_spin_lock(&thread_data->lock);
						gather_stats_info(si, &thread_data->params.si);
						clear_stats_info(&thread_data->params.si);

						pthread_spin_unlock(&thread_data->lock);
					}
				}
				print_stats(si);
				clear_stats_info(si);
				stats = 0;
			}

			for (i = 0; i < thread_cnt; i++) {
				thread_data = pdata[i];
				if (thread_data->state == TS_FINISHED) {
					if (++finished == thread_cnt)
						stop = 1;
				}
			}
		}
		return;
	}

	initscr();

	mvprintw(0, 12, "Packets");
	mvprintw(0, 30, "Bytes");
	mvprintw(0, 50, "L1 Mbps");
	mvprintw(0, 60, "L2 Mbps");
	mvprintw(0, 70, "PCIe Mbps");

	qri = 0;
	qrc = 0;
	for (i = 0; i < thread_cnt; i++) {
		if (qr->min[qri] + qrc > qr->max[qri]) {
			qri++;
			qrc = 0;
		}
		mvprintw(i+1, 0, "Channel %d:", qr->min[qri] + qrc++);
	}

	gettimeofday(&now, NULL);
	prev = now.tv_sec * 1000 + now.tv_usec / 1000 - 1000;
	next_tick = time(NULL);

	while (!stop) {
		if (time(NULL) >= next_tick) {
			gettimeofday(&now, NULL);
			diff = (now.tv_sec * 1000 + now.tv_usec / 1000) - prev;
			if (!si->incremental)
				prev += diff;

			pkts = bytes = 0;

			finished = 0;
			for (i = 0; i < thread_cnt; i++){
				thread_data = pdata[i];
				if (thread_data->state == TS_RUNNING) {
					pthread_spin_lock(&thread_data->lock);

					thread_pkts = thread_data->params.si.thread_packet_cnt;
					thread_bytes = thread_data->params.si.thread_bytes_cnt;
					if (!si->incremental) {
						thread_data->params.si.thread_packet_cnt = 0;
						thread_data->params.si.thread_bytes_cnt = 0;
						si->packet_cnt += thread_pkts;
						si->bytes_cnt += thread_bytes;
					}
					pthread_spin_unlock(&thread_data->lock);

					brl1       = (thread_bytes + thread_pkts * 24) * 8 / 1000 / diff;
					brl2       = (thread_bytes + thread_pkts * 4)  * 8 / 1000 / diff;
					brpcie     = (thread_bytes + thread_pkts * 12) * 8 / 1000 / diff;
					mvprintw(i+1, 12, "%17llu", thread_pkts);
					mvprintw(i+1, 30, "%17llu", thread_bytes);
					mvprintw(i+1, 50, "%9llu", brl1);
					mvprintw(i+1, 60, "%9llu", brl2);
					mvprintw(i+1, 70, "%9llu", brpcie);

					pkts += thread_pkts;
					bytes += thread_bytes;
				} else {
					if (thread_data->state == TS_FINISHED) {
						if (++finished == thread_cnt)
							stop = 1;
					}
					mvprintw(i+1, 26, "N/A");
				}
			}

			brl1   = (bytes + pkts * 24) * 8 / 1000 / diff;
			brl2   = (bytes + pkts * 4)  * 8 / 1000 / diff;
			brpcie = (bytes + pkts * 12) * 8 / 1000 / diff;
			mvprintw(thread_cnt+2, 0, "Total:");
			mvprintw(thread_cnt+2, 12, "%17llu", pkts);
			mvprintw(thread_cnt+2, 30, "%17llu", bytes);
			mvprintw(thread_cnt+2, 50, "%9llu", brl1);
			mvprintw(thread_cnt+2, 60, "%9llu", brl2);
			mvprintw(thread_cnt+2, 70, "%9llu", brpcie);
			next_tick = time(NULL) + interval;
			refresh();
		}
		usleep(25000);
	}

	endwin();
}

static inline void print_packet(struct ndp_packet *packet, struct stats_info *si)
{
	unsigned i;

	if (si->progress_type == PT_ALL || si->progress_type == PT_HEADER) {
		for (i = 0; i < packet->header_length; i++) {
			if (!(i % CL_DUMP_CHARS_PER_LINE))
				printf("\nhdr  %4x: ", i);
			else if (!(i % CL_DUMP_CHARS_PER_WORD))
				printf("   ");
			printf("%02x ", packet->header[i]);
		}
	}

	if (si->progress_type == PT_ALL || si->progress_type == PT_DATA) {
		for (i = 0; i < packet->data_length; i++) {
			if (!(i % CL_DUMP_CHARS_PER_LINE))
				printf("\ndata %4x: ", i);
			else if (!(i % CL_DUMP_CHARS_PER_WORD))
				printf("   ");
			printf("%02x ", packet->data[i]);
		}
	}
	printf("\n");
}

static inline void update_print_progress(struct ndp_packet *packets, int count, struct stats_info *si)
{
	int i;

	si->packet_cnt += count;
	for (i = 0; i < count; i++) {
		si->bytes_cnt += packets[i].data_length;
		si->thread_total_bytes_cnt += packets[i].data_length;

		if (si->progress_type != PT_NONE) {
			si->progress_counter++;
			if (si->progress_counter >= si->sampling) {
				si->progress_counter -= si->sampling;

				if (si->progress_type == PT_LETTER) {
					putchar(si->progress_letter);
					fflush(stdout);
				} else {
					print_packet(&packets[i], si);
				}
			}
		}
	}
}

void update_stats(struct ndp_packet *packets, int count, struct stats_info *si)
{
	update_print_progress(packets, count, si);
	/* Print stats on signal */
	if (stats) {
		gettimeofday(&si->endTime, NULL);
		print_stats(si);
		clear_stats_info(si);
		stats = 0;
	}
}

void update_stats_thread(struct ndp_packet *packets, int count, struct stats_info *si)
{
	update_print_progress(packets, count, si);

	si->last_update += count;
	if (si->last_update > 100000 || count == 0) {
		/* Update global stats */
		pthread_spin_lock(&((struct thread_data *)si->priv)->lock);
		si->thread_packet_cnt += (si->packet_cnt - si->last_packet_cnt);
		si->thread_bytes_cnt += (si->bytes_cnt - si->last_bytes_cnt);
		pthread_spin_unlock(&((struct thread_data *)si->priv)->lock);

		si->last_packet_cnt = si->packet_cnt;
		si->last_bytes_cnt = si->bytes_cnt;
		si->last_update = 0;
	}
}

void print_stats(struct stats_info *si)
{
	uint64_t elapsed_usecs = 1000000 *
			(si->endTime.tv_sec - si->startTime.tv_sec) +
			(si->endTime.tv_usec - si->startTime.tv_usec);

	double elapsed_secs = (double) elapsed_usecs / 1000000;

	unsigned i;

	for (i = 0; i < 39 - strlen(module->name); i++)
		printf("-");
	printf(" NDP %s stats ----\n", module->name);

	printf("Packets                    : %" CNT_FMT "\n", si->packet_cnt);
	printf("Bytes                      : %" CNT_FMT "\n", si->bytes_cnt);
	printf("Avg speed [Mpps]           : % 24.3f\n", (double) si->packet_cnt / elapsed_usecs);
	printf("Avg speed L1 [Mb/s]        : % 24.3f\n", (double) (si->bytes_cnt + si->packet_cnt * 24) * 8 / elapsed_usecs);
	printf("Avg speed L2 [Mb/s]        : % 24.3f\n", (double) (si->bytes_cnt + si->packet_cnt *  4) * 8 / elapsed_usecs);
	printf("Time                       : % 24.3f\n", elapsed_secs);
	if(module->stats_cb != NULL)
		module->stats_cb(si);
}
