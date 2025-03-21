/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver module header of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_BOOT_H
#define NFB_BOOT_H

#include <linux/pci.h>

#include <linux/spi/spi.h>

#include <linux/nfb/boot.h>

#include <uapi/linux/nfb-fpga-image-load.h>

#include "../../spi-nor/spi-nor.h"

#include "../nfb.h"

#include "sdm.h"
#include "nfb-pmci.h"
#include "nfb-spi.h"
#include "nfb-bw-bmc.h"

struct nfb_boot {
	struct nfb_comp *comp;
	struct spi_device *spi;
	struct nfb_device *nfb;
	struct pmci_device *pmci;
	struct m10bmc_spi_nfb_device *m10bmc_spi;

	void *bw_bmc;

	int num_image;

	int num_flash;
	struct map_info *map;
	struct spi_nor *nor;
	struct mtd_info **mtd;
	int flags;
	int controller_type;
	struct sdm *sdm;
	int sdm_boot_en;

	int mtd_bit;
	unsigned long mtd_size;
	int fb_active_flash;
	struct mutex load_mutex;
	struct boot_load {
		unsigned start_ops;
		unsigned done_ops;
		unsigned pending_ops;
		unsigned current_op;
		unsigned current_op_progress_max;
		unsigned current_op_progress;
	} load;
};

struct mtd_info *cfi_probe(struct map_info *map);

int nfb_boot_mtd_init(struct nfb_boot *nfb_boot);
void nfb_boot_mtd_destroy(struct nfb_boot *nfb_boot);
int nfb_boot_mtd_read(struct nfb_boot *nfb_boot, int mtd, int addr, int size, void *data);

int nfb_boot_ioctl_mtd_read(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd);
int nfb_boot_ioctl_mtd_write(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd);

int nfb_boot_ioctl_mtd_erase(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd __user *_ioc_mtd);
int nfb_boot_ioctl_mtd_info(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_mtd_info __user *_ioc_mtd_info);

int nfb_mtd_read(struct nfb_device *dev, int index, size_t addr, void *data, size_t size);

int nfb_boot_init(void);
void nfb_boot_exit(void);
int nfb_boot_attach(struct nfb_device* nfb, void **priv);
void nfb_boot_detach(struct nfb_device* nfb, void *priv);
/*int ndp_char_open(void *priv, void **app_priv, struct file *file);
void ndp_char_release(void *priv, void *app_priv, struct file *file);*/
long nfb_boot_ioctl(void *priv, void * app_priv, struct file *file, unsigned int cmd, unsigned long arg);
int nfb_boot_ioctl_error_disable(struct nfb_boot *nfb_boot);

int nfb_boot_reload(void *arg);

int nfb_boot_get_sensor_ioc(struct nfb_boot *boot, struct nfb_boot_ioc_sensor __user *);
ssize_t nfb_boot_load_get_status(struct nfb_boot *boot, char *buf);

#define NFB_BOOT_FLAG_FB_SELECT_FLASH 1
#define NFB_BOOT_FLAG_FLASH_SET_ASYNC 2

int nfb_fpga_image_load_attach(struct nfb_device *nfb, void **priv);
void nfb_fpga_image_load_detach(struct nfb_device* nfb, void *priv);
long nfb_fpga_image_load_ioctl(void *priv, void * app_priv, struct file *file, unsigned int cmd, unsigned long arg);
int nfb_fpga_image_load_open(void *priv, void **app_priv, struct file *file);
void nfb_fpga_image_load_release(void *priv, void *app_priv, struct file *file);

#endif /* NFB_BOOT_H */
