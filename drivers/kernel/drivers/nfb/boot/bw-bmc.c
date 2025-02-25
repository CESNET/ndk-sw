/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver module for BittWare BMC
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/sort.h>

#include "../nfb.h"
#include "../pci.h"

#include "boot.h"

#include <linux/nfb/boot.h>

#include <netcope/bittware_bmc_spi.h>

extern bool flash_recovery_ro;

struct bw_file_entry {
	int id;
	unsigned offset;
	int size;
	int priority;
	const char *name;

	const char *_title;
	int _empty;
};

static int bw_file_entry_sort_by_offset(const void *lhs, const void *rhs)
{
	const struct bw_file_entry *l = lhs;
	const struct bw_file_entry *r = rhs;

	if (l->offset < r->offset)
		return -1;
	if (l->offset > r->offset)
		return 1;
	return 0;
}

int nfb_boot_bw_bmc_parse_partition(struct bw_file_entry *p, char **buffer)
{
	const char *bbeg;
	char *bend;

	char name_prefix[] = "0x00000000-";

	bbeg = *buffer;

	/* parse id */
	if (sscanf(bbeg, "%d,", &p->id) != 1)
		return 1;

	bend = strstr(bbeg, ",");
	if (bend == NULL)
		return 2;

	/* parse name */
	bbeg = bend + 1;
	bend = strstr(bbeg, ",");
	if (bend == NULL)
		return 3;


	bend[0] = 0;
	p->name = bbeg;


	/* parse offset */
	bbeg = bend + 1;
	if (sscanf(bbeg, "%x,", &p->offset) != 1)
		return 4;

	bend = strstr(bbeg, ",");
	if (bend == NULL)
		return 5;

	/* parse size */
	bbeg = bend + 1;
	if (sscanf(bbeg, "%d,", &p->size) != 1)
		return 6;

	bend = strstr(bbeg, ",");
	if (bend == NULL)
		return 7;

	/* parse priority */
	bbeg = bend + 1;
	if (sscanf(bbeg, "%d,", &p->priority) != 1)
		return 8;

	bend = strstr(bbeg, "\n");
	if (bend == NULL)
		return 9;

	bend += 1;

	p->_title = p->name;

	snprintf(name_prefix, sizeof(name_prefix), "0x%08x-", p->offset);
	if (strncmp(p->name, name_prefix, strlen(name_prefix)) == 0) {
		p->_title += strlen(name_prefix);
	}

	*buffer = bend;
	return 0;
}

static inline void nfb_boot_bw_bmc_add_partition(void *fdt, int node, int *id, struct bw_file_entry *p, unsigned *flag)
{
	int tmp;
	char node_name[32];

	int i = *id;

	if (p->_empty) {
		sprintf(node_name, "empty%d", i);
		*id = i + 1;
	} else {
		if (p->offset == 0) {
			i = 1;
			*flag |= 1;
		} else if ((*flag & 2) == 0) {
			i = 0;
			*flag |= 2;
		} else {
			*id = i + 1;
		}

		sprintf(node_name, "image%d", i);
	}

	tmp = nfb_fdt_create_binary_slot(fdt, node, node_name, p->_title, i, p->_empty ? -1 : i, -1, p->offset, p->size);
	if (p->_empty) {
		fdt_setprop(fdt, tmp, "empty", NULL, 0);
	} else {
		fdt_setprop_u32(fdt, tmp, "priority", p->priority);
		fdt_setprop_string(fdt, tmp, "filename", p->name);
	}

	if (i == 1 && flash_recovery_ro) {
		tmp = fdt_subnode_offset(fdt, tmp, "control-param");
		fdt_setprop(fdt, tmp, "ro", NULL, 0);
	}
}

int nfb_boot_bw_bmc_load_partition_table(struct nfb_boot *boot)
{
	static const int buffer_size = 16384;

	void *fdt;
	char *buffer;
	char *bbeg;

	/* ID 1 is reserved for image at offset 0x0: power-on (RO)
	 * ID 0 is for first image after ID 1 */
	int id = 2;
	int i;
	int ret;
	int node;
	unsigned max_size = 0, add_flag = 0;

	int p_cnt = 0;
	struct bw_file_entry *p = NULL, *p_buffer = NULL;
	struct bw_file_entry pe;

	const char * fw_table = "/fpga/table.csv";
	const char *card_name;

	fdt = nfb_get_fdt(boot->nfb);
	node = fdt_path_offset(fdt, "/firmware");
	card_name = fdt_getprop(fdt, node, "card-name", NULL);

	if (card_name == NULL) {
		return -ENODEV;
	} else if (strcmp(card_name, "IA-440I") == 0) {
		max_size = 0x10000000;
	} else {
		return -ENODEV;
	}

	node = nfb_comp_find(boot->nfb, "bittware,bmc", 0);
	if (node < 0)
		return -ENODEV;

	node = fdt_add_subnode(fdt, node, "images");

	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	/* Download Flash partition table */
	ret = nc_bw_bmc_download_file(boot->bw_bmc, fw_table, buffer, buffer_size);
	if (ret < 0) {
		ret = -EPIPE;
		goto err_dl_file;
	} else if ((unsigned)ret >= buffer_size) {
		ret = -ENOMEM;
		goto err_dl_file_small;
	}
	buffer[ret] = 0;

	pe.name = pe._title = "<empty>";
	pe.offset = 0;
	pe._empty = 1;

	bbeg = buffer;
	do {
		p = p_buffer;
		p_buffer = krealloc(p_buffer, (p_cnt + 1) * sizeof(*p), GFP_KERNEL);
		if (p_buffer == NULL) {
			kfree(p);
			ret = -ENOMEM;
			goto err_parse;
		}

		p = p_buffer + p_cnt;
		p->_empty = 0;
		ret = nfb_boot_bw_bmc_parse_partition(p, &bbeg);
		if (ret == 0) {
			p_cnt++;

		}
	} while (ret == 0);

	sort(p_buffer, p_cnt, sizeof(*p), &bw_file_entry_sort_by_offset, NULL);
	for (i = 0; i < p_cnt; i++) {
		p = p_buffer + i;

		if (pe.offset < p->offset) {
			/* TODO: align offset */
			pe.size = p->offset - pe.offset;
			nfb_boot_bw_bmc_add_partition(fdt, node, &id, &pe, &add_flag);
		}

		nfb_boot_bw_bmc_add_partition(fdt, node, &id, p, &add_flag);

		pe.offset = p->offset + p->size;
		pe.offset = (pe.offset + 0xFFF) & ~0xFFF;
	}

	if (pe.offset < max_size) {
		pe.size = max_size - pe.offset;
		nfb_boot_bw_bmc_add_partition(fdt, node, &id, &pe, &add_flag);
	}
	kfree(buffer);
	return 0;

err_parse:
err_dl_file_small:
err_dl_file:
	kfree(buffer);
	return ret;
}

int nfb_boot_bw_bmc_update_binary_slots(struct nfb_boot *boot)
{
	void *fdt;
	int node;

	fdt = nfb_get_fdt(boot->nfb);
	node = nfb_comp_find(boot->nfb, "bittware,bmc", 0);
	node = fdt_subnode_offset(fdt, node, "images");
	if (node >= 0)
		fdt_del_node(fdt, node);

	nfb_boot_bw_bmc_load_partition_table(boot);
	return 0;
}

int nfb_boot_bw_bmc_attach(struct nfb_boot* boot)
{
	int node = -1;

	boot->bw_bmc = NULL;

	node = nfb_comp_find(boot->nfb, "bittware,bmc", 0);
	if (node < 0)
		return -ENODEV;

	boot->bw_bmc = nc_bw_bmc_open(boot->nfb, node, NULL, 2048);
	if (boot->bw_bmc == NULL)
		return -ENODEV;

	nfb_boot_bw_bmc_load_partition_table(boot);

	return 0;
}

void nfb_boot_bw_bmc_detach(struct nfb_boot* boot)
{
	if (boot->bw_bmc) {
		nc_bw_bmc_close(boot->bw_bmc);
		boot->bw_bmc = NULL;
	}
}

void nfb_boot_bw_bmc_load_cb(void *priv, unsigned offset)
{
	struct nfb_boot* boot = priv;
	boot->load.current_op_progress = offset;

	if ((offset & 0x1FFFF) == 0)
		cond_resched();
}

int nfb_boot_bw_bmc_set_priority(struct nfb_boot *boot, struct nfb_boot_ioc_load *load)
{
	int ret = 0;
	int i;
	int node;
	int proplen;
	const uint32_t *prop32;
	uint64_t id, prio_pid, prio_val;
	size_t prio_size = 0, prio_add;
	unsigned char *prio_data = NULL, *prio_data_tmp;
	const size_t sz_prio_item = sizeof(prio_pid) + sizeof(prio_val);
	const char *prop;

	void *fdt = nfb_get_fdt(boot->nfb);

	if (load->data_size % sz_prio_item)
		return -EINVAL;

	fdt_for_each_compatible_node(fdt, node, "netcope,binary_slot") {
		prop32 = fdt_getprop(fdt, node, "id", &proplen);
		if (proplen != sizeof(*prop32))
			continue;
		id = fdt32_to_cpu(*prop32);

		for (i = 0; i < load->data_size / sz_prio_item; i++) {
			prio_pid = *(uint64_t*) (load->data + i * sz_prio_item);
			prio_val = *(uint64_t*) (load->data + i * sz_prio_item + sizeof(prio_pid));
			if (id == prio_pid) {
				prop = fdt_getprop(fdt, node, "filename", &proplen);
				if (prop == NULL) {
					ret = -ENODEV;
					break;
				}
				prio_add = strlen(prop) + 1 + 1 + 1;
				prio_data_tmp = prio_data;
				prio_data = krealloc(prio_data, prio_size + prio_add, GFP_KERNEL);
				if (prio_data == NULL) {
					kfree(prio_data_tmp);
					ret = -ENOMEM;
					break;
				}

				strcpy(prio_data + prio_size, prop);
				prio_data[prio_size + prio_add - 2] = (uint8_t) prio_val;
				prio_data[prio_size + prio_add - 1] = '\n';
				prio_size += prio_add;
			}
		}
		if (ret)
			break;
	}
	if (ret) {
		if (prio_data)
			kfree(prio_data);
		return ret;
	}

	prio_add = 1 + 1 + 1;
	prio_data_tmp = prio_data;
	prio_data = krealloc(prio_data, prio_size + prio_add, GFP_KERNEL);
	if (prio_data == NULL) {
		kfree(prio_data_tmp);
		return -ENOMEM;
	}

	prio_data[prio_size+prio_add-3] = '\0';
	prio_data[prio_size+prio_add-2] = (uint8_t) 255;
	prio_data[prio_size+prio_add-1] = '\n';
	prio_size += prio_add;

	ret = nc_bw_bmc_file_upload(boot->bw_bmc, "/fpga/priority.txt", prio_data, prio_size);
	kfree(prio_data);

	return ret;
}

int nfb_boot_bw_bmc_load(struct nfb_boot *boot,
		struct nfb_boot_ioc_load *load/*,
		struct nfb_boot_app_priv * app_priv*/)
{
	#define NFB_BOOT_BW_BMC_PATH_PREFIX "/fpga/"

	char load_path[] = NFB_BOOT_BW_BMC_PATH_PREFIX "00000000";

	int ret = 0;
	char * buf;
	int node_image, node_cp, node_parent, node_next, node_next_cp;

	uint32_t offset, offset_next, size, size_next;
	void *fdt;
	const void *prop;

	unsigned buf_size;
	const char *name;

	int update_nodes = 0;

	if (load->cmd == NFB_BOOT_IOC_LOAD_CMD_PRIORITY) {
		ret = nfb_boot_bw_bmc_set_priority(boot, load);
		if (ret)
			return ret;
		nfb_boot_bw_bmc_update_binary_slots(boot);
		return 0;
	}

	fdt = nfb_get_fdt(boot->nfb);
	node_image = fdt_path_offset(fdt, load->node);
	node_parent = fdt_parent_offset(fdt, node_image);

	node_cp = fdt_subnode_offset(fdt, node_image, "control-param");
	ret = fdt_getprop32(fdt, node_cp, "base", &offset);
	if (ret)
		return -EINVAL;
	ret = fdt_getprop32(fdt, node_cp, "size", &size);
	if (ret)
		return -EINVAL;

	fdt_for_each_subnode(node_next, fdt, node_parent) {
		if (node_next == node_image)
			continue;

		node_next_cp = fdt_subnode_offset(fdt, node_next, "control-param");

		ret = fdt_getprop32(fdt, node_next_cp, "base", &offset_next);
		if (ret)
			return -EINVAL;
		ret = fdt_getprop32(fdt, node_next_cp, "size", &size_next);
		if (ret)
			return -EINVAL;
		prop = fdt_getprop(fdt, node_next, "empty", NULL);

		if (((offset + size + 0xFFF) & ~0xFFF) == offset_next && prop != NULL) {
			/* Next image is empty, its space can be used as well */
			size += size_next;
			break;
		}
	}

	if (size < load->data_size)
		return -ENOMEM;

	prop = fdt_getprop(fdt, node_image, "empty", NULL);

	boot->load.start_ops = load->cmd;
	boot->load.pending_ops = load->cmd;

	/* execute requested commands */
	if (load->cmd & NFB_BOOT_IOC_LOAD_CMD_ERASE) {
		name = fdt_getprop(fdt, node_image, "filename", NULL);
		/* a) Can't erase already empty partition.
		 * b) If isn't empty, the filename must be set. */
		if (prop != NULL || name == NULL) {
			ret = -EINVAL;
			goto err_cmd;
		}

		boot->load.current_op = NFB_BOOT_IOC_LOAD_CMD_ERASE;

		boot->load.current_op_progress_max = 1;
		boot->load.current_op_progress = 0;

		buf_size = strlen(NFB_BOOT_BW_BMC_PATH_PREFIX) + strlen(name) + 1;
		buf = kzalloc(buf_size, GFP_KERNEL);
		if (buf == NULL) {
			ret = -ENOMEM;
			goto err_cmd;
		}

		snprintf(buf, buf_size, NFB_BOOT_BW_BMC_PATH_PREFIX "%s", name);

		ret = nc_bw_bmc_file_unlink(boot->bw_bmc, buf);
		kfree(buf);
		if (ret)
			goto err_cmd;

		update_nodes |= NFB_BOOT_IOC_LOAD_CMD_ERASE;
		boot->load.current_op_progress = 1;
	} else if (prop == NULL) {
		/* This slot is not empty, must be erased! */
		ret = -EINVAL;
		goto err_cmd;
	}

	if (load->cmd & NFB_BOOT_IOC_LOAD_CMD_WRITE) {
		boot->load.current_op = NFB_BOOT_IOC_LOAD_CMD_WRITE;

		boot->load.current_op_progress_max = load->data_size;
		ret = nc_bw_bmc_fpga_load_ext(boot->bw_bmc, load->data, load->data_size, offset, nfb_boot_bw_bmc_load_cb, boot);
		if (ret)
			goto err_cmd;

		buf_size = strlen(NFB_BOOT_BW_BMC_PATH_PREFIX "0x00000000-") + load->name_size;
		buf = kzalloc(buf_size, GFP_KERNEL);
		if (buf == NULL) {
			ret = -ENOMEM;
			goto err_cmd;
		}
		snprintf(load_path, sizeof(load_path), NFB_BOOT_BW_BMC_PATH_PREFIX "%08x", offset);
		snprintf(buf, buf_size, NFB_BOOT_BW_BMC_PATH_PREFIX "0x%08x-%s", offset, load->name);

		ret = nc_bw_bmc_file_move(boot->bw_bmc, load_path, buf);
		kfree(buf);
		if (ret)
			goto err_cmd;

		update_nodes |= NFB_BOOT_IOC_LOAD_CMD_WRITE;
	}

	boot->load.current_op = NFB_BOOT_IOC_LOAD_CMD_NONE;

	if (update_nodes)
		nfb_boot_bw_bmc_update_binary_slots(boot);

	return 0;

err_cmd:
	boot->load.current_op = NFB_BOOT_IOC_LOAD_CMD_NONE;
	if (update_nodes)
		nfb_boot_bw_bmc_update_binary_slots(boot);
	return ret;
}

int nfb_boot_bw_bmc_reload(struct nfb_boot *boot)
{
	int node;

	int proplen;
	const fdt32_t *prop32;
	const void *filename = NULL;

	fdt_for_each_compatible_node(boot->nfb->fdt, node, "netcope,binary_slot") {
		prop32 = fdt_getprop(boot->nfb->fdt, node, "boot_id", &proplen);
		if (proplen != sizeof(*prop32))
			continue;

		if (fdt32_to_cpu(*prop32) == boot->num_image) {
			filename = fdt_getprop(boot->nfb->fdt, node, "filename", &proplen);
			break;
		}
	}

	if (filename == NULL)
		return -ENODEV;

	return nc_bw_bmc_send_reload(boot->bw_bmc, filename);
}
