/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * HWMon driver module of the NFB platform
 *
 * Copyright (C) 2017-2023 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#include <config.h>
#ifdef CONFIG_NFB_ENABLE_HWMON

#include <linux/device.h>
#include <linux/hwmon.h>

#include <linux/pci.h>

#include "../nfb.h"
#include "../boot/boot.h"
#include <libfdt.h>

#include <netcope/adc_sensors.h>
#include "nfb_hwmon.h"

#include <netcope/i2c_ctrl.h>
#include <netcope/mdio.h>
#include <netcope/transceiver.h>

#include "nfb_hwmon_transceiver.h"
#include <linux/types.h>

#define ERROR_VAL 999999

struct card_thresholds_t {
	int32_t max_temp;
	int32_t crit_temp;
	const char *board_name;
};

struct hwmon_data_t {
	struct nfb_device *nfb;
	struct card_thresholds_t *card_thr;
	struct trc_data_t *trc_data;
};

static const struct card_thresholds_t card_thresholds_arr[] = {
	//{ 70000, 80000, "COMBO_400G1" },
	// It makes sense for Undefined Card temperature to be last
	// as that is the threshold that will be chosen if no other thresholds pass the name comparison check
	{ 70000, 80000, "UNDEFINED_CARD"},
	{ 0, 0, NULL, },
};

static umode_t nfb_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
	const struct hwmon_data_t *mon_data = data;
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			switch (channel) {
			case 0:
				return 0444;
			default:
				if (channel <= mon_data->trc_data->trc_count && mon_data->trc_data->trc_count > 0)
					return 0444;

				return 0000;
			}
		case hwmon_temp_max:
			switch (channel) {
			case 0:
				return 0644;
			default:
				return 0000;
			}
		case hwmon_temp_crit:
			switch (channel) {
			case 0:
				return 0644;
			default:
				return 0000;
			}
		case hwmon_temp_label:
			switch (channel) {
			case 0:
				return 0444;
			default:
				if (channel <= mon_data->trc_data->trc_count && mon_data->trc_data->trc_count > 0)
					return 0444;

				return 0000;
			}
		default:
			return 0000;
		}
	default:
		return 0000;
	}
}

static int nfb_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long val)
{
	struct hwmon_data_t *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_max:
			switch (channel) {
			case 0:
				data->card_thr->max_temp = val;
				break;
			default:
				break;
			}
			break;
		case hwmon_temp_crit:
			switch (channel) {
			case 0:
				data->card_thr->crit_temp = val;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int nfb_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
				u32 attr, int channel, long *val)
{
	struct hwmon_data_t *data = dev_get_drvdata(dev);
	int ret = 0;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			switch (channel) {
			case 0:
				*val = 0;
				ret = nc_adc_sensors_get_temp(data->nfb, (int32_t *)val);
				if (ret)
					ret = -EINVAL;
				break;
			default:
				if (channel <= data->trc_data->trc_count && data->trc_data->trc_count > 0) {
					*val = 0;
					ret = nfb_hwmon_transceiver_temp(data->nfb, data->trc_data->trc_arr[channel - 1], (int32_t *)val);
				}
				if (ret)
					ret = -EINVAL;
				break;
			}
			break;
		case hwmon_temp_max:
			switch (channel) {
			case 0:
				*val = data->card_thr->max_temp;
				break;
			default:
				ret = -EINVAL;
				break;
			}
			break;
		case hwmon_temp_crit:
			switch (channel) {
			case 0:
				*val = data->card_thr->crit_temp;
				break;
			default:
				ret = -EINVAL;
				break;
			}
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int nbf_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, const char **str)
{
	struct hwmon_data_t *data = dev_get_drvdata(dev);
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			switch (channel) {
			case 0:
				*str = "Main FPGA temperature";
				break;
			default:
				if (channel <= data->trc_data->trc_count && data->trc_data->trc_count > 0)
					*str = data->trc_data->trc_arr[channel - 1].label;
				else
					*str = "Undefined";

				break;
			}
			break;
		default:
			*str = "Undefined";
			break;
		}
		break;
	default:
		*str = "Undefined";
		break;
	}

	return 0;
}

static const struct hwmon_channel_info *channel_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX | HWMON_T_CRIT, // 1 input for chip temp
				 HWMON_T_INPUT | HWMON_T_LABEL, // 10 inputs for trc_arr temp
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL,
				 HWMON_T_INPUT | HWMON_T_LABEL),
			   NULL,
};

static const struct hwmon_ops mon_ops = {
	.is_visible = nfb_hwmon_is_visible,
	.read = nfb_hwmon_read,
	.write = nfb_hwmon_write,
	.read_string = nbf_hwmon_read_string,
};

static const struct hwmon_chip_info chip_info = {
	.ops = &mon_ops,
	.info = channel_info,
};

int nfb_hwmon_attach(struct nfb_device *nfb, void **priv)
{
	struct device *hwmon_dev = NULL;
	struct trc_data_t *trc_data;
	struct hwmon_data_t *hwmon_data;
	struct card_thresholds_t *card_thr;
	const void *fdt;
	int fdt_offset;
	int name_len;
	const void *prop;
	char *name;
	int ret;
	int i;

	fdt = nfb_get_fdt(nfb);
	fdt_offset = fdt_path_offset(fdt, "/board/");
	prop = fdt_getprop(fdt, fdt_offset, "board-name", &name_len);

	if (nfb == NULL)
		return -EINVAL;

	if (nfb->dev == NULL)
		return -EINVAL;

	if (name_len > 0) {
		name = devm_kstrdup(nfb->dev, prop, GFP_KERNEL);
		if (!name)
			goto name_err;

		for (i = 0; i < name_len; i++) {
			if (hwmon_is_bad_char(name[i]))
				name[i] = '_';

		}
	} else {
		name = "unknown_board";
	}

	trc_data = devm_kzalloc(nfb->dev, sizeof(struct trc_data_t), GFP_KERNEL);
	if (!trc_data)
		goto trc_data_err;


	ret = nfb_hwmon_transceiver_lookup(nfb, trc_data);
	if (ret)
		trc_data->trc_count = 0;

	card_thr = devm_kzalloc(nfb->dev, sizeof(struct card_thresholds_t), GFP_KERNEL);
	if (!card_thr)
		goto card_thr_err;


	hwmon_data = devm_kzalloc(nfb->dev, sizeof(struct hwmon_data_t), GFP_KERNEL);
	if (!hwmon_data)
		goto hwmon_data_err;

	hwmon_data->nfb = nfb;
	hwmon_data->trc_data = trc_data;
	hwmon_data->card_thr = card_thr;

	for (i = 0; card_thresholds_arr[i].board_name; i++) {
		hwmon_data->card_thr->board_name = card_thresholds_arr[i].board_name;
		hwmon_data->card_thr->max_temp = card_thresholds_arr[i].max_temp;
		hwmon_data->card_thr->crit_temp = card_thresholds_arr[i].crit_temp;
		if (!strcmp(card_thresholds_arr[i].board_name, name)) {
			break;
		}
	}

	hwmon_dev = hwmon_device_register_with_info(&(nfb->pci->dev), name, hwmon_data, &chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		goto hwmon_dev_err;


	*priv = (void *)hwmon_dev;

	return 0;


hwmon_dev_err:
	devm_kfree(nfb->dev, hwmon_data);
hwmon_data_err:
	devm_kfree(nfb->dev, card_thr);
card_thr_err:
	for (i = 0; i < trc_data->trc_count; i++) {
		devm_kfree(nfb->dev, trc_data->trc_arr[i].fdt_node_path);
	}
	devm_kfree(nfb->dev, trc_data);
trc_data_err:
	devm_kfree(nfb->dev, name);
name_err:
	return -ENOMEM;
}

void nfb_hwmon_detach(struct nfb_device *nfb, void *priv)
{
	struct device *hwmon_dev = priv;
	if (hwmon_dev)
		hwmon_device_unregister(hwmon_dev);
}

#else // #ifdef CONFIG_NFB_ENABLE_HWMON
#include "../nfb.h"
#include "nfb_hwmon.h"
int nfb_hwmon_attach(struct nfb_device *nfb, void **priv)
{
	return 0;
}
void nfb_hwmon_detach(struct nfb_device *nfb, void *priv) { }
#endif // #ifdef CONFIG_NFB_ENABLE_HWMON
