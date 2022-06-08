/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - MAC common
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_MAC_H
#define NETCOPE_MAC_H

#ifdef __cplusplus
extern "C" {
#endif

enum nc_mac_speed {
	MAC_SPEED_UNKNOWN = 0x0,
	MAC_SPEED_10G = 0x3,
	MAC_SPEED_40G = 0x4,
	MAC_SPEED_100G = 0x5
};

static inline int
nc_get_default_mac_for_channel(struct nfb_device *dev, uint8_t *addr_bytes,
		int bytes_cnt, unsigned ifc_nr)
{
	int ret;
	int len;
	int sn;
	const void *fdt;
	const char *prop;
	const uint32_t *prop32;
	int node;
	int type = -1;

	struct card_type_by_name {
		const char * name;
		int type;
	} card_type_by_name[] = {
		{"NFB-40G", 0x20},
		{"NFB-40G2", 0x80},
		{"NFB-100G1", 0x40},
		{"NFB-100G2Q", 0xA0},
		{"NFB-200G2QL", 0x60},
		{NULL, 0},
	};

	if (bytes_cnt != 6)
		return -ENOMEM;

	fdt = nfb_get_fdt(dev);
        node = fdt_path_offset(fdt, "/board/");
        prop = fdt_getprop(fdt, node, "card-name", &len);

	if (prop == NULL)
		return -ENODEV;

	if (0) {
	} else if (strcmp(prop, "FB2CGG3") == 0) {
		if (ifc_nr >= 16)
			return -ENOMEM;
	        ret = nfb_mtd_read(dev, 0, ifc_nr * 6, addr_bytes, bytes_cnt);
		return ret;
	}

	ret = 0;
	while (card_type_by_name[ret].name) {
		if (strcmp(prop, card_type_by_name[ret].name) == 0) {
			type = card_type_by_name[ret].type;
			break;
		}
		ret++;
	}

	if (type == -1)
		return -ENODEV;

	prop32 = fdt_getprop(fdt, node, "serial-number", &len);
	if (len < 0)
		return -ENODEV;

	sn = fdt32_to_cpu(*prop32);

	addr_bytes[0] = 0x00;
	addr_bytes[1] = 0x11;
	addr_bytes[2] = 0x17;
	addr_bytes[3] = type;
	addr_bytes[4] = sn >> 8;
	addr_bytes[5] = (sn << 4) | (ifc_nr & 0x0F);

	return 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_MAC_H */

