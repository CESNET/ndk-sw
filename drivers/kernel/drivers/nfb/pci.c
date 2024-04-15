/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI driver module of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/kref.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/idr.h>
#include <linux/xz.h>

#include <libfdt.h>

#include "nfb.h"
#include "pci.h"
#include "misc.h"
#include "mi/mi.h"
#include "ndp/ndp.h"

#define NFB_FDT_BURSTSIZE (16384)
#define NFB_FDT_MAXSIZE (65536)
#define NFB_FDT_FIXUP_NODE_NAME_LEN 16
#define NFB_CARD_NAME_GENERIC "COMBO-GENERIC"

static bool fallback_fdt = 1;
static bool fallback_fdt_boot = 0;
static bool flash_recovery_ro = 1;

struct list_head global_pci_device_list;

extern struct nfb_driver_ops nfb_registered_drivers[NFB_DRIVERS_MAX];
static const char *const nfb_card_name_generic = NFB_CARD_NAME_GENERIC;


const struct nfb_pci_dev nfb_device_infos [] = {
	/* fields:		    card name, 		ID_MTD,	SerialNoAddr,	SubtypeAddr,	card_type_id */
	[NFB_CARD_NFB40G]	= { "NFB-40G",	       -1,	-1,		-1,		0x04 },

	[NFB_CARD_NFB40G2]	= { "NFB-40G2",		0, 	0x00000004,	0x00000000,	0x01 },
	[NFB_CARD_NFB40G2_SG3]	= { "NFB-40G2_SG3",	0,	0x00000004,	0x00000000,	0x03 },

	[NFB_CARD_NFB100G]	= { "NFB-100G",		0,	0x00000004,	-1,		0x02, 0xc1c0 },

	[NFB_CARD_NFB100G2]	= { "NFB-100G2",	0,	0x01fc0004,	0x01fc0000,	0x00, 0xc2c0 },
	[NFB_CARD_NFB100G2Q]	= { "NFB-100G2Q",	0,	0x01fc0004,	0x01fc0000,	0x05, 0xc2c0 },
	[NFB_CARD_NFB100G2C]	= { "NFB-100G2C",	0,	0x01fc0004, 	0x01fc0000,	0x08, 0xc2c0 },

	[NFB_CARD_NFB200G2QL]	= { "NFB-200G2QL",	0,	0x03fc0004,	-1,		0x06, 0xc251 },

	[NFB_CARD_FB1CGG]	= { "FB1CGG",		0,	0x00000002,	0x00000001,	0x07 },
	[NFB_CARD_FB2CGG3]	= { "FB2CGG3",		0,	0x00000002,	0x00000001,	0x09 },
	[NFB_CARD_FB4CGG3]	= { "FB4CGG3",		0,	0x00000002,	0x00000001,	0x0A },

	[NFB_CARD_TIVOLI]	= { "TIVOLI",	       -1,      -1,             -1,             0x0B },

	[NFB_CARD_COMBO_GENERIC]= { NFB_CARD_NAME_GENERIC, -1,  -1,             -1,             0x0C, /* sub_device is forbidden */ },
	[NFB_CARD_COMBO400G1]	= { "COMBO-400G1",     -1,      -1,             -1,             0x0D, 0xc400 },
	[NFB_CARD_AGI_FH400G]	= { "AGI-FH400G",      -1,      -1,             -1,             0x0E },

	/* Last item */		  { NULL,	       -1,	-1,	 	-1,		0x00 },
};

/*
 * PCI identifiers of NFB cards
 */
const struct pci_device_id nfb_ids [] = {
	/* NFB-40G */
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xcb40), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_NFB40G]},
	/* NFB-80G */
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xcb80), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_NFB40G2]},
	/* NFB-100G */
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xc1c0), .driver_data = (unsigned long)NULL},
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xc1c1), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_NFB100G]},
	/* NFB-100G2 */
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xc2c0), .driver_data = (unsigned long)NULL},
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xc2c1), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_NFB100G2]},
	/* NFB-200G2QL */
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xc250), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_NFB200G2QL]},
	{ PCI_DEVICE(PCI_VENDOR_ID_NETCOPE,     0xc251), .driver_data = (unsigned long)NULL},
	/* FB1CGG */
	{ PCI_DEVICE(PCI_VENDOR_ID_FIBERBLAZE,  0xc240), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_FB1CGG]},
	{ PCI_DEVICE(PCI_VENDOR_ID_FIBERBLAZE,  0x00d0), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_FB1CGG]},
	{ PCI_DEVICE(PCI_VENDOR_ID_FIBERBLAZE,  0x00d1), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_FB1CGG]},
	/* FB2CGHH */
	{ PCI_DEVICE(PCI_VENDOR_ID_FIBERBLAZE,  0x00d2), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_TIVOLI]},
	{ PCI_DEVICE(PCI_VENDOR_ID_FIBERBLAZE,  0x00d3), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_TIVOLI]},

	{ PCI_DEVICE(PCI_VENDOR_ID_CESNET,      0xc000), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_COMBO_GENERIC]},
	{ PCI_DEVICE(PCI_VENDOR_ID_CESNET,      0xc400), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_COMBO400G1]},
	{ PCI_DEVICE(PCI_VENDOR_ID_REFLEXCES,   0xd001), .driver_data = (unsigned long)&nfb_device_infos[NFB_CARD_AGI_FH400G]},
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, nfb_ids);

/*
 * nfb_fdt_create_binary_slot - add one binary slot to DT for Flash access and booting
 */
static int nfb_fdt_create_binary_slot(void *fdt, int node, char *name, char *title, int id,
		int boot_id, int mtd, int base, int size)
{
	int fdt_offset, fdt_subnode_offset;
	fdt_offset = fdt_add_subnode(fdt, node, name);
	fdt_setprop_string(fdt, fdt_offset, "compatible", "netcope,binary_slot");
	fdt_setprop_string(fdt, fdt_offset, "module", "FPGA0");
	fdt_setprop_string(fdt, fdt_offset, "title", title);
	fdt_setprop_string(fdt, fdt_offset, "type", "mtd");
	fdt_setprop_u32(fdt, fdt_offset, "id", id);
	fdt_setprop_u32(fdt, fdt_offset, "boot_id", boot_id);

	fdt_subnode_offset = fdt_add_subnode(fdt, fdt_offset, "control-param");
	fdt_setprop_u32(fdt, fdt_subnode_offset, "mtd", mtd);
	fdt_setprop_u32(fdt, fdt_subnode_offset, "base", base);
	fdt_setprop_u32(fdt, fdt_subnode_offset, "size", size);

	if (flash_recovery_ro && !strcmp(title, "recovery"))
		fdt_setprop(fdt, fdt_subnode_offset, "ro", NULL, 0);

	return fdt_offset;
}

static inline void nfb_fdt_create_boot_type(void *fdt, int node, const char *type, int width)
{
	int subnode;

	subnode = fdt_add_subnode(fdt, node, "control-param");
	fdt_setprop_string(fdt, subnode, "boot-interface-type", type);
	if (width) {
		fdt_setprop_u32(fdt, subnode, "boot-interface-width", width);
	}
}

static inline void n6010_binary_slot_prepare(void *fdt, int node, const char *mod_val, const char* mod_mask, int mod_len)
{
	int subnode;
	uint64_t prop64;

	node = fdt_add_subnode(fdt, node, "image-prepare");
	subnode = fdt_add_subnode(fdt, node, "m0");

	prop64 = cpu_to_fdt64(8);
	fdt_appendprop(fdt, subnode, "modify-offset", &prop64, sizeof(prop64));
	fdt_appendprop(fdt, subnode, "modify-value", mod_val, mod_len);
	fdt_appendprop(fdt, subnode, "modify-mask",  mod_mask, mod_len);
}

/*
 * nfb_fdt_fixups - Fix the FDT: Create missing nodes and properties
 * @nfb: NFB device
 */
static void nfb_fdt_fixups(struct nfb_device *nfb)
{
	static const char *flag_fb_select_flash = "fb_select_flash";
	static const char *flag_flash_set_async = "flash_set_async";

	int i;
	int node, subnode;
	uint32_t prop32;
	struct nfb_pci_device *pci_device = NULL;

	const char *name;
	const char *card_name;
	void *fdt = nfb->fdt;

	enum pci_bus_speed speed;
	enum pcie_link_width width;

	char node_name[NFB_FDT_FIXUP_NODE_NAME_LEN];

	int proplen;

	static const char * boot_ctrl_compatibles[] = {
		"netcope,boot_controller",
		"netcope,intel_sdm_controller",
		"cesnet,pmci",
	};

	name = nfb->pci_name;

	node = fdt_path_offset(fdt, "/");
	node = fdt_add_subnode(fdt, node, "system");
	node = fdt_add_subnode(fdt, node, "device");

	/* Add index of device in system */
	fdt_setprop_u32(fdt, node, "card-id", nfb->minor);

	list_for_each_entry(pci_device, &nfb->pci_devices, pci_device_list) {
		snprintf(node_name, NFB_FDT_FIXUP_NODE_NAME_LEN, "endpoint%d", pci_device->index);
		node = fdt_path_offset(fdt, "/system/device");
		node = fdt_add_subnode(fdt, node, node_name);

		fdt_setprop_string(fdt, node, "pci-slot", pci_name(pci_device->pci));
		fdt_setprop_u32(fdt, node, "numa-node", dev_to_node(&pci_device->pci->dev));

		pcie_bandwidth_available(pci_device->pci, NULL, &speed, &width);
		fdt_setprop_u32(fdt, node, "pci-speed", speed);
		fdt_setprop_u32(fdt, node, "pcie-link-width", width);
	}

	node = fdt_path_offset(fdt, "/firmware");
	card_name = fdt_getprop(fdt, node, "card-name", &proplen);
	if (proplen <= 0)
		card_name = "";

	for (node = -1, i = 0; i < ARRAY_SIZE(boot_ctrl_compatibles); i++) {
		node = fdt_node_offset_by_compatible(fdt, -1, boot_ctrl_compatibles[i]);
		if (node >= 0)
			break;
	}

	if (node < 0)
		return;

	/* Add binary slots to DT for coresponding partitions for Flash access and booting */
	/* TODO - move definition to firmware FDT */
	if (!strcmp(name, "NFB-200G2QL")) {
		prop32 = cpu_to_fdt32(2);
		fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));

		prop32 = cpu_to_fdt32(28);
		fdt_appendprop(fdt, node, "mtd_bit", &prop32, sizeof(prop32));

		/* INFO: This card needs to set Flash async mode */
		fdt_appendprop(fdt, node, "flags", flag_flash_set_async, strlen(flag_flash_set_async)+1);

		nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery"     , 1, 0, 1, 0x00000000, 0x04000000-0x40000);
		nfb_fdt_create_binary_slot(fdt, node, "image0", "configuration", 0, 1, 0, 0x00000000, 0x04000000-0x40000);

		nfb_fdt_create_boot_type(fdt, node, "BPI", 16);
	} else if (!strcmp(name, "NFB-100G2")) {
		prop32 = cpu_to_fdt32(1);
		fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));
		/* INFO: This card needs to set Flash async mode */
		fdt_appendprop(fdt, node, "flags", flag_flash_set_async, strlen(flag_flash_set_async)+1);

		nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery"     , 1, 0, 0, 0x02000000, 0x02000000);
		nfb_fdt_create_binary_slot(fdt, node, "image0", "configuration", 0, 1, 0, 0x00000000, 0x02000000-0x40000);

		nfb_fdt_create_boot_type(fdt, node, "BPI", 16);
	} else if (!strcmp(name, "FB1CGG")) {
		prop32 = cpu_to_fdt32(2);
		fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));

		prop32 = cpu_to_fdt32(128 * 1024 * 1024);
		fdt_appendprop(fdt, node, "mtd_size", &prop32, sizeof(prop32));

		/* INFO: This card needs to do a special command for switching between Flash memories */
		fdt_appendprop(fdt, node, "flags", flag_fb_select_flash, strlen(flag_fb_select_flash)+1);

		/* INFO: This card needs specifically aligned bitstream with sync header 0x995566AA on bytes 48-51 */
		subnode = nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery"     , 1, 0, 0, 0x00040000, 0x04000000-0x40000);
		subnode = fdt_subnode_offset(fdt, subnode, "control-param");
		fdt_setprop_u32(fdt, subnode, "bitstream-offset", 32);

		subnode = nfb_fdt_create_binary_slot(fdt, node, "image0", "configuration", 0, 1, 1, 0x00040000, 0x04000000-0x40000);
		subnode = fdt_subnode_offset(fdt, subnode, "control-param");
		fdt_setprop_u32(fdt, subnode, "bitstream-offset", 32);

		/* INFO: mango is SMAPx32, but this works too */
		nfb_fdt_create_boot_type(fdt, node, "BPI", 16);
	} else if (!strcmp(name, "TIVOLI")) {
		prop32 = cpu_to_fdt32(2);
		fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));

		/* INFO: This card needs to do a special command for switching between Flash memories */
		fdt_appendprop(fdt, node, "flags", flag_fb_select_flash, strlen(flag_fb_select_flash)+1);

		nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery",      1, 0, 0, 0x00000000, 0x04000000);
		nfb_fdt_create_binary_slot(fdt, node, "image0", "configuration", 0, 1, 1, 0x00000000, 0x04000000);

		nfb_fdt_create_boot_type(fdt, node, "SPI", 4);
	} else if (!strcmp(name, "NFB-40G2") || !strcmp(name, "NFB-100G")) {
		prop32 = cpu_to_fdt32(1);
		fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));

		nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery"     , 1, 0, 0, 0x00020000, 0x02000000-0x20000);
		nfb_fdt_create_binary_slot(fdt, node, "image0", "configuration", 0, 1, 0, 0x02000000, 0x02000000);

		nfb_fdt_create_boot_type(fdt, node, "BPI", 16);
	} else if (!strcmp(name, "COMBO-400G1")) {
		prop32 = cpu_to_fdt32(2);
		fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));
		prop32 = cpu_to_fdt32(256 * 1024 * 1024);
		fdt_appendprop(fdt, node, "mtd_size", &prop32, sizeof(prop32));
		prop32 = cpu_to_fdt32(28);
		fdt_appendprop(fdt, node, "mtd_bit", &prop32, sizeof(prop32));

		nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery"     , 1, 0, 1, 0x00000000, 0x08000000);
		nfb_fdt_create_binary_slot(fdt, node, "image0", "configuration", 0, 1, 0, 0x00000000, 0x08000000);

		nfb_fdt_create_boot_type(fdt, node, "INTEL-AVST", 0);
	} else if (!strcmp(name, nfb_card_name_generic)) {
		if (!strcmp(card_name, "IA-420F")) {
			prop32 = cpu_to_fdt32(1);
			fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));
			prop32 = cpu_to_fdt32(256 * 1024 * 1024);
			fdt_appendprop(fdt, node, "mtd_size", &prop32, sizeof(prop32));
			prop32 = cpu_to_fdt32(28);
			fdt_appendprop(fdt, node, "mtd_bit", &prop32, sizeof(prop32));

			nfb_fdt_create_binary_slot(fdt, node, "image1", "recovery"     , 1, 0, 0, 0x00210000, 0x02000000-0x210000);
			nfb_fdt_create_binary_slot(fdt, node, "image0", "application0" , 0, 1, 0, 0x02000000, 0x04000000);
			//nfb_fdt_create_boot_type(fdt, node, "BPI", 16);
		} else if (!strcmp(card_name, "N6010")) {
			subnode = nfb_fdt_create_binary_slot(fdt, node, "image2", "fpga_factory", 2, 2, -1, 0, 0);
			n6010_binary_slot_prepare(fdt, subnode, "\x03\x00\x00\x00", "\xff\xff\xff\xff", 4);
			subnode = nfb_fdt_create_binary_slot(fdt, node, "image1", "fpga_user2",   1, 4, -1, 0, 0);
			n6010_binary_slot_prepare(fdt, subnode, "\x00\x00\x01\x00", "\xff\xff\xff\xff", 4);
			subnode = nfb_fdt_create_binary_slot(fdt, node, "image0", "fpga_user1",   0, 3, -1, 0, 0);
			n6010_binary_slot_prepare(fdt, subnode, "\x00\x00\x00\x00", "\xff\xff\xff\xff", 4);
		} else if (!strcmp(card_name, "ALVEO_U200") || !strcmp(card_name, "ALVEO_U250") || !strcmp(card_name, "ALVEO_U55C")) {
			prop32 = cpu_to_fdt32(1);
			fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));
			prop32 = cpu_to_fdt32(128 * 1024 * 1024);
			fdt_appendprop(fdt, node, "mtd_size", &prop32, sizeof(prop32));

			nfb_fdt_create_binary_slot(fdt, node, "image0", "application0" , 0, 0, 0, 0x01002000, 0x04000000);

			nfb_fdt_create_boot_type(fdt, node, "SPI", 4);
		} else if (!strcmp(card_name, "ALVEO_UL3524")) {
			prop32 = cpu_to_fdt32(1);
			fdt_appendprop(fdt, node, "num_flash", &prop32, sizeof(prop32));
			prop32 = cpu_to_fdt32(256 * 1024 * 1024);
			fdt_appendprop(fdt, node, "mtd_size", &prop32, sizeof(prop32));
			nfb_fdt_create_binary_slot(fdt, node, "image0", "application0" , 0, 0, 0, 0x01002000, 0x04000000);

			nfb_fdt_create_boot_type(fdt, node, "SPI", 4);
		}
	}
}
/*
 * nfb_pci_create_fallback_fdt - Create empty DT with MI bus and boot controller for firmware with no DT support
 * @nfb: NFB device
 */
static void nfb_pci_create_fallback_fdt(struct nfb_device *nfb)
{
	int node;

	if (nfb->fdt == NULL) {
		nfb->fdt = kzalloc(NFB_FDT_MAXSIZE, GFP_KERNEL);
		fdt_create_empty_tree(nfb->fdt, NFB_FDT_MAXSIZE);
	}

	node = fdt_node_offset_by_compatible(nfb->fdt, -1, "netcope,bus,mi");
	if (node < 0) {
		node = fdt_path_offset(nfb->fdt, "/firmware");
		if (node < 0) {
			node = fdt_path_offset(nfb->fdt, "/");
			node = fdt_add_subnode(nfb->fdt, node, "firmware");
		}
		node = fdt_add_subnode(nfb->fdt, node, "mi_bus");
		fdt_setprop_string(nfb->fdt, node, "compatible", "netcope,bus,mi");
		fdt_setprop_string(nfb->fdt, node, "resource", "PCI0,BAR0");
	}

	node = fdt_node_offset_by_compatible(nfb->fdt, -1, "netcope,boot_controller");
	if (node < 0 && fallback_fdt_boot) {
		node = fdt_node_offset_by_compatible(nfb->fdt, -1, "netcope,bus,mi");
		node = fdt_add_subnode(nfb->fdt, node, "boot_controller");
		fdt_setprop_string(nfb->fdt, node, "compatible", "netcope,boot_controller");
		fdt_setprop_u64(nfb->fdt, node, "reg", 0x0000200000000008l);
	}
}

/*
 * nfb_pci_find_vsec - locate a VSEC in PCI extended capability space
 * @pci: PCI device
 * @vsec_header: VSEC header
 * return: index in PCI config space
 *
 * VSEC - Vendor Specific PCI header
 */
static int nfb_pci_find_vsec(struct pci_dev *pci, u32 vsec_header)
{
	u32 data;
	int ret;
	int cap_vendor;

	cap_vendor = pci_find_ext_capability(pci, PCI_EXT_CAP_ID_VNDR);
	while (cap_vendor != 0) {
		ret = pci_read_config_dword(pci, cap_vendor + 4, &data);
		if (ret != PCIBIOS_SUCCESSFUL)
			return -ENODEV;

		if (data == vsec_header)
			return cap_vendor;
		cap_vendor = pci_find_next_ext_capability(pci,
				cap_vendor, PCI_EXT_CAP_ID_VNDR);
	}
	return -ENODEV;
}

/*
 * nfb_pci_read_dsn - Read DSN from PCI device
 * @pci: PCI device
 * return: DSN value or 0 when DSN record not exists
 */
static uint64_t nfb_pci_read_dsn(struct pci_dev *pci)
{
	int i;
	int ret;
	u32 reg;
	int cap_dsn;
	int cap_dtb;
	uint64_t dsn = 0;

	cap_dtb = nfb_pci_find_vsec(pci, 0x02010D7B);
	if (cap_dtb >= 0) {
		ret = pci_read_config_dword(pci, cap_dtb + 0x08, &reg);
		/* Check for Card ID capatibility */
		if (ret == PCIBIOS_SUCCESSFUL && reg & 0x40000000) {
			for (i = 0; i < sizeof(dsn) / 4; i++) {
				pci_write_config_dword(pci, cap_dtb + 0x18, i);
				pci_read_config_dword(pci, cap_dtb + 0x1C, ((u32*) &dsn) + i);
			}
			return dsn;
		}
	}

	cap_dsn = pci_find_ext_capability(pci, PCI_EXT_CAP_ID_DSN);
	if (cap_dsn) {
		pci_read_config_dword(pci, cap_dsn+4, ((u32*) &dsn) + 0);
		pci_read_config_dword(pci, cap_dsn+8, ((u32*) &dsn) + 1);
	}
	return dsn;
}

static int nfb_pci_read_enpoint_id(struct pci_dev *pci)
{
	int ret;
	u32 reg;
	int cap_dtb;

	cap_dtb = nfb_pci_find_vsec(pci, 0x02010D7B);
	if (cap_dtb >= 0) {
		ret = pci_read_config_dword(pci, cap_dtb + 0x08, &reg);
		if (ret == PCIBIOS_SUCCESSFUL && reg & 0x80000000) {
			return reg & 0xf;
		}
	}
	return -1;
}

/*
 * nfb_pci_read_fdt - allocate and read FDT from PCI config space
 * @pci: PCI device
 * return: FDT pointer (must be free'd with kfree)
 */
static void *nfb_pci_read_fdt(struct pci_dev *pci)
{
	void * fdt;
	int i;
	int ret;
	int cap_dtb;

	u32 data;
	u32 len;

	enum   xz_ret xzret;
	struct xz_buf buffer;
	struct xz_dec *decoder;

	/* PCI-SIG Vendor-Specific Header:
	 * bits 31-20 = VSEC Length, 19-16 = VSEC Rev, 15-0: VSEC ID
	 * For DTB is: 0x020 & 0x1 & 0x0D7B */
	cap_dtb = nfb_pci_find_vsec(pci, 0x02010D7B);
	if (cap_dtb < 0) {
		ret = -EBADF;
		dev_warn(&pci->dev, "DTB VSEC not found.\n");
		goto err_vsec_not_found;
	}

	/* Read length of compressed DTB in bytes */
	ret = pci_read_config_dword(pci, cap_dtb + 0x0C, &len);
	if (ret != PCIBIOS_SUCCESSFUL || len == 0 || len > NFB_FDT_MAXSIZE) {
		ret = -EBADF;
		dev_err(&pci->dev, "DTB header malformed.\n");
		goto err_vsec_malformed;
	}

	/* Allocate and initialize xz buffer */
	buffer.in_pos = 0;
	buffer.in_size = len;
	buffer.in = kmalloc(len, GFP_KERNEL);
	if (buffer.in == NULL) {
		ret = -ENOMEM;
		goto err_alloc_in_buffer;
	}

	buffer.out_pos = 0;
	buffer.out_size = NFB_FDT_BURSTSIZE;
	buffer.out = kzalloc(buffer.out_size, GFP_KERNEL);
	if (buffer.out == NULL) {
		ret = -ENOMEM;
		goto err_alloc_out_buffer;
	}

	/* Read compressed DTB directly to xz buffer */
	for (i = 0; i < len / sizeof(data); i++) {
		data = i;
		ret = pci_write_config_dword(pci, cap_dtb + 0x10, data);
		if (ret != PCIBIOS_SUCCESSFUL) {
			ret = -EBADF;
			goto err_read;
		}
		ret = pci_read_config_dword(pci, cap_dtb + 0x14, ((u32*)buffer.in) + i);
		if (ret != PCIBIOS_SUCCESSFUL) {
			ret = -EBADF;
			goto err_read;
		}
	}

	/* Initialize a single-call decoder */
	decoder = xz_dec_init(XZ_DYNALLOC, (u32)-1);
	if (decoder == NULL) {
		ret = -ENOMEM;
		goto err_dec_init;
	}

	/* Run DTB decompression */
	buffer.out_size = NFB_FDT_BURSTSIZE / 2;
	buffer.out = NULL;
	do {
		buffer.out_size *= 2;
		buffer.out = krealloc(buffer.out, buffer.out_size, GFP_KERNEL);
		if (buffer.out == NULL) {
			ret = -ENOMEM;
			goto err_dec_run;
		}
		xzret = xz_dec_run(decoder, &buffer);
	} while (xzret == XZ_OK);

	if (xzret != XZ_STREAM_END) {
		ret = -EBADF;
		dev_err(&pci->dev, "Unable to decompress FDT, %d.\n", xzret);
		goto err_dec_run;
	}

	if (fdt_check_header(buffer.out)) {
		ret = -EBADF;
		dev_err(&pci->dev, "FDT check header failed.\n");
		goto err_fdt_check_header;
	}

	/* Increase size for driver usage */
	buffer.out_size *= 4;
	buffer.out = krealloc(buffer.out, buffer.out_size, GFP_KERNEL);
	if (buffer.out == NULL) {
		ret = -ENOMEM;
		goto err_fdt_final_realloc;
	}

	xz_dec_end(decoder);
	kfree(buffer.in);

	fdt = buffer.out;
	dev_info(&pci->dev, "FDT loaded, size: %u, allocated buffer size: %zu\n", fdt_totalsize(fdt), buffer.out_size);
	fdt_set_totalsize(fdt, buffer.out_size);

	return fdt;

err_fdt_final_realloc:
err_fdt_check_header:
err_dec_run:
	xz_dec_end(decoder);
err_dec_init:
err_read:
	if (buffer.out)
		kfree(buffer.out);
err_alloc_out_buffer:
	kfree(buffer.in);
err_alloc_in_buffer:
err_vsec_malformed:
err_vsec_not_found:
	return ERR_PTR(ret);
}

/*
 * nfb_interrupt - interrupt callback for NFB device
 * @irq: interrupt vector
 * @pnfb: pointer to NFB device
 */
irqreturn_t nfb_interrupt(int irq, void *pnfb)
{
	//struct nfb_device *nfb = (struct nfb_device *) pnfb;
	return IRQ_NONE;
}

/*
 * nfb_pci_tuneup - setup PCI communication parameters
 * @pdev: PCI device
 */
static void nfb_pci_tuneup(struct pci_dev *pdev)
{
	struct pci_dev *bus = pdev->bus->self;
	int ret, bus_ecap;
	u16 bus_payload, devctl, dev_allows;

	int exp_cap;

	exp_cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (!exp_cap)
		dev_err(&pdev->dev, "can't find PCIe capability on the card\n");

	ret = pcie_set_readrq(pdev, 4096);
	if (ret)
		dev_err(&pdev->dev, "can't set read request size\n");

	if (!exp_cap)
		return;
	/*
	 * first we check bus payload size, then device capabilities and
	 * choose the lower
	 */
	bus_ecap = pci_find_capability(bus, PCI_CAP_ID_EXP);
	if (!bus_ecap) {
		dev_err(&pdev->dev, "can't find PCIe capability on the bus\n");
		return;
	}
	pci_read_config_word(bus, bus_ecap + PCI_EXP_DEVCTL, &bus_payload);
	bus_payload &= PCI_EXP_DEVCTL_PAYLOAD;

	pci_read_config_word(pdev, exp_cap + PCI_EXP_DEVCTL, &devctl);
	pci_read_config_word(pdev, exp_cap + PCI_EXP_DEVCAP, &dev_allows);
	dev_allows &= PCI_EXP_DEVCAP_PAYLOAD;
	dev_allows <<= 12;
	dev_allows = min_t(u16, bus_payload, dev_allows);

	/* it's already there, bail out */
	if (dev_allows == (devctl & PCI_EXP_DEVCTL_PAYLOAD))
		return;

	devctl &= ~PCI_EXP_DEVCTL_PAYLOAD;
	devctl |= PCI_EXP_DEVCTL_RELAX_EN;
	devctl |= dev_allows;
	pci_write_config_word(pdev, exp_cap + PCI_EXP_DEVCTL, devctl);
}

static int nfb_pci_is_attachable(struct nfb_device *nfb, struct pci_dev* pci)
{
	if (nfb == NULL || pci == NULL)
		return 0;

	/* Device is the same as main */
	if (nfb->pci == pci)
		return 0;

	if (nfb->pci->vendor != pci->vendor)
		return 0;

	if (nfb->nfb_pci_dev == NULL ||
			nfb->nfb_pci_dev->sub_device_id == 0 ||
			nfb->nfb_pci_dev->sub_device_id != pci->device)
		return 0;

	return 1;
}

struct nfb_pci_device *_nfb_pci_device_create(struct pci_dev *pci)
{
	struct nfb_pci_device *pci_device = NULL;

	pci_device = kzalloc(sizeof(*pci_device), GFP_KERNEL);
	if (pci_device == NULL) {
		return NULL;
	}

	INIT_LIST_HEAD(&pci_device->global_pci_device_list);
	INIT_LIST_HEAD(&pci_device->pci_device_list);
	INIT_LIST_HEAD(&pci_device->reload_list);

	strcpy(pci_device->pci_name, pci_name(pci));

	list_add(&pci_device->global_pci_device_list, &global_pci_device_list);

	return pci_device;
}

/*
 * nfb_pci_attach_endpoint - Attach PCI device to NFB device
 * @nfb: NFB device
 * @pci: PCI device
 * @index: Index of PCI device within one NFB device
 * return: struct nfb_pci_device instance pointer or NULL
 */
struct nfb_pci_device *nfb_pci_attach_endpoint(struct nfb_device *nfb, struct pci_dev *pci, int index)
{
	int device_found = 0;
	struct nfb_pci_device *pci_device = NULL;

	//mutex_lock();
	list_for_each_entry(pci_device, &global_pci_device_list, global_pci_device_list) {
		if (strcmp(pci_device->pci_name, pci_name(pci)) == 0) {
			device_found = 1;
			break;
		}
	}

	if (!device_found) {
		pci_device = _nfb_pci_device_create(pci);
		if (pci_device == NULL) {
			return NULL;
		}
	}

	pci_device->pci = pci;
	pci_device->bus = pci->bus;
	pci_device->nfb = nfb;
	pci_device->index = index;

	list_add(&pci_device->pci_device_list, &nfb->pci_devices);

	//mutex_unlock();
	return pci_device;
}

/*
 * nfb_pci_detach_endpoint - Detach PCI device from NFB device
 * @nfb: NFB device
 * @pci: PCI device
 */
void nfb_pci_detach_endpoint(struct nfb_device *nfb, struct pci_dev *pci)
{
	struct nfb_pci_device *pci_device;

	list_for_each_entry(pci_device, &nfb->pci_devices, pci_device_list) {
		if (pci_device->pci == pci) {
			printk("NFB PCI: Detaching endpoint %d: %s\n", pci_device->index, pci_name(pci));
			list_del_init(&pci_device->pci_device_list);

			pci_device->nfb = NULL;
			return;
		}
	}
}

/*
 * nfb_pci_attach_all_slaves - Search PCI bus and attach all slave endpoints of NFB device
 * @nfb: NFB device
 * @bus: PCI bus
 */
void nfb_pci_attach_all_slaves(struct nfb_device *nfb, struct pci_bus *bus)
{
	int ret;
	uint64_t slave_dsn;
	struct pci_bus *child_bus;
	struct pci_dev *slave;

	list_for_each_entry(child_bus, &bus->children, node) {
		nfb_pci_attach_all_slaves(nfb, child_bus);
	}

	list_for_each_entry(slave, &bus->devices, bus_list) {
		/* prevent device create for unintended devices */
		if (!nfb_pci_is_attachable(nfb, slave))
			continue;
		slave_dsn = nfb_pci_read_dsn(slave);
		if (nfb->dsn == slave_dsn) {
			ret = nfb_pci_read_enpoint_id(slave);
			if (ret == -1)
				ret = 1;
			dev_info(&nfb->pci->dev, "Found PCI slave %d device with name %s by DSN\n", ret, pci_name(slave));
			nfb_pci_attach_endpoint(nfb, slave, ret);
		}
	}
}

/*
 * nfb_pci_detach_all_slaves - Detach all slave endpoints from NFB device
 * @nfb: NFB device
 */
void nfb_pci_detach_all_slaves(struct nfb_device *nfb)
{
	struct nfb_pci_device *pci_device, *temp;

	list_for_each_entry_safe(pci_device, temp, &nfb->pci_devices, pci_device_list) {
		if (pci_device->index > 0) {
			nfb_pci_detach_endpoint(nfb, pci_device->pci);
		}
	}
}

static int nfb_pci_probe_base(struct pci_dev *pci)
{
	int ret;
	if (pci_is_root_bus(pci->bus)) {
		dev_err(&pci->dev, "attaching an nfb card to the root PCI bus is not supported\n");
		ret = -EOPNOTSUPP;
		goto err_root_pci_bus;
	}

	ret = pci_enable_device(pci);
	if (ret) {
		dev_err(&pci->dev, "unable to enable PCI device: %d\n", ret);
		goto err_pci_enable_device;
	}
	ret = dma_set_mask(&pci->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pci->dev, "unable to set DMA mask: %d\n", ret);
		goto err_pci_set_dma_mask;
	}
	ret = dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pci->dev, "unable to set DMA consistent mask: %d\n", ret);
		goto err_pci_set_consistent_dma_mask;
	}
	pci_set_master(pci);

	nfb_pci_tuneup(pci);
	return 0;

err_pci_set_consistent_dma_mask:
err_pci_set_dma_mask:
	pci_disable_device(pci);
err_pci_enable_device:
err_root_pci_bus:
	return ret;
}

static int nfb_pci_probe_main(struct pci_dev *pci, const struct pci_device_id *id, void * nfb_dtb_inject);

/*
 * nfb_probe - called when kernel founds new NFB PCI device
 * @pci: PCI device
 * @id: PCI device identification structure
 */
static int nfb_pci_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	int ret = 0;
	void * nfb_dtb_inject;

	ret = nfb_pci_probe_base(pci);
	if (ret)
		goto err_nfb_pci_probe_base;

	nfb_dtb_inject = nfb_dtb_inject_get_pci(pci_name(pci));

	/* Check presence of driver_data parameter */
	ret = nfb_pci_read_enpoint_id(pci);
	if (((struct nfb_pci_dev*) id->driver_data == NULL || ret > 0) && nfb_dtb_inject == NULL) {
		dev_info(&pci->dev, "successfully initialized only for DMA transfers\n");
		return 0;
	}

	ret = nfb_pci_probe_main(pci, id, nfb_dtb_inject);
	return ret;

err_nfb_pci_probe_base:
	return ret;
}

static int nfb_pci_probe_main(struct pci_dev *pci, const struct pci_device_id *id, void * nfb_dtb_inject)
{
	int ret = 0;
	struct pci_bus *bus = NULL;
	struct nfb_pci_device *pci_device;
	struct nfb_device *nfb;

	nfb = nfb_create();
	if (IS_ERR(nfb)) {
		ret = PTR_ERR(nfb);
		goto err_nfb_create;
	}
	nfb->pci = pci;
	nfb->pci_name = nfb_card_name_generic;
	nfb->nfb_pci_dev = (struct nfb_pci_dev*) id->driver_data;
	if (nfb->nfb_pci_dev)
		nfb->pci_name = nfb->nfb_pci_dev->name;

	nfb->dsn = nfb_pci_read_dsn(pci);

	pci_device = nfb_pci_attach_endpoint(nfb, pci, 0);
	if (pci_device == NULL) {
		ret = -1;
		goto err_attach_device;
	}

	while ((bus = pci_find_next_bus(bus))) {
		nfb_pci_attach_all_slaves(nfb, bus);
	}

	/* Initialize interrupts */
	ret = pci_enable_msi(pci);
	if (ret) {
		dev_info(&pci->dev, "unable to enable MSI\n");
		//goto err_pci_enable_msi;
	} else {
		ret = request_irq(pci->irq, nfb_interrupt, IRQF_SHARED, "nfb", nfb);
	}
	if (ret) {
		pci->irq = -1;
	}

	nfb->fdt = nfb_dtb_inject;
	if (nfb->fdt == NULL) {
		/* Populate device tree */
		nfb->fdt = nfb_pci_read_fdt(pci);
	}
	if (IS_ERR(nfb->fdt)) {
		ret = PTR_ERR(nfb->fdt);
		dev_err(&pci->dev, "unable to read firmware description - DTB\n");

		if (!fallback_fdt)
			goto err_nfb_read_fdt;
		else
			nfb->fdt = NULL;
	}

	/* Create fallback or modify existing FDT to support booting */
	if (fallback_fdt)
		nfb_pci_create_fallback_fdt(nfb);

	nfb_fdt_fixups(nfb);

	pci_set_drvdata(pci, nfb);

	/* Publish NFB object */
	ret = nfb_probe(nfb);
	if (ret) {
		goto err_nfb_probe;
	}

	dev_info(&pci->dev, "successfully initialized\n");
	return 0;

	/* Error handling */
err_nfb_probe:
	kfree(nfb->fdt);
err_nfb_read_fdt:
	free_irq(pci->irq, nfb);
	pci_disable_msi(pci);
//err_pci_enable_msi:
err_attach_device:
	nfb_destroy(nfb);
err_nfb_create:
	return ret;
}

/*
 * nfb_remove - called when kernel removes NFB device
 * @pci: PCI device object
 */
void nfb_pci_remove(struct pci_dev *pci)
{
	struct nfb_device *nfb = (struct nfb_device *) pci_get_drvdata(pci);
	if (nfb == NULL)
		goto disable_device;

	/* At first detach all drivers */
	nfb_remove(nfb);
	kfree(nfb->fdt);

	/* Free all mappings */
	if (pci->irq != -1)
		free_irq(pci->irq, nfb);
	pci_disable_msi(pci);
	nfb_destroy(nfb);

disable_device:
	pci_disable_device(pci);
	dev_info(&pci->dev, "disabled\n");
}

static int nfb_pci_sriov_configure(struct pci_dev *dev, int numvfs)
{
	struct nfb_device *nfb = (struct nfb_device *) pci_get_drvdata(dev);
	int i;
	int ret = 0;

	if (numvfs == 0) {
		pci_disable_sriov(dev);
	} else {
		ret = pci_enable_sriov(dev, numvfs);
	}

	if (ret < 0)
		return ret;

	for (i = 0; i < NFB_DRIVERS_MAX; i++) {
		if (nfb->list_drivers[i].status == NFB_DRIVER_STATUS_OK && nfb_registered_drivers[i].numvfs_change) {
			nfb_registered_drivers[i].numvfs_change(nfb->list_drivers[i].priv, numvfs);
		}
	}

	return numvfs;
}

/* Struct for pci_register_driver */
static struct pci_driver nfb_driver = {
	.name = "nfb",
	.id_table = nfb_ids,
	.probe = nfb_pci_probe,
	.remove = nfb_pci_remove,
	.sriov_configure = nfb_pci_sriov_configure,
};

/*
 * nfb_pci_init - PCI submodule init function
 */
int nfb_pci_init(void)
{
	int ret;
	INIT_LIST_HEAD(&global_pci_device_list);
	ret = pci_register_driver(&nfb_driver);
	if (ret)
		goto err_register;

	ret = nfb_dtb_inject_init(&nfb_driver);
	if (ret)
		goto err_inject_init;

	return ret;

	//nfb_dtb_inject_exit();
err_inject_init:
	pci_unregister_driver(&nfb_driver);
err_register:
	return ret;
}

/*
 * nfb_pci_init - PCI submodule exit function
 */
void nfb_pci_exit(void)
{
	struct nfb_pci_device *pci_device, *temp;

	nfb_dtb_inject_exit(&nfb_driver);

	pci_unregister_driver(&nfb_driver);

	list_for_each_entry_safe(pci_device, temp, &global_pci_device_list, global_pci_device_list) {
		list_del(&pci_device->global_pci_device_list);
		kfree(pci_device);
	}
}

module_param(fallback_fdt, bool, S_IRUGO);
MODULE_PARM_DESC(fallback_fdt, "Create fallback FDT or modify existing FDT to support booting [yes]");
module_param(fallback_fdt_boot, bool, S_IRUGO);
MODULE_PARM_DESC(fallback_fdt_boot, "Create boot controller node when creating fallback FDT [no]");
module_param(flash_recovery_ro, bool, S_IRUGO);
MODULE_PARM_DESC(flash_recovery_ro, "Set Flash recovery partition as read-only [yes]");
