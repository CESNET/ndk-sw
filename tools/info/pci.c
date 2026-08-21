/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * PCIe endpoint status: sysfs/libpci probing and attach/bind/map/link
 * state classification for nfb-info's device and PCI listings.
 *
 * Copyright (C) 2026 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfdt.h>

#include "pci.h"

#define PCI_SYSFS_PREFIX "/sys/bus/pci/devices/"

/* VSEC header used by NFB cards for DTB/endpoint info. */
#define NFB_PCI_VSEC_HEADER 0x02010D7B

int ep_state_bdf_filter(const struct dirent *dir)
{
	return strchr(dir->d_name, ':') != NULL;
}

static int read_sysfs_string(const char *path, char *buf, size_t buflen)
{
	int fd;
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, buflen - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
	                 buf[n-1] == ' '  || buf[n-1] == '\t'))
		buf[--n] = '\0';
	return 0;
}

void ep_state_format(int flags, char *buf, size_t buflen)
{
	size_t n = 0;

	if (buflen == 0)
		return;
	buf[0] = '\0';
	if (flags == 0) {
		snprintf(buf, buflen, "ok");
		return;
	}

#define EP_ST_ADD(bit, name) do { \
	if (flags & (bit)) { \
		n += snprintf(buf + n, buflen > n ? buflen - n : 0, "%s%s", \
		              n ? "," : "", (name)); \
	} \
} while (0)

	EP_ST_ADD(EP_ST_NOT_ATTACHED, "unattached");
	EP_ST_ADD(EP_ST_NOT_BOUND, "unbound");
	EP_ST_ADD(EP_ST_UNMAPPED, "unmapped");
	EP_ST_ADD(EP_ST_DEGRADED, "degraded");
	EP_ST_ADD(EP_ST_ORPHAN, "orphan");
#undef EP_ST_ADD
}

static int pci_bdf_bound_to_nfb(const char *bdf)
{
	char path[PATH_MAX];

	if (bdf == NULL || bdf[0] == '\0')
		return 0;
	snprintf(path, sizeof(path), NFB_PCI_DRIVER_DIR "%s", bdf);
	return access(path, F_OK) == 0;
}

static int endpoint_has_unmapped_bars(const void *fdt, int ep_index)
{
	int bar, off, len;
	const uint64_t *prop64;
	char path[64];

	if (fdt == NULL || ep_index < 0)
		return 0;

	for (bar = 0; bar < 6; bar++) {
		snprintf(path, sizeof(path), "/drivers/mi/PCI%d,BAR%d", ep_index, bar);
		off = fdt_path_offset(fdt, path);
		if (off < 0)
			continue;
		prop64 = fdt_getprop(fdt, off, "mmap_size", &len);
		if (len != sizeof(*prop64) || fdt64_to_cpu(*prop64) == 0)
			return 1;
	}
	return 0;
}

/* "16.0 GT/s PCIe" -> "16 GT/s", to match pci_speed_string() */
static void normalize_speed(char *s)
{
	char *p;

	p = strstr(s, " PCIe");
	if (p)
		*p = '\0';

	p = strstr(s, ".0 ");
	if (p)
		memmove(p, p + 2, strlen(p + 2) + 1);
}

static void read_pci_link(const char *bdf, struct ep_state_link_info *li)
{
	char spath[PATH_MAX];

	li->speed[0] = '\0';
	li->width[0] = '\0';
	li->max_speed[0] = '\0';
	li->max_width[0] = '\0';

	snprintf(spath, sizeof(spath), PCI_SYSFS_PREFIX "%s/current_link_speed", bdf);
	if (read_sysfs_string(spath, li->speed, sizeof(li->speed)) == 0)
		normalize_speed(li->speed);

	snprintf(spath, sizeof(spath), PCI_SYSFS_PREFIX "%s/current_link_width", bdf);
	read_sysfs_string(spath, li->width, sizeof(li->width));

	snprintf(spath, sizeof(spath), PCI_SYSFS_PREFIX "%s/max_link_speed", bdf);
	if (read_sysfs_string(spath, li->max_speed, sizeof(li->max_speed)) == 0)
		normalize_speed(li->max_speed);

	snprintf(spath, sizeof(spath), PCI_SYSFS_PREFIX "%s/max_link_width", bdf);
	read_sysfs_string(spath, li->max_width, sizeof(li->max_width));
}

static int pci_link_degraded(const char *bdf)
{
	struct ep_state_link_info li;
	int speed_known, width_known;

	if (bdf == NULL || bdf[0] == '\0')
		return 0;

	read_pci_link(bdf, &li);
	speed_known = li.speed[0] && li.max_speed[0];
	width_known = li.width[0] && li.max_width[0];
	return (speed_known && strcmp(li.speed, li.max_speed) != 0) ||
	       (width_known && strcmp(li.width, li.max_width) != 0);
}

int ep_state_classify(const void *fdt, int ep_index, const char *bdf, int orphan)
{
	int flags = 0;

	if (orphan)
		flags |= EP_ST_ORPHAN;

	if (bdf == NULL || bdf[0] == '\0') {
		flags |= EP_ST_NOT_ATTACHED;
		return flags;
	}

	if (!pci_bdf_bound_to_nfb(bdf))
		flags |= EP_ST_NOT_BOUND;

	if (!(flags & (EP_ST_NOT_BOUND | EP_ST_NOT_ATTACHED)) &&
	    endpoint_has_unmapped_bars(fdt, ep_index))
		flags |= EP_ST_UNMAPPED;

	if (!(flags & EP_ST_NOT_BOUND) && pci_link_degraded(bdf))
		flags |= EP_ST_DEGRADED;

	return flags;
}

/* eps[] = expected ∪ FDT endpoints; *overall = OR of all states */
int ep_state_collect(const void *fdt, struct ep_state_desc *eps, int max, int *overall)
{
	unsigned mask;
	int ep_index;
	int count = 0;
	int ovr = 0;

	mask = nfb_pci_ep_mask_by_mi_bus_nodes(fdt) | nfb_pci_ep_mask_by_system_endpoints(fdt);
	for (ep_index = 0; ep_index < NFB_PCI_EP_MAX && count < max; ep_index++) {
		char ep_path[64];
		const char *bdf = NULL;
		int node = -1;
		int state;

		if (!(mask & (1u << ep_index)))
			continue;

		snprintf(ep_path, sizeof(ep_path), "/system/device/endpoint%d", ep_index);
		node = fdt_path_offset(fdt, ep_path);
		if (node >= 0)
			bdf = fdt_getprop(fdt, node, "pci-slot", NULL);

		state = ep_state_classify(fdt, ep_index, bdf, 0);
		eps[count].index = ep_index;
		eps[count].fdt_offset = node;
		snprintf(eps[count].bdf, sizeof(eps[0].bdf), "%s", bdf ? bdf : "");
		eps[count].state = state;
		ovr |= state;
		count++;
	}

	if (overall)
		*overall = ovr;
	return count;
}

static const char *pcie_speed_mts(unsigned code)
{
	switch (code) {
	case 1: return "2.5 GT/s";
	case 2: return "5 GT/s";
	case 3: return "8 GT/s";
	case 4: return "16 GT/s";
	case 5: return "32 GT/s";
	case 6: return "64 GT/s";
	default: return NULL;
	}
}

/* config-space fallback, works even when unbound from nfb */
static void read_pci_link_libpci(struct pci_access *pacc, const char *bdf, struct ep_state_link_info *li)
{
	struct pci_dev *dev;
	struct pci_cap *cap;
	unsigned int domain, bus, devno, func;
	u32 lnkcap;
	u16 lnksta;
	const char *speed;
	unsigned width;

	if (pacc == NULL || bdf == NULL || bdf[0] == '\0')
		return;
	if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus, &devno, &func) != 4)
		return;

	dev = pci_get_dev(pacc, domain, bus, devno, func);
	if (!dev)
		return;
	pci_fill_info(dev, PCI_FILL_CAPS);

	cap = pci_find_cap(dev, PCI_CAP_ID_EXP, PCI_CAP_NORMAL);
	if (!cap)
		goto out;

	lnkcap = pci_read_long(dev, cap->addr + PCI_EXP_LNKCAP);
	lnksta = pci_read_word(dev, cap->addr + PCI_EXP_LNKSTA);

	if (li->speed[0] == '\0') {
		speed = pcie_speed_mts(lnksta & PCI_EXP_LNKSTA_SPEED);
		if (speed)
			snprintf(li->speed, sizeof(li->speed), "%s", speed);
	}
	if (li->max_speed[0] == '\0') {
		speed = pcie_speed_mts(lnkcap & PCI_EXP_LNKCAP_SPEED);
		if (speed)
			snprintf(li->max_speed, sizeof(li->max_speed), "%s", speed);
	}
	if (li->width[0] == '\0') {
		width = (lnksta & PCI_EXP_LNKSTA_WIDTH) >> 4;
		if (width)
			snprintf(li->width, sizeof(li->width), "%u", width);
	}
	if (li->max_width[0] == '\0') {
		width = (lnkcap & PCI_EXP_LNKCAP_WIDTH) >> 4;
		if (width)
			snprintf(li->max_width, sizeof(li->max_width), "%u", width);
	}

out:
	pci_free_dev(dev);
}

void ep_state_fill_link(struct pci_access *pacc, const char *bdf, struct ep_state_link_info *li)
{
	if (bdf == NULL || bdf[0] == '\0') {
		memset(li, 0, sizeof(*li));
		return;
	}

	read_pci_link(bdf, li);
	if (li->speed[0] == '\0' || li->width[0] == '\0' ||
	    li->max_speed[0] == '\0' || li->max_width[0] == '\0')
		read_pci_link_libpci(pacc, bdf, li);
}

const char *ep_state_link_number(const char *str, char *buf, size_t buflen)
{
	const char *sp;
	size_t len;

	if (str[0] == '\0')
		return "?";

	sp = strchr(str, ' ');
	len = sp ? (size_t)(sp - str) : strlen(str);
	if (len >= buflen)
		len = buflen - 1;
	memcpy(buf, str, len);
	buf[len] = '\0';
	return buf;
}

const char *ep_state_str_or_unknown(const char *str)
{
	return str[0] ? str : "?";
}

int ep_state_read_endpoint_id(struct pci_access *pacc, const char *bdf)
{
	struct pci_dev *dev;
	struct pci_cap *cap;
	unsigned int domain, bus, devno, func;
	int ret = -1;

	if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus, &devno, &func) != 4)
		return -1;

	dev = pci_get_dev(pacc, domain, bus, devno, func);
	if (!dev)
		return -1;

	cap = pci_find_cap(dev, PCI_EXT_CAP_ID_VNDR, PCI_CAP_EXTENDED);
	while (cap) {
		u32 vsec = pci_read_long(dev, cap->addr + 4);
		if (vsec == NFB_PCI_VSEC_HEADER) {
			u32 reg = pci_read_long(dev, cap->addr + 8);
			if (reg & 0x80000000)
				ret = reg & 0xf;
			break;
		}
		cap = cap->next;
	}

	pci_free_dev(dev);
	return ret;
}
