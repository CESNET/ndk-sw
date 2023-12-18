/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - NIC RSS component
 *
 * Copyright (C) 2023 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_NIC_RSS_H
#define NETCOPE_NIC_RSS_H

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_CESNET_NIC_RSS "cesnet,nic_rss"

struct nc_nic_rss {
	int reta_capacity;
	int key_size;
};

static inline struct nc_nic_rss *nc_nic_rss_open(struct nfb_device *dev, int fdt_offset)
{
	struct nc_nic_rss *rss;
	struct nfb_comp *comp;
	const fdt32_t *prop;
	int proplen;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_CESNET_NIC_RSS))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_nic_rss));
	if (!comp)
		return NULL;

	rss = (struct nc_nic_rss*) nfb_comp_to_user(comp);

	prop = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), fdt_offset, "reta_capacity", &proplen);
	if (proplen != sizeof(*prop))
		goto err_prop;
	rss->reta_capacity = fdt32_to_cpu(*prop);

	prop = (const fdt32_t*) fdt_getprop(nfb_get_fdt(dev), fdt_offset, "key_size", &proplen);
	if (proplen != sizeof(*prop))
		goto err_prop;
	rss->key_size = fdt32_to_cpu(*prop);

	return rss;

err_prop:
	nfb_comp_close(comp);
	return NULL;
}

static inline void nc_nic_rss_close(struct nc_nic_rss * rss)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	nfb_comp_close(comp);
}

static inline int nc_nic_rss_write_key(struct nc_nic_rss *rss, int channel, const unsigned char *key, int key_len)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	int i;

	if (!nfb_comp_lock(comp, 1))
		return -EAGAIN;

	for (i = 0; i < key_len; i++) {
		nfb_comp_write32(comp, 0x00, (channel << 16) | (i >> 2));
		nfb_comp_write8(comp, 0x18 + i % 4, key[i]);
	}

	nfb_comp_write32(comp, 0x14, 1);

	nfb_comp_unlock(comp, 1);

	return 0;
}

static inline int nc_nic_rss_read_key(struct nc_nic_rss *rss, int channel, unsigned char *key, int key_len)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	int i;

	if (rss->key_size < key_len)
		return -ENOMEM;

	if (!nfb_comp_lock(comp, 1))
		return -EAGAIN;

	for (i = 0; i < key_len; i++) {
		nfb_comp_write32(comp, 0x00, (channel << 16) | (i >> 2));
		key[i] = nfb_comp_read8(comp, 0x18 + i % 4);
	}

	nfb_comp_unlock(comp, 1);
	return 0;
}

static inline int nc_nic_rss_set_input(struct nc_nic_rss *rss, int channel, int fn)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	uint32_t val = 0;

	if (!nfb_comp_lock(comp, 1))
		return -EAGAIN;

	val |= channel << 16;

	nfb_comp_write32(comp, 0x00, channel);
	nfb_comp_write32(comp, 0x10, fn);

	nfb_comp_unlock(comp, 1);
	return 0;
}

static inline int nc_nic_rss_get_input(struct nc_nic_rss *rss, int channel, int *fn)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	uint32_t val = 0;

	if (!nfb_comp_lock(comp, 1))
		return -EAGAIN;

	val |= channel << 16;

	nfb_comp_write32(comp, 0x00, channel);
	val = nfb_comp_read32(comp, 0x10);
	if (fn)
		*fn = val;

	nfb_comp_unlock(comp, 1);
	return 0;
}

static inline int nc_nic_rss_set_reta(struct nc_nic_rss *rss, int channel, int hash, int queue)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	uint32_t val = 0;

	if (!nfb_comp_lock(comp, 1))
		return -EAGAIN;

	val |= channel << 16;
	val |= hash & 0xFFFF;

	nfb_comp_write32(comp, 0x00, val);
	nfb_comp_write32(comp, 0x1C, queue);

	nfb_comp_unlock(comp, 1);
	return 0;
}

static inline int nc_nic_rss_get_reta(struct nc_nic_rss *rss, int channel, int hash, int *queue)
{
	struct nfb_comp *comp = nfb_user_to_comp(rss);
	uint32_t val = 0;

	if (!nfb_comp_lock(comp, 1))
		return -EAGAIN;

	val |= channel << 16;
	val |= hash & 0xFFFF;

	nfb_comp_write32(comp, 0x00, val);
	val = nfb_comp_read32(comp, 0x1C);

	if (queue)
		*queue = val;

	nfb_comp_unlock(comp, 1);
	return 0;
}

static inline int nc_nic_rss_get_reta_size(struct nc_nic_rss *rss)
{
	return rss->reta_capacity;
}

static inline int nc_nic_rss_get_key_size(struct nc_nic_rss *rss)
{
	return rss->key_size;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_NIC_RSS_H */
