/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver module for Intel M10 BMC
 *
 * Copyright (C) 2023 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
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

#include <linux/ctype.h>

#include "boot.h"
#include "../nfb.h"
#include "../pci.h"
#include "../mi/mi.h"

#include <linux/platform_device.h>

#include <linux/fpga/nfb-fpga-image-load.h>

#include "nfb-pmci.h"

#define INDIRECT_CMD_OFF	0x0
#define INDIRECT_CMD_RD	BIT(0)
#define INDIRECT_CMD_WR	BIT(1)
#define INDIRECT_CMD_ACK	BIT(2)

#define INDIRECT_ADDR_OFF	0x4
#define INDIRECT_RD_OFF	0x8
#define INDIRECT_WR_OFF	0xc

#define INDIRECT_INT_US	1
#define INDIRECT_TIMEOUT_US	10000


#define M10BMC_PMCI_INDIRECT_BASE 0x400

#define PMCI_FLASH_CTRL 0x40
#define PMCI_FLASH_WR_MODE BIT(0)
#define PMCI_FLASH_RD_MODE BIT(1)
#define PMCI_FLASH_BUSY    BIT(2)
#define PMCI_FLASH_FIFO_SPACE GENMASK(13, 4)
#define PMCI_FLASH_READ_COUNT GENMASK(25, 16)

#define PMCI_FLASH_INT_US       1
#define PMCI_FLASH_TIMEOUT_US   10000

#define PMCI_FLASH_ADDR 0x44
#define PMCI_FLASH_FIFO 0x800
#define PMCI_READ_BLOCK_SIZE 0x800
#define PMCI_FIFO_MAX_BYTES 0x800
#define PMCI_FIFO_MAX_WORDS (PMCI_FIFO_MAX_BYTES / 4)


#if !defined(CONFIG_HAVE_READ_POLL_TIMEOUT_ATOMIC)
#define read_poll_timeout_atomic read_poll_timeout
#endif

struct indirect_ctx {
	struct device *dev;
	struct nfb_comp *comp;
	off_t offset;
};

static int indirect_bus_clr_cmd(struct indirect_ctx *ctx)
{
	unsigned int cmd;
	int ret;

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_CMD_OFF, 0);

	ret = read_poll_timeout_atomic(nfb_comp_read32, cmd, (!cmd), INDIRECT_INT_US, INDIRECT_TIMEOUT_US, false, ctx->comp, (ctx->offset + INDIRECT_CMD_OFF));

	if (ret)
		dev_err(ctx->dev, "%s timed out on clearing cmd 0x%xn", __func__, cmd);

	return ret;
}

static int indirect_bus_reg_read(void *context, unsigned int reg,
				     unsigned int *val)
{
	struct indirect_ctx *ctx = context;
	unsigned int cmd;
	int ret;

	cmd = nfb_comp_read32(ctx->comp, ctx->offset + INDIRECT_CMD_OFF);

	if (cmd)
		dev_warn(ctx->dev, "%s non-zero cmd 0x%x\n", __func__, cmd);

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_ADDR_OFF, reg);

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_CMD_OFF, INDIRECT_CMD_RD);

	ret = read_poll_timeout_atomic(nfb_comp_read32, cmd, (cmd & INDIRECT_CMD_ACK), INDIRECT_INT_US, INDIRECT_TIMEOUT_US, false, ctx->comp, (ctx->offset + INDIRECT_CMD_OFF));

	*val = nfb_comp_read32(ctx->comp, ctx->offset + INDIRECT_RD_OFF);

	if (ret)
		dev_err(ctx->dev, "%s timed out on reg 0x%x cmd 0x%x\n", __func__, reg, cmd);

	if (indirect_bus_clr_cmd(ctx))
		ret = -ETIME;

	return ret;
}

static int indirect_bus_reg_write(void *context, unsigned int reg,
				      unsigned int val)
{
	struct indirect_ctx *ctx = context;
	unsigned int cmd;
	int ret;

	cmd = nfb_comp_read32(ctx->comp, ctx->offset + INDIRECT_CMD_OFF);

	if (cmd)
		dev_warn(ctx->dev, "%s non-zero cmd 0x%x\n", __func__, cmd);

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_WR_OFF, val);

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_ADDR_OFF, reg);

	nfb_comp_write32(ctx->comp, ctx->offset + INDIRECT_CMD_OFF, INDIRECT_CMD_WR);

	ret = read_poll_timeout_atomic(nfb_comp_read32, cmd, (cmd & INDIRECT_CMD_ACK), INDIRECT_INT_US, INDIRECT_TIMEOUT_US, false, ctx->comp, (ctx->offset + INDIRECT_CMD_OFF));

	if (ret)
		dev_err(ctx->dev, "%s timed out on reg 0x%x cmd 0x%x\n", __func__, reg, cmd);

	if (indirect_bus_clr_cmd(ctx))
		ret = -ETIME;

	return ret;
}

static const struct regmap_bus indirect_bus = {
	.fast_io = true,
	.reg_write = indirect_bus_reg_write,
	.reg_read =  indirect_bus_reg_read,
};

struct regmap *nfb_devm_regmap_init_indirect_register(struct device *dev,
		off_t offset, struct nfb_comp *comp, struct regmap_config *cfg)
{
	struct indirect_ctx *ctx;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return NULL;

	ctx->dev = dev;
	ctx->comp = comp;
	ctx->offset = offset;

	return devm_regmap_init(dev, &indirect_bus, ctx, cfg);
}

static void pmci_write_fifo(struct nfb_comp *comp, off_t base, char *buf, size_t count)
{
       int i;
       u32 val;

       for (i = 0; i < count/4; i++) {
		val = *(u32 *)(buf + i * 4);
		nfb_comp_write32(comp, base, val);
       }
}

static void pmci_read_fifo(struct nfb_comp *comp, off_t base, char *buf, size_t count)
{
       int i;
       u32 val;

       for (i = 0; i < count/4; i++) {
	       val = nfb_comp_read32(comp, base);
               *(u32 *)(buf + i * 4) = val;
       }
}

static u32
pmci_get_write_space(struct pmci_device *pmci, u32 size)
{
	u32 count, val;
	int ret;

	ret = read_poll_timeout_atomic(nfb_comp_read32, val,
				FIELD_GET(PMCI_FLASH_FIFO_SPACE, val) ==
				PMCI_FIFO_MAX_WORDS,
				PMCI_FLASH_INT_US, PMCI_FLASH_TIMEOUT_US,
				false, pmci->comp, PMCI_FLASH_CTRL);
	if (ret == -ETIMEDOUT)
		return 0;

	count = FIELD_GET(PMCI_FLASH_FIFO_SPACE, val) * 4;

	return (size > count) ? count : size;
}

static int
pmci_flash_bulk_write(struct intel_m10bmc *m10bmc, void *buf, u32 size)
{
	struct pmci_device *pmci = container_of(m10bmc, struct pmci_device, m10bmc);
	u32 blk_size, n_offset = 0;

	while (size) {
		blk_size = pmci_get_write_space(pmci, size);
		if (blk_size == 0) {
			dev_err(m10bmc->dev, "get FIFO available size fail\n");
			return -EIO;
		}
		size -= blk_size;
		pmci_write_fifo(pmci->comp, PMCI_FLASH_FIFO, buf + n_offset, blk_size);
		n_offset += blk_size;
	}

	return 0;
}

static int
pmci_flash_bulk_read(struct intel_m10bmc *m10bmc, void *buf,
		     u32 addr, u32 size)
{
	struct pmci_device *pmci = container_of(m10bmc, struct pmci_device, m10bmc);
	u32 blk_size, offset = 0, val;
	int ret;

	if (!IS_ALIGNED(addr, 4))
		return -EINVAL;

	while (size) {
		blk_size = min_t(u32, size, PMCI_READ_BLOCK_SIZE);

		nfb_comp_write32(pmci->comp, PMCI_FLASH_ADDR, addr + offset);

		nfb_comp_write32(pmci->comp, PMCI_FLASH_CTRL, FIELD_PREP(PMCI_FLASH_READ_COUNT, blk_size / 4) | PMCI_FLASH_RD_MODE);

		/* First check of PMCI_FLASH_CTRL reg is too soon after write and doesn't have valid PMCI_FLASH_BUSY flag: */
		nfb_comp_read32(pmci->comp, PMCI_FLASH_ADDR);

		ret = read_poll_timeout_atomic(nfb_comp_read32, val,
				!(val & PMCI_FLASH_BUSY),
				PMCI_FLASH_INT_US, PMCI_FLASH_TIMEOUT_US,
				false, pmci->comp, PMCI_FLASH_CTRL);

		if (ret) {
			dev_err(m10bmc->dev, "%s timed out on reading flash 0x%xn",
				__func__, val);
			return ret;
		}

		pmci_read_fifo(pmci->comp, PMCI_FLASH_FIFO, buf + offset, blk_size);

		size -= blk_size;
		offset += blk_size;

		nfb_comp_write32(pmci->comp, PMCI_FLASH_CTRL, 0);
	}


	return 0;
}

static const struct regmap_range m10bmc_pmci_regmap_range[] = {
	regmap_reg_range(M10BMC_PMCI_SYS_BASE, M10BMC_PMCI_SYS_END),
};

static const struct regmap_access_table m10_access_table = {
	.yes_ranges	= m10bmc_pmci_regmap_range,
	.n_yes_ranges	= ARRAY_SIZE(m10bmc_pmci_regmap_range),
};

static struct regmap_config m10bmc_pmci_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.wr_table = &m10_access_table,
	.rd_table = &m10_access_table,
	.max_register = M10BMC_PMCI_SYS_END,
};

int nfb_pmci_attach(struct nfb_boot *boot)
{
	char * pmci_bom_info, *snc, *snc_end;

	struct nfb_device *nfb = boot->nfb;
	struct pmci_device *pmci;

	int fdt_offset;
	int ret = 0;
	int sn;

	pmci = kzalloc(sizeof(*pmci), GFP_KERNEL);
	if (!pmci) {
		ret = -ENOMEM;
		goto err_alloc_pmci;
	}

	fdt_offset = fdt_node_offset_by_compatible(nfb->fdt, -1, "cesnet,pmci");
	pmci->comp = nfb_comp_open(nfb, fdt_offset);
	if (pmci->comp == NULL) {
		ret = -ENODEV;
		goto err_nfb_comp;
	}

	pmci->pd = platform_device_register_resndata(nfb->dev, "nfb-pmci-m10bmc", nfb->minor, NULL, 0, pmci, sizeof(pmci));
	if (IS_ERR(pmci->pd)) {
		ret = PTR_ERR(pmci->pd);
		goto err_pd_register;
	}

	pmci->flash_ops.read_blk = pmci_flash_bulk_read;
	pmci->flash_ops.write_blk = pmci_flash_bulk_write;
	mutex_init(&pmci->flash_ops.mux_lock);

	pmci->m10bmc.dev = &pmci->pd->dev;
	pmci->m10bmc.type = M10_N6000;
	pmci->m10bmc.flash_ops = &pmci->flash_ops;

	pmci->m10bmc.regmap = nfb_devm_regmap_init_indirect_register(&pmci->pd->dev,
			M10BMC_PMCI_INDIRECT_BASE, pmci->comp,
			&m10bmc_pmci_regmap_config);
	if (IS_ERR(pmci->m10bmc.regmap)) {
		ret = PTR_ERR(pmci->m10bmc.regmap);
		goto err_regmap_register;
	}

	ret = devm_device_add_groups(&pmci->pd->dev, nfb_m10bmc_dev_groups);
	if (ret)
		goto err_dev_addgroups;

	ret = m10bmc_dev_init(&pmci->m10bmc);
	if (ret)
		goto err_m10bmc_init;

	boot->pmci = pmci;

	/* Read SN */
	if (pmci->m10bmc.ops.flash_read) {
		pmci_bom_info = kzalloc(PMCI_BOM_INFO_SIZE+1+1, GFP_KERNEL);
		if (pmci_bom_info) {
			pmci_bom_info[0] = '\n';
			pmci_bom_info[PMCI_BOM_INFO_SIZE+1] = 0;
			pmci->m10bmc.ops.flash_read(&pmci->m10bmc, pmci_bom_info+1, PMCI_BOM_INFO_ADDR, PMCI_BOM_INFO_SIZE);
			if ((snc = strstr(pmci_bom_info, "\nSN,"))) {
				if (sscanf(snc, "\nSN,%d\n", &sn) == 1) {
					boot->nfb->serial = sn;
				} else {
					snc_end = strstr(snc+1, "\n");
					if (snc_end) {
						snc_end[0] = 0;
						snc += strlen("\nSN,");
						while (isspace(snc[0]))
							snc += 1;
						boot->nfb->serial_str = kstrdup(snc, GFP_KERNEL);
					}
				}
			}
			kfree(pmci_bom_info);
		}
	}
	return ret;

err_m10bmc_init:
err_dev_addgroups:
err_regmap_register:
	mutex_destroy(&pmci->m10bmc.flash_ops->mux_lock);
	platform_device_unregister(pmci->pd);
err_pd_register:
	nfb_comp_close(pmci->comp);
err_nfb_comp:
	kfree(pmci);
err_alloc_pmci:
	return ret;
}

void nfb_pmci_detach(struct nfb_boot *boot)
{
	struct pmci_device *pmci = boot->pmci;
	if (pmci == NULL)
		return;

	platform_device_unregister(pmci->pd);

	mutex_destroy(&pmci->m10bmc.flash_ops->mux_lock);
	nfb_comp_close(pmci->comp);
	kfree(pmci);
}

extern struct platform_driver nfb_intel_m10bmc_sec_driver;
extern struct platform_driver nfb_intel_m10bmc_hwmon_driver;
extern struct platform_driver nfb_intel_m10bmc_log_driver;

struct platform_driver nfb_intel_m10bmc = {
	.driver = {
		.name = "nfb-pmci-m10bmc",
	},
};

int nfb_pmci_init()
{
	int ret;
	ret = platform_driver_register(&nfb_intel_m10bmc_log_driver);
	if (ret)
		goto err_log;

	ret = platform_driver_register(&nfb_intel_m10bmc_sec_driver);
	if (ret)
		goto err_sec;

	ret = platform_driver_register(&nfb_intel_m10bmc_hwmon_driver);
	if (ret)
		goto err_hwmon;

	ret = platform_driver_register(&nfb_intel_m10bmc);
	if (ret)
		goto err_pdr;

	return 0;

err_pdr:
	platform_driver_unregister(&nfb_intel_m10bmc_hwmon_driver);
err_hwmon:
	platform_driver_unregister(&nfb_intel_m10bmc_sec_driver);
err_sec:
	platform_driver_unregister(&nfb_intel_m10bmc_log_driver);
err_log:
	return ret;

}

void nfb_pmci_exit()
{
	platform_driver_unregister(&nfb_intel_m10bmc);
	platform_driver_unregister(&nfb_intel_m10bmc_hwmon_driver);
	platform_driver_unregister(&nfb_intel_m10bmc_sec_driver);
	platform_driver_unregister(&nfb_intel_m10bmc_log_driver);
}

#endif
