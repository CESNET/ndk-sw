/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HWMon helper library - transciever sensors functions
 *
 * Copyright (C) 2017-2023 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include <config.h>
#ifdef CONFIG_NFB_ENABLE_HWMON

#ifndef NFB_HWMON_TRANSCEIVER_H
#define NFB_HWMON_TRANSCEIVER_H

#define SFF8636_IDENTIFIER            0
#define SFF8636_TEMPERATURE          22
#define CMIS_TEMPERATURE             14
#define MDIO_TEMPERATURE         0xA02F
#define MAX_FDT_PATH_LENGTH 	    512
#define LABEL_BUFFER_LEN 	     90

struct trc_data_t {
	int trc_count;
	struct trc_t *trc_arr;
};

enum trc_type {
	QSFP,
	QSFP28,
	CFP2,
	CFP4,
	UNKNOWN,
};

struct trc_t {
	const char *fdt_node_path;
	enum trc_type type;
	const char *label;
};

static inline int _nfb_hwmon_transceiver_is_present(const struct nfb_device *dev, int node)
{
	int ret;
	int node_statusreg;

	struct nfb_comp *comp_status;

	node_statusreg = fdt_node_offset_by_phandle_ref(nfb_get_fdt(dev), node, "status-reg");
	comp_status = nfb_comp_open(dev, node_statusreg);
	if (comp_status == NULL)
		return -1;

	ret = nc_transceiver_statusreg_is_present(comp_status);
	nfb_comp_close(comp_status);
	return ret;
}

static inline enum trc_type _nfb_hwmon_transceiver_type(const struct nfb_device *nfb, int trc_node_offset)
{
	int proplen;
	const char *property;
	const void *fdt;
	fdt = nfb_get_fdt(nfb);
	property = fdt_getprop(fdt, trc_node_offset, "type", &proplen);

	if (proplen < 1)
		return UNKNOWN;

	if (!strcmp(property, "QSFP"))
		return QSFP;
	else if (!strcmp(property, "QSFP28"))
		return QSFP28;
	else if (!strcmp(property, "CFP2"))
		return CFP2;
	else if (!strcmp(property, "CFP4"))
		return CFP4;
	else
		return UNKNOWN;
}

static inline const char *_nfb_hwmon_transceiver_type_label(const struct trc_t trc)
{
	const char *label;

	switch (trc.type) {
	case QSFP:
		label = "QSFP transceiver";
		break;
	case QSFP28:
		label = "QSFP28 transceiver";
		break;
	case CFP2:
		label = "CFP2 transceiver";
		break;
	case CFP4:
		label = "CFP4 transceiver";
		break;
	default:
		label = "Unknown transceiver";
		break;
	}

	return label;
}

//Looks for available transceivers
static inline int nfb_hwmon_transceiver_lookup(const struct nfb_device *nfb, struct trc_data_t *data)
{
	const void *fdt;
	int node = -1;
	int t_counter = 0;
	char fdt_path_buffer[MAX_FDT_PATH_LENGTH];
	char label_buffer[LABEL_BUFFER_LEN];
	int ret = 0;

	fdt = nfb_get_fdt(nfb);

	fdt_for_each_compatible_node(fdt, node, "netcope,transceiver")
	{
		t_counter++;
	}

	data->trc_count = t_counter;
	if (t_counter == 0) {
		data->trc_arr = NULL;
		return 0;
	}

	data->trc_arr = devm_kzalloc(nfb->dev, sizeof(struct trc_t) * t_counter, GFP_KERNEL);
	if (!data->trc_arr) {
		data->trc_count = 0;
		return -ENOMEM;
	}

	t_counter = 0;
	node = -1;
	fdt_for_each_compatible_node(fdt, node, "netcope,transceiver")
	{
		if (fdt_get_path(fdt, node, fdt_path_buffer, MAX_FDT_PATH_LENGTH) < 0) {
			data->trc_count--;
			ret = -1;
			continue;
		}

		data->trc_arr[t_counter].fdt_node_path = devm_kstrdup_const(nfb->dev, fdt_path_buffer, GFP_KERNEL);
		if (!data->trc_arr[t_counter].fdt_node_path) {
			data->trc_count--;
			ret = -1;
			continue;
		}

		data->trc_arr[t_counter].type = _nfb_hwmon_transceiver_type(nfb, node);

		sprintf(label_buffer, "%s %d", _nfb_hwmon_transceiver_type_label(data->trc_arr[t_counter]), t_counter);
		data->trc_arr[t_counter].label = devm_kstrdup_const(nfb->dev, label_buffer, GFP_KERNEL);
		if (!data->trc_arr[t_counter].label) {
			devm_kfree(nfb->dev, data->trc_arr[t_counter].fdt_node_path);
			data->trc_count--;
			ret = -1;
			continue;
		}

		t_counter++;
		if (t_counter >= data->trc_count)
			break;
	}

	return ret;
}

static inline int32_t _nfb_hwmon_transceiver_temp_qsfpp(const struct nfb_device *nfb, int trc_node_offset, int32_t *val)
{
	uint32_t i2c_addr;
	struct nc_i2c_ctrl *ctrl;

	const void *fdt;
	const fdt32_t *prop32;
	int proplen;
	int node_ctrl;
	int node_params;
	uint8_t reg8 = 0;
	uint32_t reg32 = 0;
	int32_t temp;

	fdt = nfb_get_fdt(nfb);

	prop32 = fdt_getprop(fdt, trc_node_offset, "control", &proplen);
	node_ctrl = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop32));
	node_params = fdt_subnode_offset(fdt, trc_node_offset, "control-param");
	prop32 = fdt_getprop(fdt, node_params, "i2c-addr", &proplen);

	if (proplen == sizeof(*prop32))
		i2c_addr = fdt32_to_cpu(*prop32);
	else
		i2c_addr = 0xA0;


	ctrl = nc_i2c_open(nfb, node_ctrl);
	if (ctrl == NULL)
		return -EINVAL;


	nc_i2c_set_addr(ctrl, i2c_addr);

	//transciever identifier
	nc_i2c_read_reg(ctrl, SFF8636_IDENTIFIER, &reg8, 1);

	if (reg8 == 0x18) {
		nc_i2c_read_reg(ctrl, CMIS_TEMPERATURE, (uint8_t *)&reg32, 1);
		nc_i2c_read_reg(ctrl, CMIS_TEMPERATURE + 1, ((uint8_t *)&reg32) + 1, 1);
		reg32 = ntohs(reg32);
		temp = reg32 * 1000 / 256;
	} else {
		nc_i2c_read_reg(ctrl, SFF8636_TEMPERATURE, (uint8_t *)&reg32, 1);
		nc_i2c_read_reg(ctrl, SFF8636_TEMPERATURE + 1, ((uint8_t *)&reg32) + 1, 1);
		reg32 = ntohs(reg32);
		temp = reg32 * 1000 / 256;
	}

	nc_i2c_close(ctrl);

	*val = temp;
	return 0;
}

static inline int32_t _nfb_hwmon_transceiver_temp_cfp2(const struct nfb_device *nfb, int trc_node_offset, int32_t *val)
{
	uint32_t reg32 = 0;
	int32_t temp = 0;
	const void *fdt = nfb_get_fdt(nfb);

	int node_ctrl;
	int node_ctrlparam;

	const fdt32_t *prop32;
	int proplen;

	struct nc_mdio *mdio;
	int mdev = 0;

	prop32 = fdt_getprop(fdt, trc_node_offset, "control", &proplen);
	node_ctrl = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop32));
	node_ctrlparam = fdt_subnode_offset(fdt, trc_node_offset, "control-param");
	prop32 = fdt_getprop(fdt, node_ctrlparam, "dev", &proplen);
	if (proplen == sizeof(*prop32)) {
		mdev = fdt32_to_cpu(*prop32);
	} else {
		return -EINVAL;
	}

	mdio = nc_mdio_open(nfb, node_ctrl, node_ctrlparam);
	if (mdio == NULL) {
		return -EINVAL;
	}

	reg32 = nc_mdio_read(mdio, mdev, 1, MDIO_TEMPERATURE);
	temp = reg32 * 1000 / 256;

	*val = temp;
	return 0;
}

static inline int nfb_hwmon_transceiver_temp(const struct nfb_device *nfb, const struct trc_t trc, int32_t *val)
{
	int ret;
	int node_offset = fdt_path_offset(nfb->fdt, trc.fdt_node_path);
	if (node_offset < 0)
		return -EINVAL;

	if (_nfb_hwmon_transceiver_is_present(nfb, node_offset) == 0)
		return -ENODEV;

	switch (trc.type) {
	case QSFP:
		ret = _nfb_hwmon_transceiver_temp_qsfpp(nfb, node_offset, val);
		break;
	case QSFP28:
		ret = _nfb_hwmon_transceiver_temp_qsfpp(nfb, node_offset, val);
		break;
	case CFP2:
		ret = _nfb_hwmon_transceiver_temp_cfp2(nfb, node_offset, val);
		break;
	case CFP4:
		ret = _nfb_hwmon_transceiver_temp_cfp2(nfb, node_offset, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#endif // NFB_HWMON_TRANSCEIVER_H
#endif // CONFIG_NFB_ENABLE_HWMON
