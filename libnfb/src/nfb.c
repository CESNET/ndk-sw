/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - base module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <libfdt.h>
#include <nfb/nfb.h>
#include <linux/nfb/nfb.h>

#include "nfb.h"

//#include "mi.c"

#define PATH_LEN (32)

const void *nfb_get_fdt(const struct nfb_device *dev)
{
	return dev->fdt;
}

struct nfb_comp *nfb_user_to_comp(void *ptr)
{
	return ((struct nfb_comp *) ptr) - 1;
}

void *nfb_comp_to_user(struct nfb_comp *ptr)
{
	return (void *)(ptr + 1);
}

struct nfb_device *nfb_open_ext(const char *devname, int oflag)
{
	off_t size;
	int fd;
	int ret;
	struct nfb_device *dev;
	char path[PATH_LEN];
	unsigned index;

	if (sscanf(devname, "%u", &index) == 1) {
		ret = snprintf(path, PATH_LEN, "/dev/nfb%u", index);
		if (ret >= PATH_LEN || ret < 0) {
			errno = ENODEV;
			return NULL;
		}
		devname = (const char *)path;
	}

	/* Open device */
	fd = open(devname, O_RDWR | oflag, 0);
	if (fd == -1) {
		goto err_open;
	}

	/* Allocate structure */
	dev = (struct nfb_device*) malloc(sizeof(struct nfb_device));
	if (dev == 0) {
		goto err_malloc_dev;
	}

	dev->fd = fd;
	dev->queue_count = 0;
	dev->queues = NULL;

	/* Read FDT from driver */
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	if (size == 0) {
		errno = ENODEV;
		fprintf(stderr, "FDT size is zero\n");
		goto err_fdt_size;
	}
	dev->fdt = malloc(size);
	if (dev->fdt == NULL) {
		goto err_malloc_fdt;
	}
	ret = read(fd, dev->fdt, size);
	if (ret != size) {
		errno = ENODEV;
		goto err_read_fdt;
	}

	/* Check for valid FDT */
	ret = fdt_check_header(dev->fdt);
	if (ret != 0) {
		errno = EBADF;
		goto err_fdt_check_header;
	}

	return dev;

err_fdt_check_header:
err_read_fdt:
	free(dev->fdt);
err_fdt_size:
err_malloc_fdt:
	free(dev);
err_malloc_dev:
	close(fd);
err_open:
	return NULL;
}

struct nfb_device *nfb_open(const char *devname)
{
	return nfb_open_ext(devname, 0);
}

void nfb_close(struct nfb_device *dev)
{
	close(dev->fd);
	if (dev->queues)
		free(dev->queues);
	free(dev->fdt);
	free(dev);
	dev = NULL;
}

int nfb_get_system_id(const struct nfb_device *dev)
{
	int len;
	int fdt_offset;
	const uint32_t *prop32;

	fdt_offset = fdt_path_offset(dev->fdt, "/system/device");
	prop32 = fdt_getprop(dev->fdt, fdt_offset, "card-id", &len);
	if (fdt_offset < 0 || len != sizeof(*prop32)) {
		return -1;
	}
	return fdt32_to_cpu(*prop32);
}

int nfb_comp_count(const struct nfb_device *dev, const char *compatible)
{
	if (!dev || !compatible)
		return -1;

	const void *fdt = nfb_get_fdt(dev);
	int node_offset;
	int count = 0;

	fdt_for_each_compatible_node(fdt, node_offset, compatible) {
		count++;
	}

	return count;
}

int nfb_comp_find(const struct nfb_device *dev, const char *compatible, unsigned index)
{
	if (!dev || !compatible)
		return -1;

	const void *fdt = nfb_get_fdt(dev);
	int node_offset;
	unsigned count = 0;

	fdt_for_each_compatible_node(fdt, node_offset, compatible) {
		if (count == index)
			return node_offset;
		count++;
	}

	return node_offset;
}

/**
 * find_in_subtree() - get offset of n-th compatible component in subtree
 *
 * @fdt:  FDT blob
 * @subtree_offset: Offset of the parent node (subtree root)
 * @compatible:     Searched component compatible property
 * @index_searched: Index of the searched component
 * @index_current:  [internal] Pointer to zero-initialized variable
 */
static int find_in_subtree(const void* fdt,
		int subtree_offset,
		const char* compatible,
		unsigned index_searched,
		unsigned* index_current)
{
	int node;
	int ret;

	fdt_for_each_subnode(node, fdt, subtree_offset) {
		if (fdt_node_check_compatible(fdt, node, compatible) == 0) {
			(*index_current)++;
			if (*index_current == (index_searched + 1) ) {
				return node;
			}
		}

		if (fdt_first_subnode(fdt, node) > 0) {
			ret = find_in_subtree(fdt, node, compatible, index_searched, index_current);
			if (ret > 0)
				return ret;
		}
	}

	return -FDT_ERR_NOTFOUND;
}


int nfb_comp_find_in_parent(const struct nfb_device *dev, const char *compatible, unsigned index, int parent_offset)
{
	if (!dev || !compatible)
		return -1;

	const void *fdt = nfb_get_fdt(dev);
	unsigned subtree_index = 0;

	return find_in_subtree(fdt, parent_offset, compatible, index, &subtree_index);
}

int nfb_bus_open_mi(struct nfb_bus *bus, int node_offset);
void nfb_bus_close_mi(struct nfb_bus *bus);

int nfb_bus_open_for_comp(struct nfb_comp *comp, int nodeoffset)
{
	int compatible_offset;

	do {
		compatible_offset = -1;
		while ((compatible_offset = fdt_node_offset_by_compatible(comp->dev->fdt, compatible_offset, "netcope,bus,mi")) >= 0) {
			if (compatible_offset == nodeoffset) {
				return nfb_bus_open(comp, nodeoffset);
			}
		}
	} while ((nodeoffset = fdt_parent_offset(comp->dev->fdt, nodeoffset)) >= 0);

	return ENODEV;
}

int nfb_bus_open(struct nfb_comp *comp, int fdt_offset)
{
	int ret;

	comp->bus.dev = comp->dev;
	comp->bus.type = NFB_BUS_TYPE_MI;

	ret = nfb_bus_open_mi(&comp->bus, fdt_offset);
	if (ret != 0) {
		printf("open MI error, %d\n", ret);
		goto err_bus_open;
	}

	return 0;

err_bus_open:
	return ret;
}

void nfb_bus_close(struct nfb_comp * comp)
{
	nfb_bus_close_mi(&comp->bus);
}

struct nfb_comp *nfb_comp_open(const struct nfb_device *dev, int fdt_offset)
{
	return nfb_comp_open_ext(dev, fdt_offset, 0);
}

#define MAX_PATH_LEN  512

struct nfb_comp *nfb_comp_open_ext(const struct nfb_device *dev, int fdt_offset, int user_size)
{
	int ret;
	int proplen;
	const fdt32_t *prop;
	struct nfb_comp *comp;

	char path[MAX_PATH_LEN];

	prop = fdt_getprop(dev->fdt, fdt_offset, "reg", &proplen);
	if (proplen != sizeof(*prop) * 2) {
		errno = EBADFD;
		goto err_fdt_getprop;
	}

	if (fdt_get_path(dev->fdt, fdt_offset, path, MAX_PATH_LEN) != 0) {
		errno = EBADFD;
		goto err_fdt_get_path;
	}
	proplen = strlen(path) + 1;

	comp = malloc(sizeof(struct nfb_comp) + user_size + proplen);
	if (!comp) {
		goto err_malloc;
	}

	comp->dev = dev;
	comp->base = fdt32_to_cpu(prop[0]);
	comp->size = fdt32_to_cpu(prop[1]);

	comp->path = user_size + (char*)(comp + 1);
	strcpy(comp->path, path);

	ret = nfb_bus_open_for_comp(comp, fdt_offset);
	if (ret) {
		errno = ret;
		goto err_bus_open;
	}

	return comp;

err_bus_open:
	free(comp);
err_malloc:
err_fdt_get_path:
err_fdt_getprop:
	return NULL;
}

void nfb_comp_close(struct nfb_comp *comp)
{
	nfb_bus_close(comp);
	free(comp);
}

int nfb_comp_lock(const struct nfb_comp *comp, uint32_t features)
{
	struct nfb_lock lock;
	int ret;

	if (!comp)
		return -1;

	lock.path = comp->path;
	lock.features = features;

	while ((ret = ioctl(comp->dev->fd, NFB_LOCK_IOC_TRY_LOCK, &lock)) != 0) {
		if (ret != EBUSY)
			break;

		usleep(50);
	}

	return (ret == 0);
}

void nfb_comp_unlock(const struct nfb_comp *comp, uint32_t features)
{
	struct nfb_lock lock;

	if (!comp)
		return;

	lock.path = comp->path;
	lock.features = features;

	ioctl(comp->dev->fd, NFB_LOCK_IOC_UNLOCK, &lock);
}

int nfb_comp_get_version(const struct nfb_comp *comp)
{
	int fdt_offset;
	int proplen;
	const fdt32_t *prop;

	if (!comp)
		return -1;

	fdt_offset = fdt_path_offset(comp->dev->fdt, comp->path);

	prop = fdt_getprop(comp->dev->fdt, fdt_offset, "version", &proplen);
	if (proplen != sizeof(*prop))
		return -1;

	return fdt32_to_cpu(*prop);
}

ssize_t nfb_bus_mi_read(void *p, void *buf, size_t nbyte, off_t offset);
ssize_t nfb_bus_mi_write(void *p, const void *buf, size_t nbyte, off_t offset);

ssize_t nfb_comp_read(const struct nfb_comp *comp, void *buf, size_t nbyte, off_t offset)
{
	if (offset + nbyte > comp->size)
		return -1;

	if (comp->bus.type == NFB_BUS_TYPE_MI) {
		return nfb_bus_mi_read(comp->bus.priv, buf, nbyte, offset + comp->base);
	} else {
		return comp->bus.read(comp->bus.priv, buf, nbyte, offset + comp->base);
	}
}

ssize_t nfb_comp_write(const struct nfb_comp *comp, const void *buf, size_t nbyte, off_t offset)
{
	if (offset + nbyte > comp->size)
		return -1;

	if (comp->bus.type == NFB_BUS_TYPE_MI) {
		return nfb_bus_mi_write(comp->bus.priv, buf, nbyte, offset + comp->base);
	} else {
		return comp->bus.write(comp->bus.priv, buf, nbyte, offset + comp->base);
	}
}
