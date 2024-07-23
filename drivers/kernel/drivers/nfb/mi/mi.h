/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MI driver header of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_MI_H
#define NFB_MI_H

struct nfb_mi {
	struct list_head node_list;
	struct nfb_device *nfb;
};

struct nfb_mi_node {
	struct list_head nfb_mi_list;
	struct nfb_mi *mi;

	resource_size_t mem_phys;
	resource_size_t mem_len;
	void __iomem *mem_virt;

	int pci_index;
	int bar;

	size_t mmap_offset;
	struct nfb_bus bus;
	int is_wc_mapped;
};

int nfb_mi_attach(struct nfb_device* nfb, void **priv);
void nfb_mi_detach(struct nfb_device* nfb, void *priv);

void nfb_mi_probe_endpoint(void *priv, struct nfb_pci_device *pci_device);
void nfb_mi_remove_endpoint(void *priv, struct nfb_pci_device *pci_device);

#endif //NFB_MI_H
