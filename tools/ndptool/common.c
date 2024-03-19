/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - common code
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <numa.h>
#include "common.h"

void delay_usecs(unsigned int us)
{
	struct timespec t1;
	struct timespec t2;

	if (us == 0)
		return;

	t1.tv_sec = (us / 1000000);
	t1.tv_nsec = (us % 1000000) * 1000;

	/* NB: Other variants of sleep block whole process. */
retry:
	if (nanosleep((const struct timespec *)&t1, &t2) == -1)
		if (errno == EINTR) {
			t1 = t2; /* struct copy */
			goto retry;
		}
	return;
}

void delay_nsecs(unsigned int ns)
{
	struct timespec t1;
	struct timespec t2;

	if (ns == 0)
		return;

	t1.tv_sec = (ns / 1000000000);
	t1.tv_nsec = (ns % 1000000000);

	/* NB: Other variants of sleep block whole process. */
retry:
	if (nanosleep((const struct timespec *)&t1, &t2) == -1)
		if (errno == EINTR) {
			t1 = t2; /* struct copy */
			goto retry;
		}
	return;
}

void adjust_tx_throughput(unsigned status_num_of_loops, unsigned long long throughput_mbps,
			bool use_delay_nsec, struct stats_info *si, struct ndp_queue *tx)
{
	unsigned status_loop = status_num_of_loops;
	struct timeval status_time;

	/* zero Mbps = unlimited throughput */
	if (throughput_mbps != 0) {
		/* Check throughput only every N cycles */
		if (status_loop != 0) {
			status_loop--;
		} else {
			status_loop = status_num_of_loops;
			do {
				/* calculate elapsed time and expected bits */
				gettimeofday(&status_time, NULL);
				double elapsed_time = ((status_time.tv_sec - si->startTime.tv_sec) * 1000000) + (status_time.tv_usec - si->startTime.tv_usec);
				double expected_bits = elapsed_time * throughput_mbps;

				/* total number of transferred bits by this thread */
				double transferred_bits = si->thread_total_bytes_cnt * 8;

				/* check threshold */
				if (expected_bits / transferred_bits < 1.0) {
					/* We have to pause sending packets for a while */
					//update_stats(packets, 0, si);
					ndp_tx_burst_flush(tx);
					if (use_delay_nsec)
						delay_nsecs(1);
				} else {
					break;
				}
			} while(true);
		}
	}
}
