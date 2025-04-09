#include <stdlib.h>
#include <stdio.h>

#include <nfb/nfb.h>

#include "xdp_common.h"

#define PATH_BUFF 128

static void xdp_mode_common_sysfs_int(const char *sysfs_path, const char *postfix, int *ret) {
	FILE *file;
	char path[PATH_BUFF];
	int err;
	err = snprintf(path, sizeof(path), "%s%s", sysfs_path, postfix);
	if (err < 0 || err >= PATH_BUFF) {
		fprintf(stderr,"Failed to parse sysfs path\n");
		exit(-1);
	}
	if (!(file = fopen(path , "r"))) {
		fprintf(stderr,"Failed to open %s; Is the XDP driver loaded?\n", path);
		exit(-1);
	}
	err = fscanf(file, "%d", ret);
	if(err == EOF) {
		fprintf(stderr,"Failed to read %s\n", path);
		fclose(file);
		exit(-1);
	}
	fclose(file);
}

static void xdp_mode_common_sysfs_ifname(const char *sysfs_path, const char *postfix, char *ret) {
	FILE *file;
	char path[PATH_BUFF];
	int err;
	err = snprintf(path, sizeof(path), "%s%s", sysfs_path, postfix);
	if (err < 0 || err >= PATH_BUFF) {
		fprintf(stderr,"Failed to parse sysfs path\n");
		exit(-1);
	}
	if (!(file = fopen(path , "r"))) {
		fprintf(stderr,"Failed to open %s; Is the XDP driver loaded?\n", path);
		exit(-1);
	}
	err = fscanf(file, "%s", ret);
	if(err == EOF) {
		fprintf(stderr,"Failed to read %s\n", path);
		fclose(file);
		exit(-1);
	}
	fclose(file);
}

void xdp_mode_common_parse_queues(struct ndp_tool_params *p) {
	struct nfb_device *nfb;
	struct ndp_mode_xdp_params *params = &p->mode.xdp;
	struct list_range *queue_range = &params->queue_range;
	int queue_count;
	int ret, nfb_system_id, channel_total;
	char sysfs_path[PATH_BUFF];
	
	// get nfb system id
	if(!(nfb = nfb_open(p->nfb_path))) {
		fprintf(stderr,"Failed to open nfb\n");
		exit(-1);
	}
	nfb_system_id = nfb_get_system_id(nfb);
	nfb_close(nfb);

	// read xdp module sysfs
	ret = snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/nfb/nfb%d/nfb_xdp", nfb_system_id);
	if (ret < 0 || ret >= PATH_BUFF) {
		fprintf(stderr,"Failed to get sysfs path\n");
		exit(-1);
	}
	xdp_mode_common_sysfs_int(sysfs_path, "/channel_total", &channel_total);

	// Open all queues if no range provided
	if(!(queue_count = list_range_count(queue_range))) {
		queue_count = channel_total;
		list_range_add_range(queue_range, 0, channel_total);
	}

	// Allocate the channel data structs
	if(!(params->queue_data_arr = calloc(queue_count, sizeof(struct ndp_mode_xdp_xsk_data)))) {
		fprintf(stderr,"Failed to alloc xdp xsk_data\n");
		exit(-1);
	}

	// read channel sysfs
	int data_idx = 0;
	for (int ch_idx = 0; ch_idx < channel_total; ch_idx++) {
		// Skip unwanted channels
		if (!list_range_contains(queue_range, ch_idx))
			continue;

		// Parse the path
		ret = snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/nfb/nfb%d/nfb_xdp/channel%d",
			 nfb_system_id, ch_idx);
		if (ret < 0 || ret >= PATH_BUFF) {
			fprintf(stderr,"Failed to parse sysfs path for channel %d\n", ch_idx);
			exit(-1);
		}

		// Fill the structs
		params->queue_data_arr[data_idx].nfb_qid = ch_idx;
		xdp_mode_common_sysfs_int(sysfs_path, "/index", &params->queue_data_arr[data_idx].eth_qid);
		xdp_mode_common_sysfs_ifname(sysfs_path, "/ifname", params->queue_data_arr[data_idx].ifname);
		xdp_mode_common_sysfs_int(sysfs_path, "/status", &params->queue_data_arr[data_idx].alive);
		if(!params->queue_data_arr[data_idx].alive)
			fprintf(stderr, "The queue %d is not open. Is there an XDP netdevice coresponding to this queue? nfb-dma can be used to manage netdevices.\n", ch_idx);

		data_idx++;
	}

	params->socket_cnt = queue_count;
	if(!params->socket_cnt) {
		fprintf(stderr,"No queues found\n");
		exit(-1);
	}
}