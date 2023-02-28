/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Component and bus driver module of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <libfdt.h>

#include "nfb.h"

struct nfb_bus *nfb_bus_match(const struct nfb_device *nfb, int nodeoffset);

struct nfb_comp *nfb_comp_open_ext(const struct nfb_device *nfb, int nodeoffset, size_t user_size)
{
	int ret;
	int proplen;
	struct nfb_comp *comp;
	const fdt32_t *prop32;
	const fdt64_t *prop64;

	char path[MAX_FDT_PATH_LENGTH];

	if (fdt_get_path(nfb->fdt, nodeoffset, path, MAX_FDT_PATH_LENGTH) != 0) {
		ret = -EBADF;
		goto err_fdt_get_path;
	}
	proplen = strlen(path) + 1;

	comp = kmalloc(sizeof(*comp) + user_size + proplen, GFP_KERNEL);
	if (comp == NULL) {
		ret = -ENOMEM;
		goto err_alloc_comp;
	}

	comp->path = user_size + (char*)(comp + 1);
	strncpy(comp->path, path, proplen);

	prop32 = fdt_getprop(nfb->fdt, nodeoffset, "reg", &proplen);
	prop64 = (fdt64_t *)prop32;

	if (proplen == sizeof(*prop32) * 2) {
		comp->offset = fdt32_to_cpu(prop32[0]);
		comp->size   = fdt32_to_cpu(prop32[1]);
	} else if (proplen == sizeof(*prop64) + sizeof(*prop32)) {
		comp->offset = fdt64_to_cpu(prop64[0]);
		comp->size   = fdt32_to_cpu(prop32[2]);
	} else if (proplen == sizeof(*prop64) * 2) {
		comp->offset = fdt64_to_cpu(prop64[0]);
		comp->size   = fdt64_to_cpu(prop64[1]);
	} else {
		ret = -ENODEV;
		goto err_fdt_nobase;
	}

	comp->bus = nfb_bus_match(nfb, nodeoffset);
	if (!comp->bus)
		goto err_bus_match;
	comp->nfb = (struct nfb_device *)nfb;

	return comp;

err_bus_match:
err_fdt_nobase:
	kfree(comp);
err_alloc_comp:
err_fdt_get_path:
	return NULL;
}

struct nfb_comp *nfb_comp_open(const struct nfb_device *nfb, int nodeoffset)
{
	return nfb_comp_open_ext(nfb, nodeoffset, 0);
}

void nfb_comp_close(struct nfb_comp *comp)
{
	kfree(comp);
}

const char *nfb_comp_path(struct nfb_comp *comp)
{
	return comp->path;
}

int nfb_comp_lock(struct nfb_comp *comp, uint32_t features)
{
	int cnt = 0;
	struct nfb_lock lock;

	lock.path = comp->path;
	lock.features = features;

	while (nfb_lock_try_lock(comp->nfb, &comp->nfb->kernel_app, lock) != 0) {
		msleep(1);
		cnt++;
		if (cnt > 100) {
			dev_warn(comp->nfb->dev, "Can't lock comp %s within 100 ms\n", comp->path);
			return 0;
		}
	}

	return 1;
}

void nfb_comp_unlock(struct nfb_comp *comp, uint32_t features)
{
	struct nfb_lock lock;

	lock.path = comp->path;
	lock.features = features;
	nfb_lock_unlock(comp->nfb, &comp->nfb->kernel_app, lock);
}

struct nfb_bus *nfb_bus_match(const struct nfb_device *nfb, int nodeoffset)
{
	struct nfb_bus *bus;

	while ((nodeoffset = fdt_parent_offset(nfb->fdt, nodeoffset)) >= 0) {
		list_for_each_entry(bus, &nfb->buses, bus_list) {
			if (fdt_path_offset(nfb->fdt, bus->path) == nodeoffset)
				return bus;
		}
	}

	return NULL;
}

void nfb_bus_register(struct nfb_device *nfb, struct nfb_bus *bus)
{
	list_add(&bus->bus_list, &nfb->buses);
}

void nfb_bus_unregister(struct nfb_device *nfb, struct nfb_bus *bus)
{
	list_del(&bus->bus_list);
}
