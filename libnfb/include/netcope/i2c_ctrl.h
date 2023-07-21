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
	uint8_t addr;
	uint16_t prescale;
	int bytes; /* Obsolete */
	unsigned int prescale_init : 1;
};

/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_i2c_ctrl *nc_i2c_open(const struct nfb_device *dev, int fdt_offset);
static inline void nc_i2c_set_addr(struct nc_i2c_ctrl *ctrl, uint8_t address);
static inline int nc_i2c_read_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, uint8_t *data, unsigned size);
static inline int nc_i2c_write_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, const uint8_t *data, unsigned size);
static inline void nc_i2c_close(struct nc_i2c_ctrl *ctrl);

/* Obsolete */
static inline int nc_i2c_read(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t *data);
static inline int nc_i2c_write(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t data);

/* ~~~~[ DEFINES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
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
	ctrl->prescale_init = 0;

	/* INFO: The first opener should init the controller and the last shutter should deinit it.
	 * Unfornatelly there is currently not a counted-semaphore-like mechanism in nfb lock API.
	 * The workaround is to init and deinit the controller in each read / write access. */
//	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, 0x800000 | ctrl->prescale);

	return ctrl;
}

static inline void nc_i2c_close(struct nc_i2c_ctrl *ctrl)
{
	struct nfb_comp *comp;

	comp = nfb_user_to_comp(ctrl);

//	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, 0x0);
	nfb_comp_close(comp);
}

static inline void nc_i2c_set_addr(struct nc_i2c_ctrl *ctrl, uint8_t address)
{
	ctrl->addr = address;
}

static inline void i2c_set_addr(struct nc_i2c_ctrl *ctrl, unsigned address)
{
	nc_i2c_set_addr(ctrl, address);
}

static inline void i2c_set_data_bytes(struct nc_i2c_ctrl *ctrl, unsigned count)
{
	if (count > 0 && count <= 4)
		ctrl->bytes = count;
}

static inline int nc_i2c_read(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t *data)
{
	int ret, i;
	uint8_t data8[4] = {0xFF, 0xFF, 0xFF, 0xFF};

	*data = 0xFFFFFFFF;
	ret = nc_i2c_read_reg(ctrl, reg, data8, ctrl->bytes);
	if (ret != ctrl->bytes)
		return -1;
	for (i = 0; i < ctrl->bytes && (unsigned) i < sizeof(data8); i++) {
		*data |= data8[i] << (i * 8);
	}
	return 0;
}

static inline int nc_i2c_write(struct nc_i2c_ctrl *ctrl, uint32_t reg, uint32_t data)
{
	int ret, i;
	uint8_t data8[4];

	for (i = 0; i < ctrl->bytes && (unsigned) i < sizeof(data8); i++) {
		data8[i] = data >> (8 * i);
	}
	ret = nc_i2c_write_reg(ctrl, reg, data8, ctrl->bytes);
	return ret == ctrl->bytes ? 0 : -1;
}

static inline int _nc_i2c_write_byte(struct nc_i2c_ctrl *ctrl, uint8_t data, uint8_t flags)
{
	struct nfb_comp *comp;
	uint8_t sr = 0;

	comp = nfb_user_to_comp(ctrl);
	nfb_comp_write32(comp, I2C_CTRL_REG_DATA, (((uint32_t)data) << 8) | flags);
	sr = _nc_i2c_wait_for_ready(ctrl);

	if (sr & I2C_CTRL_REG_SR_nRXACK && (flags & I2C_CTRL_REG_CR_STO) == 0)
		return 1;
	return 0;
}

static inline void __nc_i2c_prescale_init(struct nc_i2c_ctrl *ctrl)
{
	if (ctrl->prescale_init == 0) {
		ctrl->prescale_init = 1;
		ctrl->prescale = nfb_comp_read16(nfb_user_to_comp(ctrl), I2C_CTRL_REG_CONTROL);
		if (ctrl->prescale == 0)
			ctrl->prescale = 0x00f9;
	}

}

static inline int nc_i2c_read_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, uint8_t *data, unsigned size)
{
	unsigned i;
	int ret;
	struct nfb_comp *comp;
	uint8_t flags;
	int retries = 0;

	if (size == 0)
		return 0;

	comp = nfb_user_to_comp(ctrl);

	/* lock I2C access */
	if (!nfb_comp_lock(comp, I2C_COMP_LOCK))
		return -EAGAIN;

	__nc_i2c_prescale_init(ctrl);

	/* Init HW controller */
	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, 0x800000 | ctrl->prescale);

retry:
	_nc_i2c_wait_for_ready(ctrl);

	ret = 0;
	ret |= _nc_i2c_write_byte(ctrl, ctrl->addr, I2C_CTRL_REG_CR_WR | I2C_CTRL_REG_CR_STA);
	ret |= _nc_i2c_write_byte(ctrl, reg, I2C_CTRL_REG_CR_WR | I2C_CTRL_REG_CR_STO);

	ret |= _nc_i2c_write_byte(ctrl, ctrl->addr | 1, I2C_CTRL_REG_CR_WR | I2C_CTRL_REG_CR_STA);

	for (i = 0, flags = I2C_CTRL_REG_CR_RD; i < size; i++) {
		if (i == size - 1)
			flags |= I2C_CTRL_REG_CR_ACK | I2C_CTRL_REG_CR_STO;

		ret |= _nc_i2c_write_byte(ctrl, 0, flags);

		if (ret)
			continue;

		data[i] = nfb_comp_read32(comp, I2C_CTRL_REG_DATA) >> 8;
	}

	if (ret && retries++ < 0) {
		goto retry;
	}

	/* Deinit HW controller */
	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, ctrl->prescale);

	nfb_comp_unlock(comp, I2C_COMP_LOCK);

	if (ret)
		return -EIO;

	return i;
}

static inline int nc_i2c_write_reg(struct nc_i2c_ctrl *ctrl, uint8_t reg, const uint8_t *data, unsigned size)
{
	unsigned i;
	int ret;
	struct nfb_comp *comp;
	uint8_t flags;

	if (size == 0)
		return 0;

	comp = nfb_user_to_comp(ctrl);

	/* lock I2C access */
	if (!nfb_comp_lock(comp, I2C_COMP_LOCK))
		return -EAGAIN;

	__nc_i2c_prescale_init(ctrl);

	/* Init HW controller */
	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, 0x800000 | ctrl->prescale);

	_nc_i2c_wait_for_ready(ctrl);

	ret = 0;
	ret |= _nc_i2c_write_byte(ctrl, ctrl->addr, I2C_CTRL_REG_CR_WR | I2C_CTRL_REG_CR_STA);
	ret |= _nc_i2c_write_byte(ctrl, reg, I2C_CTRL_REG_CR_WR);

	for (i = 0, flags = I2C_CTRL_REG_CR_WR; i < size; i++) {
		if (i == size - 1)
			flags |= I2C_CTRL_REG_CR_STO;

		ret |= _nc_i2c_write_byte(ctrl, data[i], flags);
	}

	/* Deinit HW controller */
	nfb_comp_write32(comp, I2C_CTRL_REG_CONTROL, ctrl->prescale);

	nfb_comp_unlock(comp, I2C_COMP_LOCK);

	if (ret)
		return -EIO;

	return i;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_I2CCTRL_H */
