/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver module header of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <asm/io.h>
#include <linux/fs.h>
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

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/sched.h>
#include <linux/uaccess.h>

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/list.h>

#include <asm/atomic.h>

#include "../../spi/spi-xilinx.h"

#include "gecko.h"
#include "boot.h"
#include "sdm.h"
#include "../nfb.h"
#include "../pci.h"

static bool boot_enable = 1;

extern const struct nfb_pci_dev nfb_device_infos [];

/*
 * Read serial number from the Flash on the card and store it as nfb->serial
 *
 * Wraps card-specific functionality how to achieve this.
 */
static void nfb_boot_read_serial_number(struct nfb_device *nfb, struct nfb_boot *boot)
{
	uint32_t data;
	int ret;

	/* some cards don't have MTD or they don't support reading serial numbers */
	if (nfb->nfb_pci_dev == NULL || nfb->nfb_pci_dev->idstruct_mtd == -1 ||
			nfb->nfb_pci_dev->idstruct_serialno_addr == -1)
		return;

	nfb->serial = 0;
	ret = nfb_boot_mtd_read(boot, nfb->nfb_pci_dev->idstruct_mtd,
			nfb->nfb_pci_dev->idstruct_serialno_addr, sizeof(data), &data);
	if (ret == 0 && data != 0xFFFFFFFF) {
		if (NFB_IS_SILICOM(nfb)) {
			nfb->serial = data >> 20;
		} else {
			nfb->serial = be32_to_cpu(data);
		}
	}
}
/* These constants are in decimal format */
#define FB_TYPE_FB2CGG3 33
#define FB_TYPE_FB4CGG3 25

/*
 * Read card type from the Flash on the card and set appropriate nfb->nfb_pci_dev
 *
 * Wraps card-specific functionality how to achieve this.
 */
static void nfb_boot_read_card_subtype(struct nfb_device *nfb, struct nfb_boot *boot)
{
	uint32_t data;
	int ret;
	int card_id = 0;
	int i;

	/* some cards don't have and don't support reading subtype */
	if (nfb->nfb_pci_dev == NULL || nfb->nfb_pci_dev->idstruct_mtd == -1 ||
			nfb->nfb_pci_dev->idstruct_subtype_addr == -1)
		return;

	ret = nfb_boot_mtd_read(boot, nfb->nfb_pci_dev->idstruct_mtd,
			nfb->nfb_pci_dev->idstruct_subtype_addr, sizeof(data), &data);
	if (ret == 0) {
		if (NFB_IS_SILICOM(nfb)) {
			switch((data & 0x00FF0000) >> 16) {
				case FB_TYPE_FB2CGG3:
					card_id = NFB_CARD_FB2CGG3;
					break;
				case FB_TYPE_FB4CGG3:
					card_id = NFB_CARD_FB4CGG3;
					break;
				default:
					break;
			}
		} else {
			data = be32_to_cpu(data) & 0xFFFF;
			i = 0;
			while (nfb_device_infos[i].name != 0) {
				if (nfb_device_infos[i].card_type_id == data) {
					card_id = i;
					//nfb->nfb_pci_dev = &nfb_device_infos[i];
					//dev_info(&nfb->pci->dev, "Changing device to: %s\n", nfb_device_infos[i].name);
					break;
				}
				i++;
			}
		}

		if (card_id > 0) {
			nfb->nfb_pci_dev = &nfb_device_infos[card_id];
			dev_info(&nfb->pci->dev, "Changing device to: %s\n", nfb_device_infos[card_id].name);
		}
	} else {
		dev_warn(&nfb->pci->dev, "Cannot read card type from Flash\n");
	}
}

static long nfb_boot_ioctl_reload(struct nfb_boot *boot, int * __user _image)
{
	int image;
	int node;
	int fdt_offset = -1;

	int proplen;
	const fdt32_t *prop;

	if (get_user(image, _image))
		return -EFAULT;

	if (!boot_enable)
		return -EPERM;

	if (pci_num_vf(boot->nfb->pci)) {
		dev_err(&boot->nfb->pci->dev, "Trying to reload design with enabled SRIOV functions.\n");
		return -EBUSY;
	}

	/* Check image number is valid */
	fdt_for_each_compatible_node(boot->nfb->fdt, node, "netcope,binary_slot") {
		prop = fdt_getprop(boot->nfb->fdt, node, "boot_id", &proplen);
		if (proplen != sizeof(*prop))
			continue;

		if (fdt32_to_cpu(*prop) == image) {
			fdt_offset = node;
			node = fdt_subnode_offset(boot->nfb->fdt, node, "control-param");
			prop = fdt_getprop(boot->nfb->fdt, node, "base", &proplen);
			break;
		}
	}

	if (fdt_offset < 0)
		return -ENODEV;

	boot->num_image = image;

	/* overwrite boot->num_image (boot_id) with the actual address of the image (for the purpose of RSU) */
	if (boot->sdm && boot->sdm_boot_en && proplen == sizeof(*prop)) {
		boot->num_image = fdt32_to_cpu(*prop);
	}

	return nfb_char_set_lr_callback(boot->nfb, nfb_boot_reload, boot);
}

int nfb_boot_get_sensor_ioc(struct nfb_boot *boot, struct nfb_boot_ioc_sensor __user *_ioc_sensor)
{
	int ret;
	int32_t temperature;
	struct nfb_boot_ioc_sensor ioc_sensor;
	struct sdm *sdm = boot->sdm;

	if (_ioc_sensor == NULL)
		return -EINVAL;

	if (copy_from_user(&ioc_sensor, _ioc_sensor, sizeof(ioc_sensor)))
		return -EFAULT;

	/* Currently only temperature sensor through SDM is implemented */
	if (sdm == NULL)
		return -ENODEV;

	ret = sdm_get_temperature(sdm, &temperature);
	if (ret)
		return ret;

	/* temperature in millicelsius */
	ioc_sensor.value = temperature * 1000 / 256;

	if (copy_to_user(_ioc_sensor, &ioc_sensor, sizeof(ioc_sensor)))
		return -EFAULT;

	return 0;
}

long nfb_boot_ioctl(void *priv, void *app_priv, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct nfb_boot *nfb_boot = priv;

	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case NFB_BOOT_IOC_RELOAD:
		if (!(file->f_flags & O_APPEND))
			return -EBUSY;
		return nfb_boot_ioctl_reload(nfb_boot, argp);
	case NFB_BOOT_IOC_ERRORS_DISABLE:
		return nfb_boot_ioctl_error_disable(nfb_boot);
	case NFB_BOOT_IOC_MTD_INFO:
		return nfb_boot_ioctl_mtd_info(nfb_boot, argp);
	case NFB_BOOT_IOC_MTD_READ:
		return nfb_boot_ioctl_mtd_read(nfb_boot, argp);
	case NFB_BOOT_IOC_MTD_WRITE:
		return nfb_boot_ioctl_mtd_write(nfb_boot, argp);
	case NFB_BOOT_IOC_MTD_ERASE:
		return nfb_boot_ioctl_mtd_erase(nfb_boot, argp);
	case NFB_BOOT_IOC_SENSOR_READ:
		return nfb_boot_get_sensor_ioc(nfb_boot, argp);
	default:
		return -ENOTTY;
	}
}

int nfb_boot_attach(struct nfb_device *nfb, void **priv)
{
	int ret = 0;
	int fdt_offset;
	int len;
	const void *prop;
	const uint32_t *prop32;
	struct nfb_boot *boot;
#ifdef CONFIG_HAVE_SPI_CONTROLLER
	struct spi_controller *spi_master;
#else
	struct spi_master *spi_master;
#endif

	fdt_offset = fdt_path_offset(nfb->fdt, "/");
	fdt_offset = fdt_add_subnode(nfb->fdt, fdt_offset, "board");
	if (nfb->dsn)
		fdt_setprop_u64(nfb->fdt, fdt_offset, "fpga-uid", nfb->dsn);

	boot = kzalloc(sizeof(*boot), GFP_KERNEL);
	if (boot == NULL) {
		ret = -ENOMEM;
		goto err_kmalloc;
	}
	*priv = boot;

	boot->nfb = nfb;
#ifdef CONFIG_NFB_ENABLE_PMCI
	ret = nfb_pmci_attach(boot);
	ret = nfb_spi_attach(boot);
#endif
	/* Cards with Intel FPGA (Stratix10, Agilex) use Secure Device Manager for QSPI Flash access and Boot */
	boot->sdm = NULL;
	boot->sdm_boot_en = 0;
	fdt_offset = fdt_node_offset_by_compatible(nfb->fdt, -1, "netcope,intel_sdm_controller");
	if (fdt_offset >= 0) {
		boot->sdm = sdm_init(nfb, fdt_offset, nfb->pci_name);

		prop32 = fdt_getprop(nfb->fdt, fdt_offset, "boot_en", &len);
		if (boot->sdm && len == sizeof(*prop32) && fdt32_to_cpu(*prop32) != 0) {
			boot->sdm_boot_en = 1;
		}
	}

	/* Tivoli card has separate QSPI controller for Flash access */
	fdt_offset = fdt_node_offset_by_compatible(nfb->fdt, -1, "xlnx,axi-quad-spi");
	spi_master = nfb_xilinx_spi_probe(nfb, fdt_offset);
	if (IS_ERR(spi_master))
		boot->spi = NULL;
	else
		boot->spi = spi_alloc_device(spi_master);

	fdt_offset = fdt_node_offset_by_compatible(nfb->fdt, -1, "netcope,boot_controller");
	// FIXME: better create some general boot controller interface
	if (fdt_offset < 0) {
		if (boot->sdm_boot_en == 0 && boot->pmci == NULL && boot->m10bmc_spi == NULL) {
			ret = -ENODEV;
			dev_warn(&nfb->pci->dev, "nfb_boot: No boot_controller found in FDT.\n");
			goto err_nocomp;
		} else {
			fdt_offset = fdt_node_offset_by_compatible(nfb->fdt, -1, "netcope,intel_sdm_controller");
		}
	}

	boot->comp = nfb_comp_open(nfb, fdt_offset);
	if (!boot->comp && boot->pmci == NULL && boot->m10bmc_spi == NULL) {
		ret = -ENODEV;
		goto err_comp_open;
	}

	prop32 = fdt_getprop(nfb->fdt, fdt_offset, "num_flash", &len);
	if (len == sizeof(*prop32))
		boot->num_flash = fdt32_to_cpu(*prop32);

	boot->mtd_bit = -1;
	prop32 = fdt_getprop(nfb->fdt, fdt_offset, "mtd_bit", &len);
	if (len == sizeof(*prop32))
		boot->mtd_bit = fdt32_to_cpu(*prop32);

	boot->mtd_size = 64 * 1024 * 1024;
	prop32 = fdt_getprop(nfb->fdt, fdt_offset, "mtd_size", &len);
	if (len == sizeof(*prop32))
		boot->mtd_size = fdt32_to_cpu(*prop32);

	prop32 = fdt_getprop(nfb->fdt, fdt_offset, "type", &len);
	if (len == sizeof(*prop32))
		boot->controller_type = fdt32_to_cpu(*prop32);

	boot->flags = 0;
	prop = fdt_getprop(nfb->fdt, fdt_offset, "flags", &len);
	if (prop) {
		if (fdt_stringlist_contains(prop, len, "fb_select_flash"))
			boot->flags |= NFB_BOOT_FLAG_FB_SELECT_FLASH;
		if (fdt_stringlist_contains(prop, len, "flash_set_async"))
			boot->flags |= NFB_BOOT_FLAG_FLASH_SET_ASYNC;
	}

	nfb_boot_mtd_init(boot);

	if (NFB_IS_TIVOLI(nfb)) {
		/* Read card properties from Gecko memory (Tivoli platform) */
		/* Read serial number from Gecko */
		nfb_boot_gecko_read_serial_number(nfb, boot->comp);
		/* Read card type from Gecko */
		nfb_boot_gecko_read_card_type(nfb, boot->comp);
	} else {
		/* Read card properties from Flash using MTD */
		/* Read serial number from Flash ID struct */
		nfb_boot_read_serial_number(nfb, boot);
		/* Look for exact card type in Flash only for some cards */
		nfb_boot_read_card_subtype(nfb, boot);
	}

	/* Backward compatibility with firmware, which doesn't have card-name property in DT */
	fdt_offset = fdt_path_offset(nfb->fdt, "/firmware");
	if (fdt_getprop(nfb->fdt, fdt_offset, "card-name", &len) == NULL)
		fdt_setprop_string(nfb->fdt, fdt_offset, "card-name", nfb->pci_name);

	fdt_offset = fdt_path_offset(nfb->fdt, "/board");
	fdt_setprop_string(nfb->fdt, fdt_offset, "board-name", nfb->pci_name);
	if (nfb->serial_str) {
		fdt_setprop_string(nfb->fdt, fdt_offset, "serial-number-string", nfb->serial_str);
	} else {
		fdt_setprop_u32(nfb->fdt, fdt_offset, "serial-number", nfb->serial);
	}

	dev_info(&nfb->pci->dev, "nfb_boot: Attached successfully\n");

	return 0;

err_comp_open:
err_nocomp:
#ifdef CONFIG_NFB_ENABLE_PMCI
	nfb_pmci_detach(boot);
	nfb_spi_detach(boot);
#endif
	kfree(boot);
err_kmalloc:
	return ret;
}

void nfb_boot_detach(struct nfb_device* nfb, void *priv)
{
	struct nfb_boot *boot = priv;
#ifdef CONFIG_HAVE_SPI_CONTROLLER
	struct spi_controller *master;
#else
	struct spi_master *master;
#endif

	nfb_boot_mtd_destroy(boot);

	if (boot->spi) {
		master = boot->spi->controller;
		spi_dev_put(boot->spi);
		nfb_xilinx_spi_remove(master);
	}

	if (boot->comp)
		nfb_comp_close(boot->comp);
	sdm_free(boot->sdm);
#ifdef CONFIG_NFB_ENABLE_PMCI
	if (boot->pmci)
		nfb_pmci_detach(boot);
	if (boot->m10bmc_spi)
		nfb_spi_detach(boot);
#endif
	kfree(boot);
}

int nfb_boot_init()
{
#ifdef CONFIG_NFB_ENABLE_PMCI
    int ret;
	ret = nfb_pmci_init();
    if (ret)
        goto err_pmci;
    ret = nfb_spi_init();
err_pmci:
    return ret;
#else
	return 0;
#endif
}

void nfb_boot_exit()
{
#ifdef CONFIG_NFB_ENABLE_PMCI
	nfb_pmci_exit();
	nfb_spi_exit();
#endif
}

module_param(boot_enable, bool, S_IRUGO);
MODULE_PARM_DESC(boot_enable, "Enable boot (design reload) [yes]");
