/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - transmit module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <sys/time.h>
#include <numa.h>

#include <nfb/nfb.h>

#include "common.h"
#include "pcap.h"

static const unsigned DEFAULT_CACHE_CAPACITY = 1024;

struct pcap_cache {
	size_t                  capacity;
	size_t                  items;
	unsigned char           **packets;
	size_t                  *sizes;
	size_t                  offset;
};

struct pcap_src {
	bool                    is_cached;
	FILE                    *file;
	unsigned                loops;
	unsigned                current_loop;
	struct pcap_cache       cache;
};

static int ndp_mode_transmit_prepare(struct ndp_tool_params *p, struct pcap_src *src);
static int ndp_mode_transmit_exit(struct ndp_tool_params *p, struct pcap_src *src);
static int ndp_mode_transmit_loop(struct ndp_tool_params *p, struct pcap_src *src);

static int pcap_src_open(struct ndp_tool_params *params, struct pcap_src *src);
static void pcap_src_close(struct pcap_src *src);
static int pcap_src_burst_fill_meta(struct pcap_src *src, struct ndp_packet *packets, unsigned len);
static int pcap_src_burst_fill_data(struct pcap_src *src, struct ndp_packet *packets, unsigned len);

static int pcap_cache_create(struct pcap_cache *cache, FILE *sourcefile);
static void pcap_cache_destroy(struct pcap_cache *cache);

int ndp_mode_transmit(struct ndp_tool_params *p)
{
	int ret;
	struct pcap_src src;
	p->update_stats = update_stats;

	ret = ndp_mode_transmit_prepare(p, &src);
	if (ret)
		return ret;
	ret = ndp_mode_transmit_loop(p, &src);
	ndp_mode_transmit_exit(p, &src);
	return ret;
}

void *ndp_mode_transmit_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;
	size_t max_fn_l = strlen(p->pcap_filename) + 32;
	char pcap_fn_formatstr[max_fn_l];
	char pcap_fn[max_fn_l];
	struct ndp_tool_params local_p;
	struct pcap_src src;

	memset(pcap_fn_formatstr, 0, max_fn_l);
	memset(pcap_fn, 0, max_fn_l);

	p->update_stats = update_stats_thread;

	if (p->mode.transmit.multiple_pcaps) {
		memcpy(&local_p, p, sizeof(struct ndp_tool_params));
		if (str_expand_format(pcap_fn_formatstr, max_fn_l, p->pcap_filename, "td", "dd") >= max_fn_l-1) {
			warnx("Parameter expand overflow.");
		}
		if (snprintf(pcap_fn, max_fn_l, pcap_fn_formatstr, thread_data->thread_id, p->queue_index) >= (int)max_fn_l-1) {
			warnx("Parameter print expand overflow.");
		}
		local_p.pcap_filename = pcap_fn;
		p = &local_p;
	}

	thread_data->ret = ndp_mode_transmit_prepare(p, &src);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}

	numa_run_on_node(ndp_queue_get_numa_node(p->tx));

	thread_data->state = TS_RUNNING;
	thread_data->ret = ndp_mode_transmit_loop(p, &src);
	p->update_stats(0, 0, &p->si);
	ndp_mode_transmit_exit(p, &src);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int ndp_mode_transmit_prepare(struct ndp_tool_params *p, struct pcap_src *src)
{
	int ret = -1;

	p->si.progress_letter = 'T';
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

	ret = pcap_src_open(p, src);
	if (ret != 0) {
		warnx("initializing PCAP data source failed (file='%s', %s)",
				p->pcap_filename, p->mode.transmit.do_cache ? "cached" : "not cached");
		goto err_pcap_src_open;
	}

	gettimeofday(&p->si.startTime, NULL);

	return 0;

	/* Error handling */
	pcap_src_close(src);
err_pcap_src_open:
	ndp_queue_stop(p->tx);
err_ndp_start_tx:
 	ndp_close_tx_queue(p->tx);
err_ndp_open_tx:
	nfb_close(p->dev);
err_nfb_open:
	return ret;
}

static int ndp_mode_transmit_exit(struct ndp_tool_params *p, struct pcap_src *src)
{
	gettimeofday(&p->si.endTime, NULL);
	pcap_src_close(src);
	ndp_queue_stop(p->tx);
	ndp_close_tx_queue(p->tx);
	nfb_close(p->dev);
	return 0;
}

static int ndp_mode_transmit_loop(struct ndp_tool_params *p, struct pcap_src *src)
{
	unsigned i;
	int pkts_ready = 0;
	int cnt = 0;
	int pkts_filled = 0;
	unsigned burst_size = TX_BURST;
	struct ndp_packet packets[burst_size];
	struct ndp_queue *tx = p->tx;
	struct stats_info *si = &p->si;
	struct timeval status_time;
	bool min_invalid = false;

	/* Check throughput only every N cycles */
	unsigned status_num_of_loops = p->mode.transmit.mbps / 10000u;
	unsigned status_loop = status_num_of_loops;

	update_stats_t update_stats = p->update_stats;

	for (i = 0; i < burst_size; i++) {
		packets[i].flags = 0;
		packets[i].header_length = 0;
	}

	si->thread_total_bytes_cnt = 0;
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

		pkts_ready = pcap_src_burst_fill_meta(src, packets, burst_size);
		if (pkts_ready == 0) /* no more packets in source */
			break;

		for (int i = 0; i < pkts_ready; i++) {
			if (packets[i].data_length < p->mode.transmit.min_len) {
				min_invalid = true;
			}
		}

		if (min_invalid) {
			fprintf(stderr, "ERROR: Detected packet shorter than %lu bytes (defined by parameter \"-L\").\n", p->mode.transmit.min_len);
			break;
		}

		cnt = ndp_tx_burst_get(tx, packets, pkts_ready);
		while (cnt == 0 && !stop) {
			if (p->use_delay_nsec)
				delay_nsecs(1);
			cnt = ndp_tx_burst_get(tx, packets, pkts_ready);
		}

		pkts_filled = pcap_src_burst_fill_data(src, packets, cnt);

		update_stats(packets, pkts_filled, si);
		ndp_tx_burst_put(tx);

 		/* zero Mbps = unlimited throughput */
		if (p->mode.transmit.mbps != 0) {
			/* Check throughput only every N cycles */
			if (status_loop != 0) {
				status_loop--;
			} else {
				status_loop = status_num_of_loops;
				do {
					/* calculate elapsed time and expected bits */
					gettimeofday(&status_time, NULL);
					double elapsed_time = ((status_time.tv_sec - si->startTime.tv_sec) * 1000000) + (status_time.tv_usec - si->startTime.tv_usec);
					double expected_bits = elapsed_time * p->mode.transmit.mbps;

					/* total number of transferred bits by this thread */
					double transferred_bits = si->thread_total_bytes_cnt * 8;

					/* check threshold */
					if (expected_bits / transferred_bits < 1.0) {
						/* We have to pause sending packets for a while */
						//update_stats(packets, 0, si);
						ndp_tx_burst_flush(tx);
						if (p->use_delay_nsec)
							delay_nsecs(1);
					} else {
						break;
					}
				} while(true);
			}
		}
	}
	ndp_tx_burst_flush(tx);
	return 0;
}

int ndp_mode_transmit_init(struct ndp_tool_params *p)
{
	p->mode.transmit.do_cache = true;
	p->mode.transmit.loops = 1;
	p->mode.transmit.mbps = 0;
	p->mode.transmit.min_len = 0;
	p->mode.transmit.multiple_pcaps = false;
	return 0;
}

void ndp_mode_transmit_print_help()
{
	printf("Transmit parameters:\n");
	printf("  -f file       Read data from PCAP file <file>\n");
	printf("  -l loops      Loop over the PCAP file <loops> times (0 for forever)\n");
	printf("  -Z            Do not preload file in cache (slower, consumes less memory)\n");
	printf("  -m            Load PCAP file for each thread. -f parameter should contain %%t for thread_id or %%d fo dma_id\n");
	printf("  -s Mbps       Replay packets at a given speed\n");
	printf("  -L bytes      Minimal allowed frame length\n");
}

int ndp_mode_transmit_parseopt(struct ndp_tool_params *p, int opt, char *optarg)
{
	switch (opt) {
	case 'f':
		p->pcap_filename = optarg;
		break;
	case 'l':
		if (nc_strtoul(optarg, &p->mode.transmit.loops))
			errx(-1, "Cannot parse loops parameter");
		break;
	case 'Z':
		p->mode.transmit.do_cache = false;
		break;
	case 'm':
		p->mode.transmit.multiple_pcaps = true;
		break;
	case 's':
		if (nc_strtoull(optarg, &p->mode.transmit.mbps))
			errx(-1, "Cannot parse mbps parameter");
		break;
	case 'L':
		if (nc_strtoul(optarg, &p->mode.transmit.min_len))
			errx(-1, "Cannot parse min_len parameter");
		break;
	default:
		return -1;
	}
	return 0;
}

int ndp_mode_transmit_check(struct ndp_tool_params *p)
{
	if (p->pcap_filename == NULL) {
		errx(EXIT_FAILURE, "Parameter -f is mandatory");
	}
	return 0;
}

static int pcap_src_open(struct ndp_tool_params *params, struct pcap_src *src)
{
	src->is_cached = params->mode.transmit.do_cache;
	src->file = pcap_read_begin(params->pcap_filename);
	src->loops = params->mode.transmit.loops;
	src->current_loop = 1;

	if (!src->file) {
		warnx("cannot open PCAP file for reading");
		return -1;
	}

	if (src->is_cached) {
		return pcap_cache_create(&src->cache, src->file);
	}

	return 0;
}

static int pcap_src_burst_fill_meta(struct pcap_src *src, struct ndp_packet *packets, unsigned cnt)
{
	struct pcaprec_hdr_s phdr;
	int ret;

	if (cnt == 0)
		return 0;

	if (src->is_cached) {
		if (src->cache.offset == src->cache.items) {
			if (src->loops == 0 || src->current_loop < src->loops) {
				src->current_loop++;
				src->cache.offset = 0;
			} else {
				return 0;
			}
		}

		if (cnt >= src->cache.items - src->cache.offset) {
			cnt = src->cache.items - src->cache.offset;
		}

		for (unsigned i = 0; i < cnt; i++) {
			packets[i].data_length = src->cache.sizes[src->cache.offset + i];
		}

		return cnt;
	} else {
		ret = fread(&phdr, sizeof(phdr), 1, src->file);
		if (ret == 1) {
			packets[0].data_length = phdr.incl_len;
			return 1;
		}

		if (feof(src->file)) {
			if (src->loops == 0 || src->current_loop < src->loops) {
				ret = fseek(src->file, sizeof(struct pcap_hdr_s), SEEK_SET);
				if (ret < 0) {
					warn("error occured during file rewinding");
					return 0;
				}
				ret = fread(&phdr, sizeof(phdr), 1, src->file);
				if (ret != 1) {
					warn("even repeated PCAP file read failed, baling out");
					return 0;
				}

				src->current_loop++;
				packets[0].data_length = phdr.incl_len;
				return 1;
			} else {
				return 0;
			}
		} else {
			warn("error occured during reading PCAP file");
			return 0;
		}
	}
}

static int pcap_src_burst_fill_data(struct pcap_src *src, struct ndp_packet *packets, unsigned cnt)
{
	int ret;

	if (cnt == 0)
		return 0;

	if (src->is_cached) {
		for (unsigned i = 0; i < cnt; i++) {
			memcpy(packets[i].data, src->cache.packets[src->cache.offset + i], packets[i].data_length);
		}
		src->cache.offset += cnt;
		return cnt;
	} else {
		ret = fread(packets[0].data, packets[0].data_length, 1, src->file);
		if (ret == 0) {
			if (feof(src->file)) {
				warnx("premature EOF, PCAP contains packet header but not enough data");
			} else {
				warn("error occured during reading packet data from PCAP");
			}
		}
		return ret;
	}
}

static void pcap_src_close(struct pcap_src *src)
{
	fclose(src->file);
	if (src->is_cached)
		pcap_cache_destroy(&src->cache);
}

static int pcap_cache_create(struct pcap_cache *cache, FILE *sourcefile)
{
	struct pcaprec_hdr_s phdr;
	void * ptr = NULL;

	cache->capacity = DEFAULT_CACHE_CAPACITY;
	cache->items = 0;
	cache->offset = 0;
	cache->packets = malloc(sizeof(unsigned char *) * cache->capacity);
	cache->sizes = malloc(sizeof(size_t) * cache->capacity);

	if (cache->packets == NULL || cache->sizes == NULL) {
		warn("cannot allocate cache memory");
		goto err_init_malloc;
	}

	while (fread(&phdr, sizeof(phdr), 1, sourcefile) == 1) {
		cache->sizes[cache->items] = phdr.incl_len;
		cache->packets[cache->items] = malloc(phdr.incl_len);
		if (cache->packets[cache->items] == NULL) {
			warn("cannot allocate memory for packet %zd in cache", cache->items);
			goto err_packet_malloc;
		}
		if (fread(cache->packets[cache->items], phdr.incl_len, 1, sourcefile) != 1) {
			warn("error during reading packet %zd data from PCAP file to cache", cache->items);
			goto err_packet_read;
		}

		cache->items++;

		if (cache->items == cache->capacity) {
			cache->capacity *= 2;
			ptr = cache->packets;
			cache->packets = realloc(cache->packets, sizeof(unsigned char *) * cache->capacity);
			if (cache->packets == NULL) {
				warn("failed to reallocate memory for packet data");
				goto err_resize_malloc;
			}
			ptr = cache->sizes;
			cache->sizes = realloc(cache->sizes, sizeof(size_t) * cache->capacity);
			if (cache->sizes == NULL) {
				warn("failed to reallocate memory for packet sizes");
				goto err_resize_malloc;
			}
			ptr = NULL;
		}
	}

	if (feof(sourcefile)) {
		return 0;
	} else {
		warn("error during reading packet %zd header from PCAP file to cache", cache->items);
		goto err_ferror;
	}

err_resize_malloc:
	free(ptr);
err_packet_read:
	free(cache->packets[cache->items]);
err_ferror:
err_packet_malloc:
	for (unsigned i = 0; i < cache->items; i++) {
		free(cache->packets[i]);
	}
err_init_malloc:
	free(cache->sizes);
	free(cache->packets);

	return -1;
}

static void pcap_cache_destroy(struct pcap_cache *cache)
{
	for (unsigned i = 0; i < cache->offset; i++) {
		free(cache->packets[i]);
	}
	free(cache->sizes);
	free(cache->packets);
}
