/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver of the NFB platform - flash module
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
#include <linux/xz.h>

#include <linux/sched.h>
#include <linux/uaccess.h>

#include "../../cfi/map.h"
#include "../../cfi/mtd.h"

#include "../../spi/spi-xilinx.h"

#include "boot.h"
#include "../nfb.h"

#include "sdm.h"

inline uint16_t nfb_boot_flash_read16(struct nfb_boot *boot)
{
	int i = 0;
	int ret;

	while (1) {
		ret = nfb_comp_read32(boot->comp, 0);
		if (ret & 0x00010000)
			break;
		if (++i > 50) {
			dev_err(&boot->nfb->pci->dev, "Flash is not ready. "
				"Is a BootFPGA unit in design?\n");
			break;
		}
	}
	return ret;
}

void nfb_boot_flash_fb_switch_flash(struct nfb_boot *boot, int flash)
{
	if (boot->fb_active_flash != flash) {
		boot->fb_active_flash = flash;

		/* Gecko on Tivoli has different command set */
		if (boot->controller_type == 3) {
			uint64_t cmd = (0x7l << 60) | ((flash ? 0x04l : 0x03l) << 48);
			nfb_comp_write64(boot->comp, 0, cmd);
			/* FIXME: Sync - use something like nfb_boot_flash_read16 */
			udelay(10000);
		} else {
			nfb_comp_write32(boot->comp, 0, flash ? 0x6 : 0x2);
			nfb_comp_write32(boot->comp, 4, 0xD0000000);

			/* Workaround for Mango: Perform a read to ensure that the switch has finished */
			nfb_boot_flash_read16(boot);
		}

		dev_info(&boot->nfb->pci->dev, "Flash switched to %d", flash);
	}
}

static void nfb_boot_flash_set_async(struct map_info *map)
{
	struct nfb_boot *boot = (struct nfb_boot *) map->map_priv_1;
	int flash = map->map_priv_2;

	unsigned long addr = 0;

	if (boot->mtd_bit >= 0)
		addr |= (flash << boot->mtd_bit);
	addr >>= 1;

	if (boot->flags & NFB_BOOT_FLAG_FB_SELECT_FLASH)
		nfb_boot_flash_fb_switch_flash(boot, flash);

	nfb_comp_write32(boot->comp, 0, 0x60);
	nfb_comp_write32(boot->comp, 4, (0xf847) | 0x20000000 | addr);
	nfb_boot_flash_read16(boot);
	nfb_comp_write32(boot->comp, 0, 0x3);
	nfb_comp_write32(boot->comp, 4, (0xf847) | 0x20000000 | addr);
	nfb_boot_flash_read16(boot);
}

static map_word nfb_boot_flash_read(struct map_info *map, unsigned long addr)
{
	struct nfb_boot *boot = (struct nfb_boot *) map->map_priv_1;
	int flash = map->map_priv_2;

	int data;
	map_word tmp;
	tmp.x[0] = 0;

	if (boot->mtd_bit >= 0)
		addr |= (flash << boot->mtd_bit);
	addr >>= 1;

	if (boot->flags & NFB_BOOT_FLAG_FB_SELECT_FLASH)
		nfb_boot_flash_fb_switch_flash(boot, flash);

	nfb_comp_write32(boot->comp, 0, 0);
	nfb_comp_write32(boot->comp, 4, (addr & 0x0FFFFFFF) | 0x10000000);
	data = nfb_boot_flash_read16(boot);
	tmp.x[0] = data;
	return tmp;
}

static void nfb_boot_flash_write(struct map_info *map, map_word d, unsigned long addr)
{
	struct nfb_boot *boot = (struct nfb_boot *) map->map_priv_1;
	int flash = map->map_priv_2;

	if (boot->mtd_bit >= 0)
		addr |= (flash << boot->mtd_bit);
	addr >>= 1;

	if (boot->flags & NFB_BOOT_FLAG_FB_SELECT_FLASH)
		nfb_boot_flash_fb_switch_flash(boot, flash);

	nfb_comp_write32(boot->comp, 0, d.x[0] & 0xFFFF);
	nfb_comp_write32(boot->comp, 4, (addr & 0x0FFFFFFF) | 0x20000000);
	nfb_boot_flash_read16(boot);
}

static void nfb_boot_flash_copy_from(struct map_info *map, void *to,
	unsigned long from, ssize_t len)
{
	int i;
	map_word tmp;
	for (i = 0; i < (len >> 1); i++) {
		tmp = nfb_boot_flash_read(map, from + (i << 1));
		((uint16_t*)to)[i] = tmp.x[0];
	}
}

int nfb_boot_ioctl_mtd_info(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd_info __user *_ioc_mtd_info)
{
	struct nfb_boot_ioc_mtd_info ioc_mtd_info;
	int res = 0;
	if (copy_from_user(&ioc_mtd_info, _ioc_mtd_info, sizeof(ioc_mtd_info)))
		return -EFAULT;

	if (ioc_mtd_info.mtd >= nfb_boot->num_flash || !nfb_boot->mtd[ioc_mtd_info.mtd])
		return -ENODEV;

	ioc_mtd_info.size = nfb_boot->mtd[ioc_mtd_info.mtd]->size;
	ioc_mtd_info.erasesize = nfb_boot->mtd[ioc_mtd_info.mtd]->erasesize;
	if (copy_to_user(_ioc_mtd_info, &ioc_mtd_info, sizeof(ioc_mtd_info)))
		return -EFAULT;

	return res;
}

int nfb_boot_ioctl_mtd_erase(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd)
{
	struct nfb_boot_ioc_mtd ioc_mtd;
	struct erase_info ei;

	if (copy_from_user(&ioc_mtd, _ioc_mtd, sizeof(ioc_mtd)))
		return -EFAULT;

	if (ioc_mtd.mtd >= nfb_boot->num_flash || !nfb_boot->mtd[ioc_mtd.mtd])
		return -ENODEV;

	ei.callback = 0;
	ei.mtd = nfb_boot->mtd[ioc_mtd.mtd];
	ei.addr = ioc_mtd.addr;
	ei.len = ioc_mtd.size;

	mtd_unlock(ei.mtd, ei.addr, ei.len);
	return mtd_erase(ei.mtd, &ei);
}

int nfb_boot_ioctl_mtd_write(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd)
{
	struct nfb_boot_ioc_mtd ioc_mtd;
	void *data;
	size_t ret_size;
	int res = 0;

	if (copy_from_user(&ioc_mtd, _ioc_mtd, sizeof(ioc_mtd)))
		return -EFAULT;

	data = vmalloc(ioc_mtd.size);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, ioc_mtd.data, ioc_mtd.size)) {
		vfree(data);
		return -EFAULT;
	}

	if (ioc_mtd.mtd >= nfb_boot->num_flash || !nfb_boot->mtd[ioc_mtd.mtd])
		return -ENODEV;

	res = mtd_write(nfb_boot->mtd[ioc_mtd.mtd], ioc_mtd.addr,
			ioc_mtd.size, &ret_size, data);
	vfree(data);
	return res;
}

int nfb_mtd_read(struct nfb_device *dev, int index, size_t addr, void *data, size_t size)
{
	struct nfb_boot *nfb_boot;
	size_t ret_size;
	int ret;

	nfb_boot = nfb_get_priv_for_attach_fn(dev, nfb_boot_attach);

	if (IS_ERR(nfb_boot)) {
		return PTR_ERR(nfb_boot);
	}

	if (index >= nfb_boot->num_flash || !nfb_boot->mtd[index])
		return -ENODEV;

	ret = mtd_read(nfb_boot->mtd[index], addr, size, &ret_size, data);
	if (ret)
		return ret;

	if (ret_size != size)
		return -ENOMEM;

	return 0;
}

int nfb_boot_ioctl_mtd_read(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd)
{
	struct nfb_boot_ioc_mtd ioc_mtd;
	void *data;
	size_t ret_size;
	int res = 0;

	if (copy_from_user(&ioc_mtd, _ioc_mtd, sizeof(ioc_mtd)))
		return -EFAULT;

	data = vmalloc(ioc_mtd.size);
	if (data == NULL)
		return -ENOMEM;

	if (ioc_mtd.mtd >= nfb_boot->num_flash || !nfb_boot->mtd[ioc_mtd.mtd])
		return -ENODEV;

	res = mtd_read(nfb_boot->mtd[ioc_mtd.mtd], ioc_mtd.addr, ioc_mtd.size, &ret_size, data);

	if (copy_to_user(ioc_mtd.data, data, ioc_mtd.size)) {
		vfree(data);
		return -EFAULT;
	}

	vfree(data);
	return res;
}

int nfb_boot_mtd_read(struct nfb_boot *nfb_boot, int mtd, int addr, int size, void *data)
{
	int ret;
	size_t ret_size;

	if (mtd < 0 || mtd >= nfb_boot->num_flash)
		return -ENODEV;

	if (nfb_boot->mtd[mtd] == NULL)
		return -ENODEV;

	ret = mtd_read(nfb_boot->mtd[mtd], addr, size, &ret_size, data);

	return ret;
}

/* FIXME: Dynamic name table */
const char *nfb_boot_mtd_names[] = {
	"nfb_flash0",
	"nfb_flash1",
};

static void axi_qspi_transfer_store_addr(uint8_t *buf, loff_t addr, int addr_width)
{
	if (addr_width == 4) {
		buf[0] = addr >> 24;
		buf[1] = addr >> 16;
		buf[2] = addr >> 8;
		buf[3] = addr >> 0;
	} else if (addr_width == 3) {
		buf[0] = addr >> 16;
		buf[1] = addr >> 8;
		buf[2] = addr >> 0;
	}
}

static ssize_t axi_qspi_transfer(struct spi_nor *nor, uint8_t opcode, loff_t addr,
		int addr_width, int rx_dummy, size_t len, u_char *rx_buf, const u_char *tx_buf)
{
	ssize_t ret;

	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;
	struct spi_transfer t;

	uint8_t *rx;
	uint8_t *tx;

	if (boot->flags & NFB_BOOT_FLAG_FB_SELECT_FLASH)
		nfb_boot_flash_fb_switch_flash(boot, nor - boot->nor);

	t.len = 1 + addr_width + rx_dummy + len;

	rx = vmalloc(t.len * 2);
	if (!rx)
		return 0;
	tx = rx + t.len;

	t.rx_buf = rx;
	t.tx_buf = tx;

	tx[0] = opcode;
	axi_qspi_transfer_store_addr(&tx[1], addr, addr_width);

	if (tx_buf)
		memcpy(tx + 1 + addr_width, tx_buf, len);
	memset(rx, 0, t.len);

	if (1 || NFB_IS_TIVOLI(boot->nfb)) {
		ret = xilinx_spi_txrx_bufs_continuous(boot->spi, &t);
	} else {
		xilinx_spi_chipselect(boot->spi, 1);
		ret = xilinx_spi_txrx_bufs(boot->spi, &t);
		xilinx_spi_chipselect(boot->spi, 0);
	}

	if (rx_buf)
		memcpy(rx_buf, rx + 1 + addr_width + rx_dummy, len);
	vfree(rx);

	return ret;
}

static int axi_qspi_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	axi_qspi_transfer(nor, opcode, 0, 0, 0, len, buf, NULL);
	return 0;
}

static int axi_qspi_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	if (NFB_IS_TIVOLI(boot->nfb)) {
		if (opcode == SPINOR_OP_EN4B) {
			return 0;
		}
	}

	axi_qspi_transfer(nor, opcode, 0, 0, 0, len, NULL, buf);
	return 0;
}


static ssize_t axi_qspi_read(struct spi_nor *nor, loff_t from, size_t len, u_char *buf)
{
	ssize_t ret;
	int addr_width = nor->addr_width;
	int read_dummy = nor->read_dummy;

	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	if (NFB_IS_TIVOLI(boot->nfb)) {
		uint8_t segment = from / (16*1024*1024);
		addr_width = 3;
		read_dummy = 4;
		axi_qspi_write_reg(nor, SPINOR_OP_WREN, 0, 0);
		axi_qspi_write_reg(nor, SPINOR_OP_WREAR, (uint8_t*)&segment, 1);
		ret = axi_qspi_transfer(nor, SPINOR_OP_READ_1_1_4, from, addr_width, read_dummy, len, buf, NULL);
	} else {
		ret = axi_qspi_transfer(nor, nor->read_opcode, from, nor->addr_width, nor->read_dummy, len, buf, NULL);
	}
	if (ret <= 0)
		return ret;

	return max_t(ssize_t, 0, ret - 1 - addr_width - read_dummy);
}

static ssize_t axi_qspi_write(struct spi_nor *nor, loff_t to, size_t len, const u_char *buf)
{
	ssize_t ret;
	int addr_width = nor->addr_width;

	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	if (NFB_IS_TIVOLI(boot->nfb)) {
		uint8_t segment = to / (16 * 1024 * 1024);
		addr_width = 3;
		axi_qspi_write_reg(nor, SPINOR_OP_WREAR, (uint8_t*)&segment, 1);
		axi_qspi_write_reg(nor, SPINOR_OP_WREN, 0, 0);
		ret = axi_qspi_transfer(nor, SPINOR_OP_PP_1_1_4, to, 3, 0, len, NULL, buf);
	} else {
		ret = axi_qspi_transfer(nor, nor->program_opcode, to, addr_width, 0, len, NULL, buf);
	}
	if (ret <= 0)
		return ret;
	return max_t(ssize_t, 0, ret - 1 - addr_width);
}

static int axi_qspi_erase(struct spi_nor *nor, loff_t off)
{
	int addr_width = nor->addr_width;
	struct nfb_boot *boot = (struct nfb_boot *)nor->priv;

	if (NFB_IS_TIVOLI(boot->nfb)) {
		uint8_t segment = off / (16*1024*1024);
		addr_width = 3;
		axi_qspi_write_reg(nor, SPINOR_OP_WREN, 0, 0);
		axi_qspi_write_reg(nor, SPINOR_OP_WREAR, (uint8_t*)&segment, 1);
	}
	axi_qspi_write_reg(nor, SPINOR_OP_WREN, 0, 0);
	axi_qspi_transfer(nor, SPINOR_OP_SE, off, addr_width, 0, 0, 0, 0);
	return 0;
}

int nfb_boot_mtd_init(struct nfb_boot *nfb_boot)
{
	int i;
	int ret;

	struct spi_nor *nor;

	/* INFO: QUAD INPUT FAST PROGRAM doesn't work */
	const struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_PP
	};

	nfb_boot->fb_active_flash = -1;

	if (nfb_boot->spi || (nfb_boot->sdm && nfb_boot->sdm_boot_en)) {
		nfb_boot->nor = kzalloc(sizeof(struct spi_nor) * nfb_boot->num_flash, GFP_KERNEL);
		if (nfb_boot->nor == NULL) {
			return -ENOMEM;
		}
	} else {
		nfb_boot->map = kzalloc(sizeof(struct map_info) * nfb_boot->num_flash, GFP_KERNEL);
		if (nfb_boot->map == NULL) {
			return -ENOMEM;
		}
	}

	nfb_boot->mtd = kzalloc(sizeof(struct mtd_info*) * nfb_boot->num_flash, GFP_KERNEL);
	if (nfb_boot->mtd == NULL) {
		kfree(nfb_boot->map);
		return -ENOMEM;
	}

	/* TODO: Check for flash_names size */
	for (i = 0; i < nfb_boot->num_flash; i++) {
		/* If exist QSPI controller for Flash, use it */
		if (nfb_boot->spi || (nfb_boot->sdm && nfb_boot->sdm_boot_en)) {
			nor = &nfb_boot->nor[i];

			nor->dev = &nfb_boot->nfb->pci->dev;
			nor->priv = nfb_boot;

			if (nfb_boot->sdm && nfb_boot->sdm_boot_en) {
				nor->prepare = sdm_qspi_prepare;
				nor->unprepare = sdm_qspi_unprepare;
				nor->read = sdm_qspi_read;
				nor->read_reg = sdm_qspi_read_reg;
				nor->write = sdm_qspi_write;
				nor->write_reg = sdm_qspi_write_reg;
				nor->erase = sdm_qspi_erase;
				nor->mtd.name = "sdm_qspi_nor";
			} else {
				nor->mtd.name = "axi_qspi_nor";

				nor->read = axi_qspi_read;
				nor->read_reg = axi_qspi_read_reg;
				nor->write = axi_qspi_write;
				nor->write_reg = axi_qspi_write_reg;

				if (NFB_IS_TIVOLI(nfb_boot->nfb)) {
					nor->erase = axi_qspi_erase;

					/* Workaround for HW bug? Dummy read */
					axi_qspi_read_reg(nor, SPINOR_OP_RDFSR, (uint8_t*)&ret, 1);
				}

				axi_qspi_read_reg(nor, SPINOR_OP_RDFSR, (uint8_t*)&ret, 1);
			}

			ret = spi_nor_scan(nor, NULL, &hwcaps);
			if (ret) {
				dev_err(&nfb_boot->nfb->pci->dev, "Map probe failed for spi_nor: %d\n", ret);
			} else {
				nfb_boot->mtd[i] = &nor->mtd;
			}
		} else {
			nfb_boot->map[i].bankwidth = 2;
			nfb_boot->map[i].name = nfb_boot_mtd_names[i];
			nfb_boot->map[i].size = nfb_boot->mtd_size;
			nfb_boot->map[i].read = nfb_boot_flash_read;
			nfb_boot->map[i].write = nfb_boot_flash_write;
			nfb_boot->map[i].copy_from = nfb_boot_flash_copy_from;
			nfb_boot->map[i].map_priv_1 = (long) nfb_boot;
			nfb_boot->map[i].map_priv_2 = i;

			if (nfb_boot->flags & NFB_BOOT_FLAG_FLASH_SET_ASYNC) {
				nfb_boot_flash_set_async(&nfb_boot->map[i]);
			}

			nfb_boot->mtd[i] = cfi_probe(&nfb_boot->map[i]);

			if (nfb_boot->mtd[i] == NULL) {
				dev_err(&nfb_boot->nfb->pci->dev, "Map probe failed for flash%d\n", i);
			}
		}
	}

	return 0;
}

void nfb_boot_mtd_destroy(struct nfb_boot *nfb_boot)
{
	/* INFO: This function is called in boot.c and in reload.c,
	 *       so is called twice. Beware of memory freeing! */

	int i;

	if (nfb_boot->mtd) {
		for (i = 0; i < nfb_boot->num_flash; i++) {
			if (nfb_boot->nor) {
				/* INFO: Nothing to do with NOR flash */
			} else if (nfb_boot->mtd[i]) {
				map_destroy(nfb_boot->mtd[i]);
				nfb_boot->mtd[i] = NULL;
			}
		}
		kfree(nfb_boot->mtd);
		nfb_boot->mtd = NULL;
	}

	if (nfb_boot->nor) {
		kfree(nfb_boot->nor);
		nfb_boot->nor = NULL;
	}

	if (nfb_boot->map) {
		kfree(nfb_boot->map);
		nfb_boot->map = NULL;
	}

	/* FB1CGG family workaround: for proper reboot select first Flash */
	if (nfb_boot->flags & NFB_BOOT_FLAG_FB_SELECT_FLASH)
		nfb_boot_flash_fb_switch_flash(nfb_boot, 0);
}
