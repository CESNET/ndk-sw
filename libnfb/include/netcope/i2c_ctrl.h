/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - I2C controller
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Peresini <xperes00@stud.fit.vutbr.cz>
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_I2CCTRL_H
#define NETCOPE_I2CCTRL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_i2c_ctrl {
	unsigned reg;
	unsigned addr;
	int bytes;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_ctrl *nc_i2c_open(const struct nfb_device *dev, int fdt_offset);
static inline int nc_i2c_read(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t *data);
static inline int nc_i2c_write(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t data);
static inline void nc_i2c_close(struct nc_i2c_ctrl *ctrl);

/* ~~~~[ DEFINES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/** Programming delay in cycles */
#define nc_I2C_DELAY            500000

#define I2C_CTRL_REG_CONTROL    0
#define I2C_CTRL_REG_DATA       4

#define I2C_COMP_LOCK (1 << 0)

/* INFO: register mapping differs from OC original */
#define I2C_CTRL_REG_CR         4
#define I2C_CTRL_REG_SR         4
#define I2C_CTRL_REG_RR         5
#define I2C_CTRL_REG_TR         5

#define I2C_CTRL_REG_CR_STA     (1 << 7)
#define I2C_CTRL_REG_CR_STO     (1 << 6)
#define I2C_CTRL_REG_CR_RD      (1 << 5)
#define I2C_CTRL_REG_CR_WR      (1 << 4)
#define I2C_CTRL_REG_CR_ACK     (1 << 3)
#define I2C_CTRL_REG_CR_IACK    (1 << 0)

#define I2C_CTRL_REG_SR_nRXACK  (1 << 7)
#define I2C_CTRL_REG_SR_BUSY    (1 << 6)
#define I2C_CTRL_REG_SR_AL      (1 << 5)
#define I2C_CTRL_REG_SR_TIP     (1 << 1)
#define I2C_CTRL_REG_SR_IF      (1 << 0)
/**
 * brief Function is used as delay between signals sent to i2c bus HW.
 */
static uint8_t _nc_i2c_wait_for_ready(struct nc_i2c_ctrl *ctrl)
{
	uint8_t status_reg;
	struct nfb_comp *comp;

	comp = nfb_user_to_comp(ctrl);

	while ((status_reg = nfb_comp_read8(comp, I2C_CTRL_REG_SR)) & I2C_CTRL_REG_SR_TIP);

	return status_reg;
}

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_ctrl *nc_i2c_open(const struct nfb_device *dev, int fdt_offset)
{
	struct nfb_comp *comp;
	struct nc_i2c_ctrl *ctrl;

	if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, "netcope,i2c"))
		return NULL;

	comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(*ctrl));
	if (comp == NULL)
		return NULL;

	ctrl = (struct nc_i2c_ctrl *) nfb_comp_to_user(comp);

	ctrl->addr = 0xAC;
	ctrl->bytes = 2;

	/* Init HW controller */
	nfb_comp_lock(comp, I2C_COMP_LOCK);
	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, 0x8000f9);
	nfb_comp_unlock(comp, I2C_COMP_LOCK);

	return ctrl;
}

static inline void nc_i2c_close(struct nc_i2c_ctrl *ctrl)
{
	struct nfb_comp *comp;

	comp = nfb_user_to_comp(ctrl);

	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, 0x0);
	nfb_comp_close(comp);
}

static inline void i2c_set_data_bytes(struct nc_i2c_ctrl *ctrl, unsigned count)
{
	ctrl->bytes = count;
}

static inline void i2c_set_addr(struct nc_i2c_ctrl *ctrl, unsigned address)
{
	ctrl->addr = address;
}

static inline int nc_i2c_read(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t *data)
{
	int i;
	struct nfb_comp *comp;
	uint8_t sr = 0;
	int retries = 0;
	int nack = 0;

	comp = nfb_user_to_comp(ctrl);

	/* lock I2C access */
	nfb_comp_lock(comp, I2C_COMP_LOCK);

retry:
	nack = 0;

	/* Init data */
	*data = 0;

	_nc_i2c_wait_for_ready(ctrl);


	/* Start & device address & write */
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, ctrl->addr << 8 | 0x90);

	sr = _nc_i2c_wait_for_ready(ctrl);
	if (sr & I2C_CTRL_REG_SR_nRXACK)
		nack = 1;

	/* Write starting adress & stop */
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, reg << 8 | 0x50);

	sr = _nc_i2c_wait_for_ready(ctrl);
	if (sr & I2C_CTRL_REG_SR_nRXACK)
		nack = 1;

	/* Start & device address & read */
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, (ctrl->addr | 1) << 8 | 0x90);

	sr = _nc_i2c_wait_for_ready(ctrl);
	if (sr & I2C_CTRL_REG_SR_nRXACK)
		nack = 1;

	for(i = 0; i < ctrl->bytes-1; i++) {
		/* Read & no ACK & stop */
		nfb_comp_write32(comp, I2C_CTRL_REG_DATA, (ctrl->addr | 1) << 8 | 0x20);

		sr = _nc_i2c_wait_for_ready(ctrl);
		if (sr & I2C_CTRL_REG_SR_nRXACK)
			nack = 1;

		/* Read result */
		*data = *data << 8 | ((nfb_comp_read32(comp, I2C_CTRL_REG_DATA) >> 8) & 0xFF);
	}

	/* Read & no ACK & stop */
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, (ctrl->addr | 1) << 8 | 0x68);

	_nc_i2c_wait_for_ready(ctrl);

	/* Read result */
	*data = *data << 8 | ((nfb_comp_read32(comp, I2C_CTRL_REG_DATA) >> 8) & 0xFF);

	if (nack && retries++ < 2) {
		goto retry;
	}

	nfb_comp_unlock(comp, I2C_COMP_LOCK);
	return 0;
}

int nc_i2c_write(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t data)
{
	int ret = 0;
	struct nfb_comp *comp;

	comp = nfb_user_to_comp(ctrl);

	/* lock I2C access */
	nfb_comp_lock(comp, I2C_COMP_LOCK);

	_nc_i2c_wait_for_ready(ctrl);

	/* Start & device address & write */
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, ctrl->addr << 8 | 0x90);

	_nc_i2c_wait_for_ready(ctrl);

	/* Write starting adress & stop */
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, reg << 8 | 0x10);

	_nc_i2c_wait_for_ready(ctrl);

	if(ctrl->bytes == 2) {
		/* Write data & ack */
		nfb_comp_write32(comp, I2C_CTRL_REG_DATA, (data >> 8) << 8 | 0x10);

		_nc_i2c_wait_for_ready(ctrl);

		/* Write data & ack & stop */
		nfb_comp_write32(comp, I2C_CTRL_REG_DATA, (data & 0xFF) << 8 | 0x50);

		_nc_i2c_wait_for_ready(ctrl);


	} else if(ctrl->bytes == 1) {
		/* Write data & ack & stop */
		nfb_comp_write32(comp, I2C_CTRL_REG_DATA, data << 8 | 0x50);

		_nc_i2c_wait_for_ready(ctrl);

	} else {
		ret = -1;
	}

	nfb_comp_unlock(comp, I2C_COMP_LOCK);

	return ret;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_I2CCTRL_H */
