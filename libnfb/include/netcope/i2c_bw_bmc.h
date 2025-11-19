/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - I2C controller
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Peresini <xperes00@stud.fit.vutbr.cz>
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_I2C_BW_BMC_H
#define NETCOPE_I2C_BW_BMC_H

#include "bittware_bmc_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_i2c_bw_bmc_ctrl {
	struct nc_bw_bmc *bmc;
	uint8_t addr;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_bw_bmc_ctrl *nc_i2c_bw_bmc_open_ext(const struct nfb_device *dev, int fdt_offset, size_t custom_data_sz, void **custom_data);
static inline void nc_i2c_bw_bmc_set_addr(void *, uint8_t address);
static inline int nc_i2c_bw_bmc_read_reg(void *, uint8_t reg, uint8_t *data, unsigned size);
static inline int nc_i2c_bw_bmc_write_reg(void *, uint8_t reg, const uint8_t *data, unsigned size);
static inline void nc_i2c_bw_bmc_close(void *);

/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define I2C_COMP_LOCK (1 << 0)

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_bw_bmc_ctrl *nc_i2c_bw_bmc_open_ext(const struct nfb_device *dev, int fdt_offset, size_t custom_data_sz, void **custom_data)
{
	struct nc_bw_bmc *bmc;
	struct nc_i2c_bw_bmc_ctrl *ctrl = NULL;

	bmc = nc_bw_bmc_open_ext(dev, fdt_offset, NULL, 256, sizeof(*ctrl) + custom_data_sz, (void**) &ctrl);
	if (bmc == NULL)
		return NULL;

	ctrl->bmc = bmc;
	ctrl->addr = 0xAC;

	if (custom_data)
		*custom_data = &ctrl[1];

	return ctrl;
}

static inline void nc_i2c_bw_bmc_close(void *p)
{
	struct nc_i2c_bw_bmc_ctrl *ctrl = p;

	nc_bw_bmc_close(ctrl->bmc);
}

static inline void nc_i2c_bw_bmc_set_addr(void *p, uint8_t address)
{
	struct nc_i2c_bw_bmc_ctrl *ctrl = p;
	ctrl->addr = address;
}

static inline int nc_i2c_bw_bmc_read_reg(void *p, uint8_t reg, uint8_t *data, unsigned size)
{
	struct nc_i2c_bw_bmc_ctrl *ctrl = p;

	int ret = 0;

	uint8_t wdata[3];

	if (size == 0)
		return 0;

	/* lock I2C access */
	if (!nc_bw_bmc_lock(ctrl->bmc))
		return -EAGAIN;

	wdata[0] = ctrl->addr / 2;
	wdata[1] = size;
	wdata[2] = reg;

	ret = nc_bw_bmc_send_i2c(ctrl->bmc, wdata, 3);
	if (ret != 0) {
		nc_bw_bmc_unlock(ctrl->bmc);
		return ret;
	}

	ret = nc_bw_bmc_receive_i2c(ctrl->bmc, data, size);
	if (ret != 0) {
		nc_bw_bmc_unlock(ctrl->bmc);
		return ret;
	}

	nc_bw_bmc_unlock(ctrl->bmc);

	return size;
}

static inline int nc_i2c_bw_bmc_write_reg(void *p, uint8_t reg, const uint8_t *data, unsigned size)
{
	int ret;
	struct nc_i2c_bw_bmc_ctrl *ctrl = p;

	if (!nc_bw_bmc_lock(ctrl->bmc))
		return -EAGAIN;

	nc_bw_bmc_buffer_init(ctrl->bmc);
	nc_bw_bmc_push_uint8(ctrl->bmc, ctrl->addr / 2);
	nc_bw_bmc_push_uint8(ctrl->bmc, 0);
	nc_bw_bmc_push_uint8(ctrl->bmc, reg);
	nc_bw_bmc_push(ctrl->bmc, data, size);

	ret = _nc_bw_bmc_send_raw_frame(ctrl->bmc, ctrl->bmc->buffer, ctrl->bmc->pos, 1, 1, 0x22, 0x10);
	nc_bw_bmc_unlock(ctrl->bmc);
	return ret;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_I2C_BW_BMC_H */
