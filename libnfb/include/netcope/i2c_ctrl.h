/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - common I2C API
 *
 * Copyright (C) 2025 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_I2C_H
#define NETCOPE_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_i2c_ctrl {
	void *priv;

	void (*set_addr)(void *ctrl, uint8_t address);
	int (*read_reg)(void *ctrl, uint8_t reg, uint8_t *data, unsigned size);
	int (*write_reg)(void *ctrl, uint8_t reg, const uint8_t *data, unsigned size);
	void (*close)(void *ctrl);
};

#ifdef __cplusplus
} // extern "C"
#endif

#include "i2c_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_ctrl *nc_i2c_open(const struct nfb_device *dev, int fdt_offset);
static inline void nc_i2c_set_addr(struct nc_i2c_ctrl *ctrl, uint8_t address);
static inline int nc_i2c_read_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, uint8_t *data, unsigned size);
static inline int nc_i2c_write_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, const uint8_t *data, unsigned size);
static inline void nc_i2c_close(struct nc_i2c_ctrl *ctrl);


/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_ctrl *nc_i2c_open(const struct nfb_device *dev, int fdt_offset)
{
	void *priv = NULL;
	struct nc_i2c_ctrl *i2c = NULL;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, "netcope,i2c") == 0) {
		priv = nc_i2c_controller_open_ext(dev, fdt_offset, sizeof(*i2c), (void**) &i2c);
		if (priv == NULL)
			return NULL;

		i2c->priv = priv;
		i2c->set_addr = nc_i2c_controller_set_addr;
		i2c->read_reg = nc_i2c_controller_read_reg;
		i2c->write_reg = nc_i2c_controller_write_reg;
		i2c->close = nc_i2c_controller_close;
	}

	return i2c;
}

static inline void nc_i2c_set_addr(struct nc_i2c_ctrl *ctrl, uint8_t address)
{
	ctrl->set_addr(ctrl->priv, address);
}

static inline int nc_i2c_read_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, uint8_t *data, unsigned size)
{
	return ctrl->read_reg(ctrl->priv, reg, data, size);
}

static inline int nc_i2c_write_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, const uint8_t *data, unsigned size)
{
	return ctrl->write_reg(ctrl->priv, reg, data, size);
}

static inline void nc_i2c_close(struct nc_i2c_ctrl *ctrl)
{
	ctrl->close(ctrl->priv);
}

static inline void i2c_set_addr(struct nc_i2c_ctrl *ctrl, unsigned address)
{
	nc_i2c_set_addr(ctrl, address);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_I2C_H */
