/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver of the NFB platform - reload module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <asm/io.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/nmi.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <linux/sched.h>
#include <linux/uaccess.h>

#include "boot.h"
#include "../pci.h"

static bool boot_linkdown_enable = 0;
extern struct mutex boot_reload_mutex;


void nfb_boot_mtd_destroy(struct nfb_boot *nfb_boot);

static int nfb_boot_reload_prepare_remove(struct nfb_pci_device *card)
{
	struct pci_dev *pci_dev;
	pci_dev = card->pci;

	card->bus = pci_dev->bus;
	card->devfn = pci_dev->devfn;
	card->cap = pci_find_capability(pci_dev->bus->self, PCI_CAP_ID_EXP);
	if (card->cap == 0) {
		dev_err(&pci_dev->dev, "can't find PCIe capability on the parent bus\n");
		return -EBADF;
	}

	return 0;
}

/* Function of an endpoint's slot that another driver owns (e.g. uio_pci_generic on a P2P BAR) */
struct nfb_boot_sibling {
	struct pci_bus *bus;
	u8 devfn;
	u16 command;
};

#define NFB_BOOT_RELOAD_SIBLINGS_MAX 16

static bool nfb_boot_pci_is_endpoint(struct nfb_device *nfb, struct pci_dev *pci)
{
	struct nfb_pci_device *pd;

	list_for_each_entry(pd, &nfb->pci_devices, pci_device_list) {
		if (pd->pci == pci)
			return true;
	}
	return false;
}

/* Two endpoints in one slot share their foreign functions */
static bool nfb_boot_sibling_is_recorded(const struct nfb_boot_sibling *sibs, int n_sib, struct pci_dev *pci)
{
	int i;

	for (i = 0; i < n_sib; i++) {
		if (sibs[i].bus == pci->bus && sibs[i].devfn == pci->devfn)
			return true;
	}
	return false;
}

/* A reload wipes the BARs of every function of the card; one the nfb driver does not manage would stay unusable until a host reboot. */
static int nfb_boot_reload_record_siblings(struct nfb_device *nfb, struct nfb_pci_device *card,
		struct nfb_boot_sibling *sibs, int n_sib, struct device *mbus_dev)
{
	struct pci_dev *sib;
	int fn;

	for (fn = 0; fn < 8; fn++) {
		if (fn == PCI_FUNC(card->devfn))
			continue;
		sib = pci_get_slot(card->bus, PCI_DEVFN(PCI_SLOT(card->devfn), fn));
		if (sib == NULL)
			continue;
		if (nfb_boot_pci_is_endpoint(nfb, sib) || nfb_boot_sibling_is_recorded(sibs, n_sib, sib)) {
			pci_dev_put(sib);
			continue;
		}
		if (n_sib == NFB_BOOT_RELOAD_SIBLINGS_MAX) {
			dev_warn(mbus_dev, "reload: too many foreign functions, %s keeps its stale BARs\n", pci_name(sib));
		} else {
			sibs[n_sib].bus = card->bus;
			sibs[n_sib].devfn = sib->devfn;
			if (pci_read_config_word(sib, PCI_COMMAND, &sibs[n_sib].command))
				sibs[n_sib].command = 0;
			sibs[n_sib].command &= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
			dev_info(mbus_dev, "reload: will re-enumerate foreign sibling %s (command 0x%04x)\n",
				pci_name(sib), sibs[n_sib].command);
			n_sib++;
		}
		pci_dev_put(sib);
	}
	return n_sib;
}

static void nfb_boot_reload_shutdown(struct nfb_pci_device *card)
{
	u16 reg;

	pci_stop_and_remove_bus_device_locked(card->pci);

	/* turn off PCIe downstream port */
	if (boot_linkdown_enable) {
		pci_read_config_word(card->bus->self, card->cap + PCI_EXP_LNKCTL, &reg);
		reg |= PCI_EXP_LNKCTL_LD;
		pci_write_config_word(card->bus->self, card->cap + PCI_EXP_LNKCTL, reg);
	}
}

static void nfb_boot_reload_linkup(struct nfb_pci_device *card)
{
	u16 reg;

	/* turn on PCIe downstream port */
	if (boot_linkdown_enable) {
		pci_read_config_word(card->bus->self, card->cap + PCI_EXP_LNKCTL, &reg);
		reg &= ~PCI_EXP_LNKCTL_LD;
		pci_write_config_word(card->bus->self, card->cap + PCI_EXP_LNKCTL, reg);
	}
}

static int nfb_boot_reload_rescan(struct nfb_pci_device *card)
{
	struct pci_dev *bus_dev;
	int ext_cap_pos;

	pci_lock_rescan_remove();
	pci_rescan_bus(card->bus->parent);
	pci_unlock_rescan_remove();

	card->pci = pci_get_slot(card->bus, card->devfn);
	if (card->pci == NULL) {
		return -ENODEV;
	}

	bus_dev = card->pci->bus->self;

	dev_info(&bus_dev->dev, "restoring errors on PCI bridge\n");
	pci_write_config_word(bus_dev, PCI_COMMAND, card->bridge_command);
	pci_write_config_word(bus_dev, bus_dev->pcie_cap + PCI_EXP_DEVCTL, card->bridge_devctl);

	ext_cap_pos = pci_find_ext_capability(bus_dev, PCI_EXT_CAP_ID_ERR);
	if (ext_cap_pos) {
		pci_write_config_dword(bus_dev, ext_cap_pos + PCI_ERR_UNCOR_MASK, card->bridge_root_aer_mask);
	}

	pci_dev_put(card->pci);
	return 0;
}

static int nfb_boot_n5014_reload_rescan(struct nfb_pci_device *card) {
	struct pci_dev *root;
	struct pci_dev *bus_dev;
	struct pci_bus *b = NULL;
	int reload_time_ms = 5000;
	int root_ext_cap_pos;

	root = pcie_find_root_port(card->pci);
	if (root == NULL) {
		dev_err(&card->bus->self->dev, "Cannot find PCIE root port!\n");
		return -ENODEV;
	}

	// Stop and remove PCIE bus from root port
	pci_stop_and_remove_bus_device_locked(root);

	// Wait for a while before rescaning the bus
	msleep(reload_time_ms);

	// First try to rescan bus
	pci_lock_rescan_remove();
	while ((b = pci_find_next_bus(b)) != NULL)
		pci_rescan_bus(b);
	pci_unlock_rescan_remove();

	// Wait again for all devices after the PCIe bridge to appear
	msleep(reload_time_ms);

	// Second try to rescan bus
	pci_lock_rescan_remove();
	while ((b = pci_find_next_bus(b)) != NULL)
		pci_rescan_bus(b);
	pci_unlock_rescan_remove();

	card->pci = pci_get_slot(card->bus, card->devfn);
	if (card->pci == NULL) {
		return -ENODEV;
	}

	bus_dev = card->pci->bus->self;
	root = pcie_find_root_port(bus_dev);
	if (root == NULL) {
		dev_err(&card->bus->self->dev, "Cannot find PCIE root port!\n");
		return -ENODEV;
	}

	// Restore errors
	dev_info(&bus_dev->dev, "nfb: restoring errors on PCI bridge\n");
	root_ext_cap_pos = pci_find_ext_capability(root, PCI_EXT_CAP_ID_ERR);
	pci_write_config_word(bus_dev, PCI_COMMAND, card->bridge_command);
	pci_write_config_word(bus_dev, bus_dev->pcie_cap + PCI_EXP_DEVCTL, card->bridge_devctl);
	if (root_ext_cap_pos) {
		pci_write_config_dword(root, root_ext_cap_pos + PCI_ERR_ROOT_COMMAND, card->bridge_root_aer_cmd);
	}

	pci_dev_put(card->pci);

	pr_info("nfb: firmware reload done\n");
	return 0;
}

/*
 * nfb_pci_errors_disable - disable errors that can occur on hot reboot (firmware reload)
 * @card: struct nfb_pci_device instance
 */
static int nfb_pci_errors_disable(struct nfb_pci_device *card, int use_root_port_cmds)
{
	struct pci_dev *bridge = card->bus->self;
	struct pci_dev *root = pcie_find_root_port(bridge);
	int root_ext_cap_pos = 0;
	int ext_cap_pos = 0;

	dev_info(&card->bus->self->dev, "disabling errors on PCI bridge\n");
	if (use_root_port_cmds && root == NULL) {
		dev_err(&card->bus->self->dev, "cannot find PCIe root port!\n");
		return -ENODEV;
	}

	/* save state of error registers */
	pci_read_config_word(bridge, PCI_COMMAND, &card->bridge_command);
	pci_read_config_word(bridge, bridge->pcie_cap + PCI_EXP_DEVCTL, &card->bridge_devctl);

	pci_write_config_word(bridge, PCI_COMMAND, card->bridge_command & ~PCI_COMMAND_SERR);
	pci_write_config_word(bridge, bridge->pcie_cap + PCI_EXP_DEVCTL,
			card->bridge_devctl & ~(PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_FERE));

	ext_cap_pos = pci_find_ext_capability(bridge, PCI_EXT_CAP_ID_ERR);
	if (ext_cap_pos) {
		pci_read_config_dword(bridge, ext_cap_pos + PCI_ERR_UNCOR_MASK, &card->bridge_root_aer_mask);
		pci_write_config_dword(bridge, ext_cap_pos + PCI_ERR_UNCOR_MASK, card->bridge_root_aer_mask | (PCI_ERR_UNC_SURPDN));
	}

	if (use_root_port_cmds) {
		root_ext_cap_pos = pci_find_ext_capability(root, PCI_EXT_CAP_ID_ERR);
		if (root_ext_cap_pos) {
			pci_read_config_dword(root, root_ext_cap_pos + PCI_ERR_ROOT_COMMAND, &card->bridge_root_aer_cmd);
			pci_write_config_dword(root, root_ext_cap_pos + PCI_ERR_ROOT_COMMAND, 0);
		}
	}

	return 0;
}

int nfb_boot_ioctl_error_disable(struct nfb_boot *nfb_boot)
{
	int ret;
	int use_root_port_cmds = 0;
	struct nfb_pci_device *card;

	if (nfb_boot->m10bmc_spi) {
		use_root_port_cmds = 1;
	}

	list_for_each_entry(card, &nfb_boot->nfb->pci_devices, pci_device_list) {
		ret = nfb_pci_errors_disable(card, use_root_port_cmds);
		if (ret)
			return ret;
	}
	return 0;
}

int nfb_boot_reload(void *arg)
{
	int i;
	int ret;
	struct nfb_boot *boot;
	struct nfb_device *nfb;
	struct list_head slaves;
	struct nfb_pci_device *master = NULL;
	struct nfb_pci_device *slave, *temp;
	struct device *mbus_dev;

	struct nfb_boot_sibling sibs[NFB_BOOT_RELOAD_SIBLINGS_MAX];
	struct pci_dev *sib;
	int n_sib = 0;
	u16 command;

	int reload_time_ms;

	boot = arg;
	nfb = boot->nfb;

	mbus_dev = &nfb->pci->bus->self->dev;

	dev_info(mbus_dev, "acquiring mutex for reload on %s\n", pci_name(nfb->pci));
	mutex_lock(&boot_reload_mutex);
	dev_info(mbus_dev, "reloading firmware on %s\n", pci_name(nfb->pci));

	INIT_LIST_HEAD(&slaves);

	/* Remove PCIe slaves */
	list_for_each_entry_safe(slave, temp, &nfb->pci_devices, pci_device_list) {
		if (slave->is_probed_as_main) {
			master = slave;
			continue;
		}

		list_add(&slave->reload_list, &slaves);

		ret = nfb_boot_reload_prepare_remove(slave);
		if (ret)
			goto err_reload_prepare_remove;

	}

	/* Prepare master PCIe for removal */
	ret = nfb_boot_reload_prepare_remove(master);
	if (ret)
		goto err_reload_prepare_remove;

	/* The N5014 path removes and rescans the whole root port, foreign functions included */
	if (!boot->m10bmc_spi) {
		n_sib = nfb_boot_reload_record_siblings(nfb, master, sibs, n_sib, mbus_dev);
		list_for_each_entry(slave, &slaves, reload_list)
			n_sib = nfb_boot_reload_record_siblings(nfb, slave, sibs, n_sib, mbus_dev);
	}

	/* Workaround: Close all MTDs within BootFPGA */
	nfb_boot_mtd_destroy(boot);
	nfb_detach_driver_before_reload(nfb);

	reload_time_ms = 2000;
	/* Send reload-fw command to BootFPGA component */
	if (boot->pmci) {
		boot->pmci->image_load[boot->num_image].load_image(boot->pmci->sec);
		reload_time_ms = 5000;
	} else if (boot->m10bmc_spi) {
		boot->m10bmc_spi->image_load[boot->num_image].load_image(boot->m10bmc_spi->sec);
		ret = nfb_boot_n5014_reload_rescan(master);
		goto alternative_reload_done;
	} else if (boot->bw_bmc) {
		nfb_boot_bw_bmc_reload(boot);
	} else if (boot->sdm && boot->sdm_boot_en) {
		sdm_rsu_image_update(boot->sdm, boot->num_image);
	} else if (boot->comp) {
		if (boot->controller_type == 3) {
			uint64_t cmd = (0x7l << 60) | (7l << 48) | boot->num_image;

			/* HOTFIX: force switch flash */
			nfb_boot_flash_fb_switch_flash(boot, 1);
			nfb_boot_flash_fb_switch_flash(boot, 0);

			nfb_comp_write64(boot->comp, 0, cmd);
		} else {
			nfb_comp_write32(boot->comp, 0, boot->num_image);
			nfb_comp_write32(boot->comp, 4, 0xE0000000);
		}
	} else {
		dev_warn(mbus_dev, "no boot controller on %s\n", pci_name(nfb->pci));
	}

	nfb_boot_reload_shutdown(master);

	/* INFO: Remove slaves AFTER removing master device
	 * Some channels using slave devices for DMA allocs
	 */
	list_for_each_entry(slave, &slaves, reload_list) {
		nfb_boot_reload_shutdown(slave);
	}

	/* Removed like the endpoints, so the rescans below re-enumerate them with fresh BARs */
	for (i = 0; i < n_sib; i++) {
		sib = pci_get_slot(sibs[i].bus, sibs[i].devfn);
		if (sib) {
			dev_info(mbus_dev, "reload: removing foreign sibling %s\n", pci_name(sib));
			pci_stop_and_remove_bus_device_locked(sib);
			pci_dev_put(sib);
		}
	}

	/* Wait some time before FPGA reboots */
	msleep(reload_time_ms);

	nfb_boot_reload_linkup(master);
	list_for_each_entry(slave, &slaves, reload_list) {
		nfb_boot_reload_linkup(slave);
	}

	/*
	 * Wait some time until the Link Up
	 */
	msleep(boot->reload_link_up_time);

	/* Rescan PCIe slaves */
	for (i = 0; i < 2; i++) {
		list_for_each_entry_safe(slave, temp, &slaves, reload_list) {
			/* In first pass skip all secondary endpoints sharing bus parent with master */
			if (i == 0 && slave->bus->parent == master->bus->parent)
				continue;
			ret = nfb_boot_reload_rescan(slave);
			if (ret)
				dev_warn(mbus_dev, "unable to find slave PCI device after FW reload!\n");

			list_del_init(&slave->reload_list);
		}
	}

	ret = nfb_boot_reload_rescan(master);
	if (ret)
		dev_err(mbus_dev, "unable to find master PCI device after FW reload!\n");

	/* Re-enumeration clears the decode and bus-master enables the other driver set; anything a re-probe set stays */
	for (i = 0; i < n_sib; i++) {
		sib = pci_get_slot(sibs[i].bus, sibs[i].devfn);
		if (sib) {
			pci_read_config_word(sib, PCI_COMMAND, &command);
			pci_write_config_word(sib, PCI_COMMAND, command | sibs[i].command);
			dev_info(mbus_dev, "reload: restored foreign sibling %s command 0x%04x\n",
				pci_name(sib), sibs[i].command);
			pci_dev_put(sib);
		} else {
			dev_warn(mbus_dev, "reload: foreign sibling %02x:%02x.%x missing after reload\n",
				sibs[i].bus->number, PCI_SLOT(sibs[i].devfn), PCI_FUNC(sibs[i].devfn));
		}
	}
	dev_info(mbus_dev, "firmware reload done\n");

	ret = 0;
alternative_reload_done:
	mutex_unlock(&boot_reload_mutex);
	return ret;

err_reload_prepare_remove:
	list_for_each_entry_safe(slave, temp, &slaves, reload_list) {
		list_del_init(&slave->reload_list);
	}
	mutex_unlock(&boot_reload_mutex);
	return ret;
}


module_param(boot_linkdown_enable, bool, S_IRUGO);
MODULE_PARM_DESC(boot_linkdown_enable, "Shut the PCIe downstream link down during boot [no]");
