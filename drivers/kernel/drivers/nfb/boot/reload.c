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

	pci_dev_put(card->pci);
	return 0;
}

/*
 * nfb_pci_errors_disable - disable errors that can occur on hot reboot (firmware reload)
 * @card: struct nfb_pci_device instance
 */
static int nfb_pci_errors_disable(struct nfb_pci_device *card)
{
	struct pci_dev *bridge = card->bus->self;
	dev_info(&card->bus->self->dev, "disabling errors on PCI bridge\n");

	/* save state of error registers */
	pci_read_config_word(bridge, PCI_COMMAND, &card->bridge_command);
	pci_read_config_word(bridge, bridge->pcie_cap + PCI_EXP_DEVCTL, &card->bridge_devctl);

	pci_write_config_word(bridge, PCI_COMMAND, card->bridge_command & ~PCI_COMMAND_SERR);
	pci_write_config_word(bridge, bridge->pcie_cap + PCI_EXP_DEVCTL,
			card->bridge_devctl & ~(PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_FERE));
	return 0;
}

int nfb_boot_ioctl_error_disable(struct nfb_boot *nfb_boot)
{
	int ret;
	struct nfb_pci_device *card;

	list_for_each_entry(card, &nfb_boot->nfb->pci_devices, pci_device_list) {
		ret = nfb_pci_errors_disable(card);
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

	int reload_time_ms;

	boot = arg;
	nfb = boot->nfb;

	mbus_dev = &nfb->pci->bus->self->dev;

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

	/* Workaround: Close all MTDs within BootFPGA */
	nfb_boot_mtd_destroy(boot);

	reload_time_ms = 2000;
	/* Send reload-fw command to BootFPGA component */
	if (boot->pmci) {
		boot->pmci->image_load[boot->num_image].load_image(boot->pmci->sec);
		reload_time_ms = 5000;
	} else if (boot->m10bmc_spi) {
		boot->m10bmc_spi->image_load[boot->num_image].load_image(boot->m10bmc_spi->sec);
		reload_time_ms = 5000;
	} else if (boot->sdm && boot->sdm_boot_en) {
		sdm_rsu_image_update(boot->sdm, boot->num_image);
	} else if (boot->comp) {
		if (boot->controller_type == 3) {
			uint64_t cmd = (0x7l << 60) | (7l << 48) | boot->num_image;
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

	/* Wait some time before FPGA reboots */
	msleep(reload_time_ms);

	nfb_boot_reload_linkup(master);
	list_for_each_entry(slave, &slaves, reload_list) {
		nfb_boot_reload_linkup(slave);
	}

	/*
	 * Wait some time until the Link Up
	 */
	msleep(600);

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
	dev_info(mbus_dev, "firmware reload done\n");

	return 0;

err_reload_prepare_remove:
	list_for_each_entry_safe(slave, temp, &slaves, reload_list) {
		list_del_init(&slave->reload_list);
	}
	return ret;
}


module_param(boot_linkdown_enable, bool, S_IRUGO);
MODULE_PARM_DESC(boot_linkdown_enable, "Shut the PCIe downstream link down during boot [yes]");
