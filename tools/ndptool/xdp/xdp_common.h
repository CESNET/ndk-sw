#ifndef XDP_COMMON_H
#define XDP_COMMON_H

// libxdp
#include <xdp/libxdp.h>
#include <xdp/xsk.h>
// IF_NAMESIZE
#include <net/if.h> 

#include "../common.h"

#define NUM_FRAMES 4096

struct xsk_info {
	struct xsk_socket *xsk;
	char ifname[IF_NAMESIZE];
	unsigned queue_id;
	struct xsk_ring_cons rx_ring;
	struct xsk_ring_prod tx_ring;
	struct xsk_socket_config xsk_cfg;
};

struct umem_info {
	struct xsk_umem *umem;
	void *umem_area;
	unsigned long long size;
	struct xsk_ring_prod fill_ring;
    struct xsk_ring_cons comp_ring;
	struct xsk_umem_config umem_cfg;
};

struct ndp_mode_xdp_xsk_data {
	int alive;
	int eth_qid;
	int nfb_qid;
	char ifname[IF_NAMESIZE];
	struct umem_info umem_info;
	struct xsk_info xsk_info;
};

struct addr_stack {
	uint64_t addresses[NUM_FRAMES];
	unsigned addr_cnt;
};

// Get address from stack
static inline uint64_t alloc_addr(struct addr_stack *stack) {
	if (stack->addr_cnt == 0) {
		fprintf(stderr, "BUG out of adresses\n");
		exit(1);
	}
	uint64_t addr = stack->addresses[--stack->addr_cnt];
	stack->addresses[stack->addr_cnt] = 0;
	return addr;
}

// Put adress to stack
static inline void free_addr(struct addr_stack *stack, uint64_t address) {
	if (stack->addr_cnt == NUM_FRAMES) {
		fprintf(stderr, "BUG counting adresses\n");
		exit(1);
	}
	stack->addresses[stack->addr_cnt++] = address; 
}

// Fill the stack with addresses into the umem
static inline void init_addr(struct addr_stack *stack,	unsigned frame_size) {
	for(unsigned i = 0; i < NUM_FRAMES; i++) {
		stack->addresses[i] = i * frame_size;
	}
	stack->addr_cnt = NUM_FRAMES;
}

void xdp_mode_common_parse_queues(struct ndp_tool_params *p);

#endif // XDP_COMMON_H