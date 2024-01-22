/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Boot driver module for Intel M10 BMC via SPI core.
 * Support for Silicom N5014.
 *
 * Derived from intel-m10-bmc-spi.c and spi-altera-dfl.c from N5014 DFL drivers.
 * Those files are under GPL 2.0 and are authored respecively by:
 * intel-m10-bmc-spi.c:
 *   Intel MAX 10 Board Management Controller chip
 *   Copyright (C) 2018-2021 Intel Corporation. All rights reserved.
 * spi-altera-dfl.c:
 *   DFL bus driver for Altera SPI Master
 *   Copyright (C) 2020 Intel Corporation, Inc.
 *   Authors:
 *     Matthew Gerlach <matthew.gerlach@linux.intel.com>
 *
 * Copyright (C) 2024 BrnoLogic
 * Author(s):
 *   Vlastimil Kosar <kosar@brnologic.com>
 */

#include <config.h>
#ifdef CONFIG_NFB_ENABLE_PMCI

#include <linux/bitfield.h>
#include <linux/mfd/nfb-intel-m10-bmc.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <linux/iopoll.h>

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "boot.h"
#include "../nfb.h"
#include "../pci.h"
#include "../mi/mi.h"

#include <linux/platform_device.h>

#include <linux/fpga/nfb-fpga-image-load.h>

#include <linux/spi/spi.h>

#include "../../spi/altera.h"
#include "../../base/regmap/regmap.h"

#include "nfb-spi.h"

#define SPI_CORE_PARAMETER      0x8
#define SHIFT_MODE              BIT_ULL(1)
#define SHIFT_MODE_MSB          0
#define SHIFT_MODE_LSB          1
#define DATA_WIDTH              GENMASK_ULL(7, 2)
#define NUM_CHIPSELECT          GENMASK_ULL(13, 8)
#define CLK_POLARITY            BIT_ULL(14)
#define CLK_PHASE               BIT_ULL(15)
#define PERIPHERAL_ID           GENMASK_ULL(47, 32)
#define SPI_CLK                 GENMASK_ULL(31, 22)
#define SPI_INDIRECT_ACC_OFST   0x10

#define INDIRECT_ADDR           (SPI_INDIRECT_ACC_OFST+0x0)
#define INDIRECT_WR             BIT_ULL(8)
#define INDIRECT_RD             BIT_ULL(9)
#define INDIRECT_RD_DATA        (SPI_INDIRECT_ACC_OFST+0x8)
#define INDIRECT_DATA_MASK      GENMASK_ULL(31, 0)
#define INDIRECT_DEBUG          BIT_ULL(32)
#define INDIRECT_WR_DATA        (SPI_INDIRECT_ACC_OFST+0x10)
#define INDIRECT_TIMEOUT        10000

struct indirect_ctx {
	struct device *dev;
	struct nfb_comp *comp;
	off_t offset;
};

static int indirect_bus_reg_read(void *context, unsigned int reg,
				     unsigned int *val)
{
	struct indirect_ctx *ctx = context;
	int loops;
	u32 v;

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_ADDR, (reg >> 2) | INDIRECT_RD);

	loops = 0;
	while ((nfb_comp_read32(ctx->comp, ctx->offset + INDIRECT_ADDR) & INDIRECT_RD) &&
	       (loops++ < INDIRECT_TIMEOUT))
		cpu_relax();

	if (loops >= INDIRECT_TIMEOUT) {
		dev_err(ctx->dev, "%s timed out on reg 0x%x with loops %d\n", __func__, reg, loops);
		return -ETIME;
	}

	v = nfb_comp_read32(ctx->comp, ctx->offset + INDIRECT_RD_DATA);
	*val = v & INDIRECT_DATA_MASK;

	return 0;
}

static int indirect_bus_reg_write(void *context, unsigned int reg,
				      unsigned int val)
{
	struct indirect_ctx *ctx = context;
	int loops;

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_WR_DATA, val);
	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_ADDR, (reg >> 2) | INDIRECT_WR);

	loops = 0;
	while ((nfb_comp_read32(ctx->comp, ctx->offset + INDIRECT_ADDR) & INDIRECT_WR) &&
	       (loops++ < INDIRECT_TIMEOUT))
		cpu_relax();

	if (loops >= INDIRECT_TIMEOUT) {
		dev_err(ctx->dev, "%s timed out on reg 0x%x with loops %d\n", __func__, reg, loops);
		return -ETIME;
	}

	return 0;
}

static struct regmap_config indirect_regbus_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 24,

	.reg_write = indirect_bus_reg_write,
	.reg_read = indirect_bus_reg_read,
};

static void config_spi_host(struct nfb_comp *comp, off_t offset, struct spi_controller *host)
{
	u64 v;

	v = nfb_comp_read64(comp, offset + SPI_CORE_PARAMETER);

	host->mode_bits = SPI_CS_HIGH;
	if (FIELD_GET(CLK_POLARITY, v))
		host->mode_bits |= SPI_CPOL;
	if (FIELD_GET(CLK_PHASE, v))
		host->mode_bits |= SPI_CPHA;

	host->num_chipselect = FIELD_GET(NUM_CHIPSELECT, v);
	host->bits_per_word_mask =
		SPI_BPW_RANGE_MASK(1, FIELD_GET(DATA_WIDTH, v));
}

static struct regmap *nfb_devm_regmap_init_indirect_register(struct device *dev,
		off_t offset, struct nfb_comp *comp, struct regmap_config *cfg)
{
	struct indirect_ctx *ctx;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return NULL;

	ctx->dev = dev;
	ctx->comp = comp;
	ctx->offset = offset;

	return devm_regmap_init(dev, /*&indirect_bus*/NULL, ctx, cfg);
}

static const struct regmap_range m10bmc_spi_regmap_range[] = {
	regmap_reg_range(M10BMC_LEGACY_BUILD_VER, M10BMC_LEGACY_BUILD_VER),
	regmap_reg_range(M10BMC_SYS_BASE, M10BMC_SYS_END),
	regmap_reg_range(M10BMC_FLASH_BASE, M10BMC_FLASH_END),
};

static const struct regmap_access_table m10_access_table = {
	.yes_ranges	= m10bmc_spi_regmap_range,
	.n_yes_ranges	= ARRAY_SIZE(m10bmc_spi_regmap_range),
};

static struct regmap_config m10bmc_spi_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.wr_table = &m10_access_table,
	.rd_table = &m10_access_table,
	.max_register = M10BMC_MEM_END,
};

int nfb_spi_attach(struct nfb_boot *boot)
{
	struct nfb_device *nfb = boot->nfb;
	struct m10bmc_spi_nfb_device *m10bmc_spi;
	struct spi_board_info board_info = { 0 };
	struct spi_controller *host;
	struct altera_spi *hw;
	struct spi_device *spi_dev;

	int fdt_offset;
	int ret = 0;

	m10bmc_spi = kzalloc(sizeof(*m10bmc_spi), GFP_KERNEL);
	if (!m10bmc_spi) {
		ret = -ENOMEM;
		goto err_alloc_spi;
	}

	fdt_offset = fdt_node_offset_by_compatible(nfb->fdt, -1, "brnologic,m10bmc_spi");
	m10bmc_spi->comp = nfb_comp_open(nfb, fdt_offset);
	if (m10bmc_spi->comp == NULL) {
		ret = -ENODEV;
		goto err_nfb_comp;
	}

	m10bmc_spi->pd = platform_device_register_resndata(nfb->dev, "nfb-spi-m10bmc", nfb->minor, NULL, 0, m10bmc_spi, sizeof(m10bmc_spi));
	if (IS_ERR(m10bmc_spi->pd)) {
		ret = PTR_ERR(m10bmc_spi->pd);
		goto err_pd_register;
	}

	m10bmc_spi->m10bmc.dev = &m10bmc_spi->pd->dev;
	m10bmc_spi->m10bmc.type = M10_N5014;
	m10bmc_spi->m10bmc.flash_ops = NULL;

    host = spi_alloc_master(&m10bmc_spi->pd->dev, sizeof(struct altera_spi));
	if (!host) {
		ret = -ENOMEM;
		goto err_alloc_master;
	}


	host->bus_num = -1;

	hw = spi_controller_get_devdata(host);

	hw->dev = &m10bmc_spi->pd->dev;

	config_spi_host(m10bmc_spi->comp, 0, host);
	dev_dbg(&m10bmc_spi->pd->dev, "%s cs %u bpm 0x%x mode 0x%x\n", __func__,
		host->num_chipselect, host->bits_per_word_mask,
		host->mode_bits);

	hw->regmap = nfb_devm_regmap_init_indirect_register(&m10bmc_spi->pd->dev,
			0, m10bmc_spi->comp,
			&indirect_regbus_cfg);
	if (IS_ERR(hw->regmap)) {
		ret = PTR_ERR(hw->regmap);
		goto err_regmap_register;
	}

	hw->irq = -EINVAL;
#ifdef CONFIG_HAVE_SPI_INIT_MASTER
	altera_spi_init_master(host);
#else
	altera_spi_init_host(host);
#endif
	ret = devm_spi_register_controller(&m10bmc_spi->pd->dev, host);
	if (ret) {
		dev_err(&m10bmc_spi->pd->dev, "%s failed to register spi host %d\n", __func__, ret);
		goto err_spi_register_ctrl;
	}

	strscpy(board_info.modalias, "nfb-m10-n5014", SPI_NAME_SIZE);
	board_info.max_speed_hz = 12500000;
	board_info.bus_num = 0;
	board_info.chip_select = 0;

	spi_dev = spi_new_device(host, &board_info);
	if (!spi_dev) {
		dev_err(&m10bmc_spi->pd->dev, "%s failed to create SPI device: %s\n",
			__func__, board_info.modalias);
	}

	m10bmc_spi->m10bmc.regmap = devm_regmap_init_spi_avmm(spi_dev, &m10bmc_spi_regmap_config);
	if (IS_ERR(m10bmc_spi->m10bmc.regmap)) {
		ret = PTR_ERR(m10bmc_spi->m10bmc.regmap);
		dev_err(&m10bmc_spi->pd->dev, "%s Failed to allocate regmap: %d\n", __func__, ret);
		goto err_regmap_register_avmm;
	}

	ret = devm_device_add_groups(&m10bmc_spi->pd->dev, nfb_m10bmc_dev_groups);
	if (ret)
		goto err_dev_addgroups;

	msleep(10);

	ret = m10bmc_dev_init(&m10bmc_spi->m10bmc);
	if (ret)
		goto err_m10bmc_init;

	m10bmc_spi->host = host;
	boot->m10bmc_spi = m10bmc_spi;
	return ret;

err_spi_register_ctrl:
err_regmap_register:
	spi_controller_put(host);
err_m10bmc_init:
err_dev_addgroups:
err_regmap_register_avmm:
err_alloc_master:
	platform_device_unregister(m10bmc_spi->pd);
err_pd_register:
	nfb_comp_close(m10bmc_spi->comp);
err_nfb_comp:
	kfree(m10bmc_spi);
err_alloc_spi:
	return ret;
}

void nfb_spi_detach(struct nfb_boot *boot)
{
	struct m10bmc_spi_nfb_device *m10bmc_spi = boot->m10bmc_spi;
	if (m10bmc_spi == NULL)
		return;

	platform_device_unregister(m10bmc_spi->pd);

	nfb_comp_close(m10bmc_spi->comp);
	kfree(m10bmc_spi);
	boot->m10bmc_spi = NULL;
}

extern struct platform_driver nfb_intel_m10bmc_sec_driver;
extern struct platform_driver nfb_intel_m10bmc_hwmon_driver;

struct platform_driver nfb_intel_m10bmc_spi = {
	.driver = {
		.name = "nfb-spi-m10bmc",
	},
};

static int nfb_intel_m10_bmc_spi_probe(struct spi_device *spi)
{
	int ret = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0) && RHEL_RELEASE_CODE < 0x803
	struct device *dev = &spi->dev;
	ret = device_add_groups(dev, nfb_m10bmc_dev_groups);
	if (ret)
		return ret;
#endif
	return ret;
}

static const struct spi_device_id nfb_m10bmc_spi_id[] = {
// 	{ "m10-n3000", M10_N3000 },
// 	{ "m10-d5005", M10_D5005 },
// 	{ "m10-n5010", M10_N5010 },
	{ "nfb-m10-n5014", M10_N5014 },
	{ }
};
MODULE_DEVICE_TABLE(spi, nfb_m10bmc_spi_id);

static struct spi_driver nfb_intel_m10bmc_spi_dev = {
	.driver = {
		.name = "nfb-intel-m10-bmc-spi-dev",
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0) || RHEL_RELEASE_CODE >= 0x803
		.dev_groups = nfb_m10bmc_dev_groups,
#endif
	},
	.probe = nfb_intel_m10_bmc_spi_probe,
	.id_table = nfb_m10bmc_spi_id,
};

int nfb_spi_init()
{
	int ret;

	if (driver_find(nfb_intel_m10bmc_sec_driver.driver.name, &platform_bus_type) == NULL) {
		ret = platform_driver_register(&nfb_intel_m10bmc_sec_driver);
		if (ret)
			goto err_sec;
	}

	if (driver_find(nfb_intel_m10bmc_hwmon_driver.driver.name, &platform_bus_type) == NULL) {
		ret = platform_driver_register(&nfb_intel_m10bmc_hwmon_driver);
		if (ret)
			goto err_hwmon;
	}

	if (driver_find(nfb_intel_m10bmc_spi_dev.driver.name, &spi_bus_type) == NULL) {
		ret = spi_register_driver(&nfb_intel_m10bmc_spi_dev);
		if (ret)
			goto err_spi_driver;
	}

	if (driver_find(nfb_intel_m10bmc_spi.driver.name, &platform_bus_type) == NULL) {
		ret = platform_driver_register(&nfb_intel_m10bmc_spi);
		if (ret)
			goto err_pdr;
	}

	return 0;

err_pdr:
	spi_unregister_driver(&nfb_intel_m10bmc_spi_dev);
err_spi_driver:
	platform_driver_unregister(&nfb_intel_m10bmc_hwmon_driver);
err_hwmon:
	platform_driver_unregister(&nfb_intel_m10bmc_sec_driver);
err_sec:
	return ret;

}

void nfb_spi_exit()
{
	if (driver_find(nfb_intel_m10bmc_spi.driver.name, &platform_bus_type) != NULL) {
		platform_driver_unregister(&nfb_intel_m10bmc_spi);
	}
	if (driver_find(nfb_intel_m10bmc_spi_dev.driver.name, &spi_bus_type) != NULL) {
		spi_unregister_driver(&nfb_intel_m10bmc_spi_dev);
    }
	if (driver_find(nfb_intel_m10bmc_hwmon_driver.driver.name, &platform_bus_type) != NULL) {
		platform_driver_unregister(&nfb_intel_m10bmc_hwmon_driver);
	}
	if (driver_find(nfb_intel_m10bmc_sec_driver.driver.name, &platform_bus_type) != NULL) {
		platform_driver_unregister(&nfb_intel_m10bmc_sec_driver);
	}
}

#endif
