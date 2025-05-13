/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Calypte queue - specific functions for this type of queue
 *
 * Copyright (C) 2023-2023 CESNET
 * Author(s):
 *   Vladislav Valek <valekv@cesnet.cz>
 */

// Because the data in Calypte TX queue are written directly to the FPGA, the reference to the buffers is
// opened using this function.
static inline int ndp_queue_calypte_open_buffers(struct nfb_device *dev, struct nc_ndp_queue *q, const void *fdt, int fdt_offset)
{
	int ret = -EBADFD;
	int ctrl_node_offset = -1;
	int buffer_offset = -1;

	// Applies only for Calypte TX queue
	if (!(q->protocol == 3 && q->channel.type == NDP_CHANNEL_TYPE_TX))
		return 0;

	q->u.v3.tx_pkts_cnt = 256;

	ctrl_node_offset = fdt_node_offset_by_phandle_ref(fdt, fdt_offset, "ctrl");
	if (ctrl_node_offset < 0)
		return -EBADFD;

	buffer_offset = fdt_node_offset_by_phandle_ref(fdt, ctrl_node_offset, "data_buff");
	if (buffer_offset < 0)
		return -EBADFD;

	q->u.v3.tx_data_buff = nfb_comp_open(dev, buffer_offset);
	if (q->u.v3.tx_data_buff == NULL) {
		return -EBADFD;
	}

	buffer_offset = fdt_node_offset_by_phandle_ref(fdt, ctrl_node_offset, "hdr_buff");
	if (buffer_offset < 0)
		goto err_hdr_buff_find_tx;

	q->u.v3.tx_hdr_buff = nfb_comp_open(dev, buffer_offset);
	if (q->u.v3.tx_hdr_buff == NULL) {
		goto err_hdr_buff_open_tx;
	}

#ifdef __KERNEL__
	q->u.v3.tx_pkts = kzalloc(sizeof(void*) * q->u.v3.tx_pkts_cnt, GFP_KERNEL);
#else
	q->u.v3.tx_pkts = malloc(sizeof(void*) * q->u.v3.tx_pkts_cnt);
#endif
	if (q->u.v3.tx_pkts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_tx_pkts;
	}

	return 0;

err_alloc_tx_pkts:
	nfb_comp_close(q->u.v3.tx_hdr_buff);
err_hdr_buff_open_tx:
err_hdr_buff_find_tx:
	nfb_comp_close(q->u.v3.tx_data_buff);
	return ret;
}

static inline void ndp_queue_calypte_close_buffers(struct nc_ndp_queue *q)
{
	if (!(q->protocol == 3 && q->channel.type == NDP_CHANNEL_TYPE_TX))
		return;

	nfb_comp_close(q->u.v3.tx_data_buff);
	nfb_comp_close(q->u.v3.tx_hdr_buff);
	q->u.v3.tx_data_buff = NULL;
	q->u.v3.tx_hdr_buff = NULL;
#ifdef __KERNEL__
	kfree(q->u.v3.tx_pkts);
#else
	free(q->u.v3.tx_pkts);
#endif
}
