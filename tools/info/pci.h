/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * PCIe endpoint status for nfb-info's device and PCI listings.
 *
 * Copyright (C) 2026 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_TOOLS_INFO_PCI_H
#define NFB_TOOLS_INFO_PCI_H

#include <dirent.h>
#include <stddef.h>

#include <pci/pci.h>

#include <netcope/pci_ep.h>

#define NFB_PCI_DRIVER_DIR "/sys/module/nfb/drivers/pci:nfb/"

enum {
	EP_ST_DEGRADED     = 1 << 0,
	EP_ST_UNMAPPED     = 1 << 1,
	EP_ST_NOT_BOUND    = 1 << 2,
	EP_ST_NOT_ATTACHED = 1 << 3,
	EP_ST_ORPHAN       = 1 << 4,
};

#define EP_ST_STRUCTURAL (EP_ST_NOT_ATTACHED | EP_ST_NOT_BOUND | EP_ST_UNMAPPED | EP_ST_ORPHAN)

static inline int ep_state_is_structural(int flags)
{
	return flags & EP_ST_STRUCTURAL;
}

struct ep_state_desc {
	int index;
	int fdt_offset;
	char bdf[16];
	int state;
};

struct ep_state_link_info {
	char speed[32];
	char width[24];
	char max_speed[32];
	char max_width[24];
};

int ep_state_bdf_filter(const struct dirent *dir);
void ep_state_format(int flags, char *buf, size_t buflen);
int ep_state_classify(const void *fdt, int ep_index, const char *bdf, int orphan);
int ep_state_collect(const void *fdt, struct ep_state_desc *eps, int max, int *overall);
void ep_state_fill_link(struct pci_access *pacc, const char *bdf, struct ep_state_link_info *li);
const char *ep_state_link_number(const char *str, char *buf, size_t buflen);
const char *ep_state_str_or_unknown(const char *str);
int ep_state_read_endpoint_id(struct pci_access *pacc, const char *bdf);

#endif /* NFB_TOOLS_INFO_PCI_H */
