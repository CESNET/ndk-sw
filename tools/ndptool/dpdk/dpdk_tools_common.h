#ifndef DPDK_TOOLS_COMMON_H
#define DPDK_COMMON_H

// Function calls calloc - free needs to be called on *dpdk_params when the structure is no longer used
// Innitializes array of data for each available queue and zeroes it
static inline int dpdk_queue_data_init(struct ndp_mode_dpdk_params *dpdk_params)
{
	struct ndp_mode_dpdk_queue_data *queue_data_arr;
	unsigned idx = 0;
	unsigned queue_id;
	unsigned port_id;
	unsigned queue_count = dpdk_params->queues_available;
	struct rte_eth_dev_info dev_info;
	int ret;

	queue_data_arr = calloc(sizeof(struct ndp_mode_dpdk_queue_data), queue_count);
	if (queue_data_arr == NULL)
		return -ENOMEM;

	dpdk_params->queue_data_arr = queue_data_arr;
	
	RTE_ETH_FOREACH_DEV(port_id) {
		ret = rte_eth_dev_info_get(port_id, &dev_info);
		if (ret) {
			fprintf(stderr, "rte_eth_dev_info_get() failed: %d\n", ret);
			return ret;
		}

		for (queue_id = 0; queue_id < dev_info.max_rx_queues; queue_id++, idx++) {
			queue_data_arr[idx].port_id = port_id;
			queue_data_arr->queue_id = queue_id;
		}
	}

	return 0;
}

// gets device path pointer into char *dev_path
static inline int dpdk_get_dev_path(struct ndp_tool_params *p, const char **dev_path)
{
	int len;
	int fdt_offset;
	const void *fdt;
	const void *prop;

	p->dev = nfb_open(p->nfb_path);
	if (p->dev == NULL){
		fprintf(stderr, "nfb_open() failed for path: %s\n", p->nfb_path);
		return -ENODEV;;
	}

	fdt = nfb_get_fdt(p->dev);
	fdt_offset = fdt_path_offset(fdt, "/system/device/endpoint0");
	prop = fdt_getprop(fdt, fdt_offset, "pci-slot", &len);
	if (len < 0) {
		fprintf(stderr, "fdt error\n");
		nfb_close(p->dev);
		return -EINVAL;
	}
	*dev_path = (char *) prop;
	nfb_close(p->dev);
	return 0;
}

static inline int dpdk_get_queues_available(unsigned int *queues_available)
{
	int port_id;
	*queues_available = 0;
	struct rte_eth_dev_info dev_info;
	int ret;

	RTE_ETH_FOREACH_DEV(port_id) {
		if (!rte_eth_dev_is_valid_port(port_id)) {
			continue;	
		}
		ret = rte_eth_dev_info_get(port_id, &dev_info);
		if (ret) {
			fprintf(stderr, "rte_eth_dev_info_get() failed: %d\n", ret);
			return ret;
		}
		*queues_available += dev_info.max_rx_queues;
	}

	return 0;
}

#define CL_DUMP_CHARS_PER_LINE 32
#define CL_DUMP_CHARS_PER_WORD 8
// copied print packet method to go around the issue with freeing mbufs before printing them
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
#endif // DPDK_TOOLS_COMMON_H