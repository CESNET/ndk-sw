/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * MI bus driver of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include <libfdt.h>

#include "../nfb.h"
#include "mi.h"

#include <netcope/mi.h>


static bool mi_debug = 0;

static ssize_t nfb_bus_mi_read(struct nfb_bus *bus, void *buf, size_t nbyte, off_t offset)
{
	struct nfb_mi_node *mi_node = bus->priv;
	bool wc_used = false;
	if (mi_node->mem_virt == NULL)
		return -EBADF;

	return nfb_bus_mi_memcopy_noopt(buf, mi_node->mem_virt + offset, nbyte, offset, &wc_used);
}

static ssize_t nfb_bus_mi_write(struct nfb_bus *bus, const void *buf, size_t nbyte, off_t offset)
{
	struct nfb_mi_node *mi_node = bus->priv;
	bool wc_used = false;
	if (mi_node->mem_virt == NULL)
		return -EBADF;

	return nfb_bus_mi_memcopy_noopt(mi_node->mem_virt + offset, buf, nbyte, offset, &wc_used);
}

static int nfb_mi_mmap(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv)
{
	struct nfb_mi* mi = (struct nfb_mi*) priv;
	struct nfb_mi_node *mi_node;
	//struct nfb_app *app = (struct nfb_app*) vma->vm_private_data;

	list_for_each_entry(mi_node, &mi->node_list, nfb_mi_list) {
		if (mi_node->mmap_offset <= offset && mi_node->mmap_offset + mi_node->mem_len >= offset + size) {
#ifdef pgprot_noncached
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
#ifdef CONFIG_HAVE_VM_FLAGS_SET
			vm_flags_set(vma, VM_IO);
#else
			vma->vm_flags |= VM_IO;
#endif
			return io_remap_pfn_range(vma, vma->vm_start,
					(mi_node->mem_phys + (offset - mi_node->mmap_offset)) >> PAGE_SHIFT,
					size, vma->vm_page_prot);
		}
	}

	return -ENOENT;
}

static int nfb_mi_map(struct nfb_device *nfb, struct nfb_mi_node *mi_node, struct nfb_pci_device *pci_device)
{
	int ret;
	char nodename[64];
	int node_offset;

	/* Map PCI memory region */
	mi_node->mem_phys = pci_resource_start(pci_device->pci, mi_node->bar);
	mi_node->mem_len = pci_resource_len(pci_device->pci, mi_node->bar);

	if (request_mem_region(mi_node->mem_phys, mi_node->mem_len, "nfb") == NULL) {
		dev_err(&nfb->pci->dev, "unable to grab memory region 0x%llx-0x%llx\n",
			(u64)mi_node->mem_phys, (u64)(mi_node->mem_phys + mi_node->mem_len - 1));
		goto err_request_mem_region;
	}

	/* if (mi_node->is_wc_mapped) */
	/* 	mi_node->mem_virt = ioremap_wc(mi_node->mem_phys, mi_node->mem_len); */
	/* else */
	mi_node->mem_virt = ioremap(mi_node->mem_phys, mi_node->mem_len);

	if (mi_node->mem_virt == NULL) {
		dev_err(&nfb->pci->dev, "unable to remap memory region 0x%llx-0x%llx\n",
			(u64)mi_node->mem_phys, (u64)(mi_node->mem_phys + mi_node->mem_len - 1));
		goto err_ioremap;
	}

	ret = nfb_char_register_mmap(nfb, mi_node->mem_len, &mi_node->mmap_offset, nfb_mi_mmap, mi_node->mi);
	if (ret) {
		goto err_nfb_char_register_mmap;
	}

	/* update Device tree */
	if (mi_debug) {
		node_offset = fdt_path_offset(nfb->fdt, mi_node->bus.path);
		fdt_setprop_u64(nfb->fdt, node_offset, "reg", mi_node->mem_len);
	}

	snprintf(nodename, sizeof(nodename), "/drivers/mi/PCI%d,BAR%d", mi_node->pci_index, mi_node->bar);
	node_offset = fdt_path_offset(nfb->fdt, nodename);

	fdt_setprop_u64(nfb->fdt, node_offset, "mmap_base", mi_node->mmap_offset);
	fdt_setprop_u64(nfb->fdt, node_offset, "mmap_size", mi_node->mem_len);

	return 0;

err_nfb_char_register_mmap:
	iounmap(mi_node->mem_virt);
	mi_node->mem_virt = NULL;
err_ioremap:
	release_mem_region(mi_node->mem_phys, mi_node->mem_len);
err_request_mem_region:
	return -EBADF;
}

static void nfb_mi_unmap(struct nfb_device *nfb, struct nfb_mi_node *mi)
{
	char nodename[64];
	int node_offset;

	if (mi->mem_virt) {
		nfb_char_unregister_mmap(nfb, mi->mmap_offset);

		iounmap(mi->mem_virt);
		release_mem_region(mi->mem_phys, mi->mem_len);
		mi->mem_virt = 0;

		snprintf(nodename, sizeof(nodename), "/drivers/mi/PCI%d,BAR%d", mi->pci_index, mi->bar);
		node_offset = fdt_path_offset(nfb->fdt, nodename);

		fdt_setprop_u64(nfb->fdt, node_offset, "mmap_base", 0);
		fdt_setprop_u64(nfb->fdt, node_offset, "mmap_size", 0);
	}
}

static int nfb_mi_attach_bus(struct nfb_device *nfb, void *priv, int node_offset)
{
	int ret;
	int mapped = 0;
	struct nfb_mi *mi = priv;
	struct nfb_mi_node *mi_node;
	struct nfb_pci_device *pci_device;

	int pci_index, bar;

	const void *prop;
	int proplen;
	char nodename[16];

	prop = fdt_getprop(nfb->fdt, node_offset, "resource", NULL);
	if (prop == NULL)
		return -EINVAL;

	ret = sscanf(prop, "PCI%d,BAR%d", &pci_index, &bar);
	if (ret != 2)
		return -EINVAL;

	ret = -ENODEV;

	mi_node = kzalloc(sizeof(*mi_node), GFP_KERNEL);
	if (!mi_node)
		return -ENOMEM;

	INIT_LIST_HEAD(&mi_node->nfb_mi_list);
	mi_node->mi = mi;
	mi_node->bar = bar;
	mi_node->pci_index = pci_index;
	prop = fdt_getprop(nfb->fdt, node_offset, "map-as-wc", &proplen);
	mi_node->is_wc_mapped = prop && proplen == 0;

	/* Init bus structure and fill DT path */
	INIT_LIST_HEAD(&mi_node->bus.bus_list);
	mi_node->bus.priv = mi_node;
	mi_node->bus.read = nfb_bus_mi_read;
	mi_node->bus.write = nfb_bus_mi_write;

	if (fdt_get_path(nfb->fdt, node_offset, mi_node->bus.path, sizeof(mi_node->bus.path)) < 0)
		goto err_fdt_get_path;

	if (mi_debug)
		fdt_setprop_u64(nfb->fdt, node_offset, "reg", 0);

	snprintf(nodename, sizeof(nodename), "PCI%d,BAR%d", pci_index, bar);

	node_offset = fdt_path_offset(nfb->fdt, "/drivers/mi");
	node_offset = fdt_add_subnode(nfb->fdt, node_offset, nodename);
	fdt_setprop_u64(nfb->fdt, node_offset, "mmap_base", 0);
	fdt_setprop_u64(nfb->fdt, node_offset, "mmap_size", 0);

	list_add(&mi_node->nfb_mi_list, &mi->node_list);

	nfb_bus_register(nfb, &mi_node->bus);

	/* Find corresponding PCI device in list */
	list_for_each_entry(pci_device, &nfb->pci_devices, pci_device_list) {
		if (pci_device->index == mi_node->pci_index) {
			mapped = nfb_mi_map(nfb, mi_node, pci_device);
			mapped = mapped == 0 ? 1 : -1;
			break;
		}
	}

	dev_info(&nfb->pci->dev, "nfb_mi: MI%d on PCI%d map: %s\n", bar, pci_index,
			(mapped == 1 ? "successfull" : (mapped == -1 ? "failed" : "postponed")));

	return ret;

err_fdt_get_path:
	kfree(mi_node);
	return ret;
}

static int nfb_mi_attach_node(struct nfb_device *nfb, void *priv, int base_offset)
{
	int base_depth;
	int supernode_offset;
	int node_offset = -1;

	char path[MAX_FDT_PATH_LENGTH];
	char base_path[MAX_FDT_PATH_LENGTH];

	base_depth = fdt_node_depth(nfb->fdt, base_offset);
	if (base_depth < 0)
		return -EINVAL;

	if (fdt_get_path(nfb->fdt, base_offset, base_path, MAX_FDT_PATH_LENGTH) < 0)
		return -EINVAL;

	while ((node_offset = fdt_node_offset_by_compatible(nfb->fdt, node_offset, "netcope,bus,mi")) >= 0) {
		supernode_offset = fdt_supernode_atdepth_offset(nfb->fdt, node_offset, base_depth, NULL);
		/* Check if this node_offset is successor of base_offset */
		if (supernode_offset != base_offset)
			continue;

		/* Store path because nfb_mi_attach_bus edits FDT */
		if (fdt_get_path(nfb->fdt, node_offset, path, MAX_FDT_PATH_LENGTH) < 0)
			continue;

		nfb_mi_attach_bus(nfb, priv, node_offset);

		/* FDT was edited, restore node_offset from path */
		node_offset = fdt_path_offset(nfb->fdt, path);
		base_offset = fdt_path_offset(nfb->fdt, base_path);
	}
	return 0;
}

void nfb_mi_probe_endpoint(void *priv, struct nfb_pci_device *pci_device)
{
	struct nfb_mi *nfb_mi = priv;
	struct nfb_mi_node *mi;

	list_for_each_entry(mi, &nfb_mi->node_list, nfb_mi_list) {
		if (mi->pci_index == pci_device->index) {
			nfb_mi_map(nfb_mi->nfb, mi, pci_device);
		}
	}
}

void nfb_mi_remove_endpoint(void *priv, struct nfb_pci_device *pci_device)
{
	struct nfb_mi *nfb_mi = priv;
	struct nfb_mi_node *mi;

	list_for_each_entry(mi, &nfb_mi->node_list, nfb_mi_list) {
		if (mi->pci_index == pci_device->index) {
			nfb_mi_unmap(nfb_mi->nfb, mi);
		}
	}
}

int nfb_mi_attach(struct nfb_device *nfb, void **priv)
{
	struct nfb_mi *mi;
	int node_offset = -1;

	mi = kzalloc(sizeof(*mi), GFP_KERNEL);
	if (!mi)
		return -ENOMEM;

	INIT_LIST_HEAD(&mi->node_list);
	mi->nfb = nfb;

	*priv = mi;

	node_offset = fdt_path_offset(nfb->fdt, "/drivers");
	node_offset = fdt_add_subnode(nfb->fdt, node_offset, "mi");

	node_offset = fdt_path_offset(nfb->fdt, "/firmware");
	nfb_mi_attach_node(nfb, mi, node_offset);

	return 0;
}

static void nfb_mi_detach_bus(struct nfb_device *nfb, struct nfb_mi* nfb_mi, int node_offset)
{
	int ret;
	int pci_index, bar;

	const void *prop;
	struct nfb_mi_node *mi;

	prop = fdt_getprop(nfb->fdt, node_offset, "resource", NULL);
	if (prop == NULL)
		return;

	ret = sscanf(prop, "PCI%d,BAR%d", &pci_index, &bar);
	if (ret != 2)
		return;

	node_offset = fdt_path_offset(nfb->fdt, "/drivers/mi");
	node_offset = fdt_subnode_offset(nfb->fdt, node_offset, prop);
	fdt_del_node(nfb->fdt, node_offset);

	list_for_each_entry(mi, &nfb_mi->node_list, nfb_mi_list) {
		if (mi->bar == bar && mi->pci_index == pci_index) {
			nfb_bus_unregister(nfb, &mi->bus);
			nfb_mi_unmap(nfb, mi);
			list_del(&mi->nfb_mi_list);

			dev_info(&nfb->pci->dev, "nfb_mi: MI%d on PCI%d detached\n", bar, pci_index);
			break;
		}
	}
}

static void nfb_mi_detach_node(struct nfb_device *nfb, void *priv, int base_offset)
{
	int base_depth;
	int supernode_offset;
	int node_offset = -1;

	char path[MAX_FDT_PATH_LENGTH];
	char base_path[MAX_FDT_PATH_LENGTH];

	base_depth = fdt_node_depth(nfb->fdt, base_offset);
	if (base_depth < 0)
		return;

	if (fdt_get_path(nfb->fdt, base_offset, base_path, MAX_FDT_PATH_LENGTH) < 0)
		return;

	while ((node_offset = fdt_node_offset_by_compatible(nfb->fdt, node_offset, "netcope,bus,mi")) >= 0) {
		supernode_offset = fdt_supernode_atdepth_offset(nfb->fdt, node_offset, base_depth, NULL);
		/* Check if this node_offset is successor of base_offset */
		if (supernode_offset != base_offset)
			continue;

		/* Store path because of editing FDT */
		if (fdt_get_path(nfb->fdt, node_offset, path, MAX_FDT_PATH_LENGTH) < 0)
			continue;

		nfb_mi_detach_bus(nfb, priv, node_offset);

		/* FDT was edited, restore node_offset from path */
		node_offset = fdt_path_offset(nfb->fdt, path);
		base_offset = fdt_path_offset(nfb->fdt, base_path);
	}
}

void nfb_mi_detach(struct nfb_device* nfb, void *priv)
{
	int node_offset = -1;
	struct nfb_mi *mi = priv;

	node_offset = fdt_path_offset(nfb->fdt, "/firmware");
	nfb_mi_detach_node(nfb, mi, node_offset);

	node_offset = fdt_path_offset(nfb->fdt, "/drivers/mi");
	fdt_del_node(nfb->fdt, node_offset);

	kfree(mi);
}

module_param(mi_debug, bool, S_IRUGO);
MODULE_PARM_DESC(mi_debug, "Allow open whole MI bus for debug purposes [no]");
