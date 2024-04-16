/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI driver module header of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_PCI_H
#define NFB_PCI_H

#define PCI_VENDOR_ID_CESNET            0x18ec
#define PCI_VENDOR_ID_NETCOPE           0x1b26
#define PCI_VENDOR_ID_REFLEXCES         0x1bad
#define PCI_VENDOR_ID_FIBERBLAZE        0x1c2c

enum nfb_devices {
	NFB_CARD_NFB40G2,
	NFB_CARD_NFB100G,
	NFB_CARD_NFB40G2_SG3,
	NFB_CARD_NFB40G,
	NFB_CARD_NFB100G2,
	NFB_CARD_NFB100G2Q,
	NFB_CARD_NFB100G2C,
	NFB_CARD_NFB200G2QL,
	NFB_CARD_FB1CGG,
	NFB_CARD_FB2CGG3,
	NFB_CARD_FB4CGG3,
	NFB_CARD_TIVOLI,
	NFB_CARD_COMBO_GENERIC,
	NFB_CARD_COMBO400G1,
	NFB_CARD_AGI_FH400G,
};
/*
 * struct nfb_pci_dev - Device-specific information
 * @name: Device name ("NFB-200G2QL" etc.)
 * @idstruct_mtd: ID of the MTD where board specific information can be found
 * @idstruct_serialno_addr: Address (in the MTD) where card serial number can be found
 * @idstruct_subtype_addr: Address (in the MTD) where card type can be found
 * @card_type_id: Card type number
 * @sub_device_id: Device ID for subsidiary device
 *
 * This structure is associated with PCI ID (through
 * &struct pci_device_id->driver_data) and embedded in &struct nfb_device to provide
 * device-specific information.
 *
 * 'idstruct' is a specific structure in the Flash (MTD) where board specific
 * information like serial number or MAC addresses are stored
 */
struct nfb_pci_dev {
	const char *name;
	int idstruct_mtd;
	size_t idstruct_serialno_addr;
	size_t idstruct_subtype_addr;
	int card_type_id;
	unsigned short sub_device_id;
};

struct nfb_pci_device {
	struct list_head global_pci_device_list;
	struct list_head pci_device_list;
	struct list_head reload_list;
	struct mutex attach_lock;

	struct pci_dev *pci;
	struct nfb_device *nfb;
	struct pci_bus *bus;

	uint64_t dsn;
	int index;                      /* 0: main device, > 0: subsidiary devices */
	int index_valid: 1;             /* index (and dsn) is valid: freshly readen */
	int is_probed_as_main: 1;
	int is_probed_as_sub: 1;

	int devfn;
	int cap;

	char pci_name[32];

	uint16_t bridge_command;
	uint16_t bridge_devctl;
};

void nfb_pci_detach_endpoint(struct nfb_device *nfb, struct pci_dev *pci);
struct nfb_pci_device *nfb_pci_attach_endpoint(struct nfb_device *nfb, struct nfb_pci_device *pci_device, int index);
void nfb_pci_attach_all_slaves(struct nfb_device *nfb, struct pci_bus *bus);
void nfb_pci_detach_all_slaves(struct nfb_device *nfb);

#endif // NFB_PCI_H
