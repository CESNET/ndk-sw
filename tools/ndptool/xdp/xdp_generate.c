#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <err.h>
#include <numa.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include "xdp_common.h"

static int xdp_mode_generate_prepare(struct ndp_tool_params *p);
static int xdp_mode_generate_loop(struct ndp_tool_params *p);
static int xdp_mode_generate_exit(struct ndp_tool_params *p);

int xdp_mode_generate(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = xdp_mode_generate_prepare(p);
	if (ret)
		return ret;
	ret = xdp_mode_generate_loop(p);
	xdp_mode_generate_exit(p);
	return ret;
}

void *xdp_mode_generate_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	thread_data->ret = xdp_mode_generate_prepare(p);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}

	thread_data->state = TS_RUNNING;
	thread_data->ret = xdp_mode_generate_loop(p);
	p->update_stats(0, 0, &p->si);
	xdp_mode_generate_exit(p);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int xdp_mode_generate_prepare(struct ndp_tool_params *p)
{
	p->si.progress_letter = 'G';
	gettimeofday(&p->si.startTime, NULL);

	return 0;
}

static int xdp_mode_generate_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	return 0;
}

static int xdp_mode_generate_loop(struct ndp_tool_params *p)
{
	unsigned burst_size = TX_BURST;
	struct ndp_packet packets[burst_size];
	struct stats_info *si = &p->si;
	update_stats_t update_stats = p->update_stats;
	struct ndp_mode_xdp_params *params = &p->mode.xdp;
	struct ndp_mode_xdp_xsk_data *xsk_data = NULL;
	struct xsk_ring_cons *comp_ring;
	struct xsk_ring_prod *tx_ring;
	struct addr_stack stack;

	int gen_index = 0;

	unsigned long long bytes_cnt = 0;
	unsigned long long packets_rem = p->limit_packets;

	const bool clear_data    = p->mode.xdp.generate.clear_data;
	const bool limit_bytes   = p->limit_bytes > 0 ? true : false;
	const bool limit_packets = p->limit_packets > 0 ? true : false;


	// Clear length of packet header
	for (unsigned i = 0; i < burst_size; i++) {
		packets[i].flags = 0;
		packets[i].header_length = 0;
	}

	// OPT: Set the constant values in packet for one packet length
	if (p->mode.xdp.generate.range.items == 1 && p->mode.xdp.generate.range.max[0] == 0) {
		gen_index = -1;
		for (unsigned i = 0; i < burst_size; i++) {
			packets[i].data_length = p->mode.xdp.generate.range.min[0];
		}
	}

	// find socket data and check if socket is alive
	for (unsigned i = 0; i < params->socket_cnt; i++) {
		if(p->queue_index == params->queue_data_arr[i].nfb_qid) {
			xsk_data = &params->queue_data_arr[i];
		}
	}
	if(!xsk_data) {
		fprintf(stderr,"Failed to match socket data for queue: %d\n", p->queue_index);
		return -1;
	}
	if(!xsk_data->alive) {
		fprintf(stderr,"Socket for queue: %d failed to initialize\n", p->queue_index);
		return -1;
	}
	comp_ring = &xsk_data->umem_info.comp_ring;
	tx_ring = &xsk_data->xsk_info.tx_ring;

	// Fill the address stack with addresses into the umem
	init_addr(&stack, xsk_data->umem_info.umem_cfg.frame_size);

	while (!stop) {
		if (limit_packets > 0) {
			// Packet limit was reached in previous burst
			if (packets_rem == 0) {
				break;
			}
			// Packet limit will be reached in one burst
			if (packets_rem < burst_size) {
				burst_size = packets_rem;
			}
		}

		if (limit_bytes > 0) {
			if (bytes_cnt >= p->limit_bytes) {
				break;
			}
		}

		// Fill parameters for packets to send
		if (gen_index != -1) {
			for (unsigned i = 0; i < burst_size; i++) {
				packets[i].data_length = p->mode.xdp.generate.range.min[gen_index];
				if (p->mode.xdp.generate.range.max[gen_index])
					packets[i].data_length += nc_fast_rand(&p->mode.xdp.generate.srand) % p->mode.xdp.generate.range.max[gen_index];
				if ((unsigned) ++gen_index == p->mode.xdp.generate.range.items)
					gen_index = 0;
			}
		}

		unsigned idx_tx = 0;
		unsigned collected;
		unsigned cnt;
		// Collect sent buffers
		do {
			collected = xsk_ring_cons__peek(comp_ring, burst_size, &idx_tx);
			for (unsigned i = 0; i < collected; i++) {
				free_addr(&stack, *xsk_ring_cons__comp_addr(comp_ring, idx_tx++));
			}
			xsk_ring_cons__release(comp_ring, collected);
		} while(collected || stack.addr_cnt < burst_size);

		// Reserve descriptors
		cnt = xsk_ring_prod__reserve(tx_ring, burst_size, &idx_tx);
		while (cnt != burst_size) {
			if (stop)
				return 0;
			delay_nsecs(1);
			cnt = xsk_ring_prod__reserve(tx_ring, burst_size, &idx_tx);
		} 

		// Fill the descriptors
		for (unsigned i = 0; i < burst_size; i++) {	
			uint64_t addr = alloc_addr(&stack);
			struct xdp_desc *desc = xsk_ring_prod__tx_desc(tx_ring, idx_tx++);
			// fill the xdp desc
			desc->addr = addr;
			desc->len = packets[i].data_length;
			void *data = xsk_umem__get_data(xsk_data->umem_info.umem_area, desc->addr);
			// update the packets structure for compatibility with ndptool
			packets[i].data = data;
		}
		if (clear_data) {
			for (unsigned i = 0; i < cnt; i++) {
				memset(packets[i].data, 0, packets[i].data_length);
			}
		}

		// Update limits
		packets_rem -= cnt;
		if (limit_bytes) {
			for (unsigned i = 0; i < cnt; i++) {
				bytes_cnt += packets[i].data_length;
			}
		}

		// Update stats
		update_stats(packets, cnt, si);

		// Release packet descriptors
		xsk_ring_prod__submit(tx_ring, cnt);
	}

	if(xsk_data->alive) {
		xsk_socket__delete(xsk_data->xsk_info.xsk);
		xsk_umem__delete(xsk_data->umem_info.umem);
		free(xsk_data->umem_info.umem_area);
		xsk_data->alive = false;
	}
	return 0;
}

int xdp_mode_generate_init(struct ndp_tool_params *p)
{
	list_range_init(&p->mode.xdp.generate.range);
	p->mode.xdp.generate.mbps = 0;
	return 0;
}

void xdp_mode_generate_print_help()
{
	printf("Generate parameters:\n");
	printf("  -s size       Packet size - list or random from range, e.g \"64,128-256\"\n");
	printf("  -C            Clear packet data before send\n");
	printf("  --speed Mbps  Replay packets at a given speed\n");
}

int xdp_mode_generate_parseopt(struct ndp_tool_params *p, int opt, char *optarg,
		int option_index)
{
	switch (opt) {
	case 0:
		if (!strcmp(module->long_options[option_index].name, "speed")) {
			if (nc_strtoull(optarg, &p->mode.xdp.generate.mbps))
				errx(-1, "Cannot parse --speed parameter");
		} else {
			errx(-1, "Unknown long option");
		}
		break;
	case 's':
		if (list_range_parse(&p->mode.xdp.generate.range, optarg) < 0)
			errx(-1, "Cannot parse size range");
		break;
	case 'C':
		p->mode.xdp.generate.clear_data = 1;
		break;
	case 'S':
		if (nc_strtoull(optarg, &p->mode.xdp.generate.mbps))
			errx(-1, "Cannot parse mbps parameter");
		break;
	default:
		return -1;
	}
	return 0;
}

int xdp_mode_generate_check(struct ndp_tool_params *p)
{
	struct ndp_mode_xdp_params *params = &p->mode.xdp;
	
	uint32_t pagesize = getpagesize();
	int ret;
	unsigned i;
	// Compatibility params
	if (list_range_empty(&p->mode.xdp.generate.range)) {
		errx(-1, "Unspecified size parameter");
	}
	
	// Convert random ranges from [min, max] to [min, delta + 1]
	// So that min + rand(seed) % (delta + 1) randomizes the lengths
	for (i = 0; i < p->mode.xdp.generate.range.items; i++) {
		p->mode.xdp.generate.range.max[i] -= p->mode.xdp.generate.range.min[i];
		if (p->mode.xdp.generate.range.max[i])
			p->mode.xdp.generate.range.max[i]++;
	}

	xdp_mode_common_parse_queues(p);

	// create UMEM and XSK for each queue
	for (i = 0; i < params->socket_cnt; i++) {
		// Skip unopen queues
		if(!params->queue_data_arr[i].alive)
			continue;

		// UMEM
		struct umem_info *uinfo = &params->queue_data_arr[i].umem_info;
		uinfo->size = NUM_FRAMES * pagesize;
		uinfo->umem_cfg.comp_size = NUM_FRAMES;
		uinfo->umem_cfg.fill_size = NUM_FRAMES;
		uinfo->umem_cfg.flags = 0;
		uinfo->umem_cfg.frame_headroom = 0;
		uinfo->umem_cfg.frame_size = pagesize;

		if(posix_memalign(&uinfo->umem_area, pagesize, uinfo->size)) {
			fprintf(stderr, "Failed to get allocate umem buff for queue %d\n", params->queue_data_arr[i].eth_qid);
			params->queue_data_arr[i].alive = false;
			continue;
		}

		if((ret = xsk_umem__create(&uinfo->umem, uinfo->umem_area, uinfo->size, &uinfo->fill_ring, &uinfo->comp_ring, &uinfo->umem_cfg))) {
			fprintf(stderr, "Failed to create umem for queue %d; ret: %d\n", params->queue_data_arr[i].eth_qid, ret);
			free(uinfo->umem_area);
			params->queue_data_arr[i].alive = false;
			continue;
		}

		// XSK
		struct xsk_info *xinfo = &params->queue_data_arr[i].xsk_info;
		xinfo->queue_id = params->queue_data_arr[i].eth_qid;
		xinfo->xsk_cfg.rx_size = NUM_FRAMES;
		xinfo->xsk_cfg.tx_size = NUM_FRAMES;
		xinfo->xsk_cfg.bind_flags = XDP_ZEROCOPY;
		// xinfo->xsk_cfg.bind_flags |= XDP_USE_NEED_WAKEUP;
		xinfo->xsk_cfg.libxdp_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;

		strcpy(xinfo->ifname, params->queue_data_arr[i].ifname);
		if((ret = xsk_socket__create(&xinfo->xsk, xinfo->ifname, xinfo->queue_id, uinfo->umem, &xinfo->rx_ring, &xinfo->tx_ring, &xinfo->xsk_cfg))) {
			fprintf(stderr, "Failed to create xsocket for queue %d; ret: %d\n", params->queue_data_arr[i].eth_qid, ret);
			xsk_umem__delete(uinfo->umem);
			free(uinfo->umem_area);
			params->queue_data_arr[i].alive = false;
			continue;
		}

		params->queue_data_arr[i].alive = true;
	}
	return 0;
}

void xdp_mode_generate_destroy(struct ndp_tool_params *p)
{
	list_range_destroy(&p->mode.xdp.generate.range);
}
