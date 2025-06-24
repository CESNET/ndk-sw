#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <err.h>
#include <numa.h>

#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include "xdp_common.h"
#include "../pcap.h"

#include <xdp/libxdp.h>
#include <xdp/xsk.h>
#include <net/if.h> // IF_NAMESIZE
#include <linux/types.h>

static int xdp_mode_read_prepare(struct ndp_tool_params *p);
static int xdp_mode_read_loop(struct ndp_tool_params *p);
static int xdp_mode_read_exit(struct ndp_tool_params *p);

int xdp_mode_read(struct ndp_tool_params *p)
{
	int ret;

	p->update_stats = update_stats;

	ret = xdp_mode_read_prepare(p);
	if (ret)
		return ret;
	ret = xdp_mode_read_loop(p);
	p->update_stats(0, 0, &p->si);
	xdp_mode_read_exit(p);
	return ret;
}

int xdp_mode_read_check(struct ndp_tool_params *p) {
	struct ndp_mode_xdp_params *params = &p->mode.xdp;
	
	uint32_t pagesize = getpagesize();
	int ret;
	unsigned i;

	xdp_mode_common_parse_queues(p);

	// Create UMEM and XSK for each queue
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

void *xdp_mode_read_thread(void *tmp)
{
	struct thread_data *thread_data = (struct thread_data *)tmp;
	struct ndp_tool_params *p = &thread_data->params;

	p->update_stats = update_stats_thread;

	thread_data->ret = xdp_mode_read_prepare(p);
	if (thread_data->ret) {
		thread_data->state = TS_FINISHED;
		return NULL;
	}

	thread_data->state = TS_RUNNING;
	thread_data->ret = xdp_mode_read_loop(p);
	p->update_stats(0, 0, &p->si);
	xdp_mode_read_exit(p);
	thread_data->state = TS_FINISHED;

	return NULL;
}

static int xdp_mode_read_prepare(struct ndp_tool_params *p)
{
	p->si.progress_letter = 'R';
	gettimeofday(&p->si.startTime, NULL);
	return 0;
}

static int xdp_mode_read_exit(struct ndp_tool_params *p)
{
	gettimeofday(&p->si.endTime, NULL);
	return 0;
}

static int xdp_mode_read_loop(struct ndp_tool_params *p)
{
	unsigned cnt;
	unsigned burst_size = RX_BURST;

	struct ndp_packet packets[burst_size];
	struct stats_info *si = &p->si;
	update_stats_t update_stats = p->update_stats;
	struct ndp_mode_xdp_params *params = &p->mode.xdp;
	struct ndp_mode_xdp_xsk_data *xsk_data = NULL;
	struct xsk_ring_prod *fill_ring;
	struct xsk_ring_cons *rx_ring;
	struct addr_stack stack;

	// Find socket data and check if socket is alive
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

	// Clear length of packet header
	for (unsigned i = 0; i < burst_size; i++) {
		packets[i].flags = 0;
		packets[i].header_length = 0;
	}

	fill_ring = &xsk_data->umem_info.fill_ring;
	rx_ring = &xsk_data->xsk_info.rx_ring;

	// Fill the adress stack with adresses into the umem
	init_addr(&stack, xsk_data->umem_info.umem_cfg.frame_size);

	// The receive loop
	while (!stop) {
		// Fill RX
		unsigned rx_idx = 0;
		unsigned fill_idx = 0;
		unsigned reservable = xsk_prod_nb_free(fill_ring, stack.addr_cnt);
		reservable = reservable < stack.addr_cnt ? reservable : stack.addr_cnt;
		if (reservable > burst_size) {
			unsigned reserved = xsk_ring_prod__reserve(fill_ring, reservable, &fill_idx);
			for(unsigned i = 0; i < reserved; i++) {
				*xsk_ring_prod__fill_addr(fill_ring, fill_idx++) = alloc_addr(&stack);
			}
			xsk_ring_prod__submit(fill_ring, reserved);
		}

		// Check limits if there is one (0 means loop forever)
		if (p->limit_packets > 0) {
			// Limit reached
			if (si->packet_cnt == p->limit_packets) {
				break;
			}
			// Limit will be reached in one burst
			if (si->packet_cnt + burst_size > p->limit_packets) {
				burst_size = p->limit_packets - si->packet_cnt;
			}
		}
		if (p->limit_bytes > 0 && si->bytes_cnt > p->limit_bytes) {
			break;
		}

		// Receive packets
		cnt = xsk_ring_cons__peek(rx_ring, burst_size, &rx_idx);
		if (cnt == 0) {
			if (p->use_delay_nsec)
				delay_nsecs(1);
			continue;
		}

		// Process packets
		for(unsigned i = 0; i < cnt; i++) {
			struct xdp_desc const *desc = xsk_ring_cons__rx_desc(rx_ring, rx_idx++);	
			packets[i].data = xsk_umem__get_data(xsk_data->umem_info.umem_area, desc->addr);
			packets[i].data_length = desc->len;
			free_addr(&stack, desc->addr);
		}
		update_stats(packets, cnt, si);
		
		// Mark done
		xsk_ring_cons__release(rx_ring, cnt);
	}

	if(xsk_data->alive) {
		xsk_socket__delete(xsk_data->xsk_info.xsk);
		xsk_umem__delete(xsk_data->umem_info.umem);
		free(xsk_data->umem_info.umem_area);
		xsk_data->alive = false;
	}
	return 0;
}
