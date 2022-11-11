/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - ADC and temperature management
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Lukas Hejcman <xhejcm01@stud.fit.vutbr.cz>
 */

#ifndef NETCOPE_ADCSENSORS_H
#define NETCOPE_ADCSENSORS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <netcope/idcomp.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libfdt.h>

#include <inttypes.h>

// Macros for the addresses of the configuration registers
#define ADC_CONF_REG  0x00
#define ADC_CTRL_REG  0x04
#define ADC_STAT_REG  0x08


/**
 * Internal function used to get a value from any of the DATA registers inside the ADC_SENSOR on a Stratix FPGA.
 * This function is not meant to be accessed directly by the user. Instead, the user should access the data values
 * using the functions nc_get_adc_volt_stratix and nc_get_adc_temp_stratix.
 */
static inline uint32_t _nc_get_adc_value_stratix(struct nfb_device *dev, uint32_t channel_address)
{
	int nodeoffset, shl_modifier;
	struct nfb_comp *comp;
	uint32_t stat_reg_readout, conf_reg;

	// This is used to compensate for the gap in the address space between temps and volts
	shl_modifier = (channel_address <= 0x30) ? 4 : 0;

	// Get conf reg, which should be active only for the specific channel_address.
	conf_reg = 1 << (channel_address / 4 - shl_modifier);

	// Get card address space
	nodeoffset = fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,stratix_adc_sensors");
	comp = nfb_comp_open(dev, nodeoffset);
	if (comp == NULL)
		return -1;

	// Write the relevant CONF and CTRL registers
	nfb_comp_write32(comp, ADC_CONF_REG, conf_reg);
	nfb_comp_write32(comp, ADC_CTRL_REG, (channel_address <= 0x30) ? 0x1 : 0x10000);

	// Wait until our value has been read
	do {
		stat_reg_readout = nfb_comp_read32(comp, ADC_STAT_REG);
	} while (stat_reg_readout != conf_reg);

	// Read it and return it
	conf_reg = nfb_comp_read32(comp, channel_address);

	nfb_comp_close(comp);
	
	return conf_reg;
}

/**
 * Function used for getting the voltage from a specific channel.
 */
static inline float nc_get_adc_volt_stratix(struct nfb_device *dev, uint8_t channel)
{
	double ans;
	uint32_t output_chn = 0x40; // The first address of a voltage data register

	// Quick boundary check
	if (channel > 15) {
	    return NAN;
	}

	// Get the address of the channel to be read
	output_chn += 4*channel;

	// Using double because float had problems with conversions, the value would be off by up to 0xC0
	// in the positive or the negative direction. I'm not really sure why, but double does not seem to have
	// this problem.
	ans = (double)_nc_get_adc_value_stratix(dev, output_chn);

	// Getting the decimal part of the number
	ans /= 0x10000;

	return (float)ans;
}

/**
 * Same as nc_get_adc_volt_stratix();
 * Unless external sensors are connected, channels larger than 1 should NOT be read from.
 */
static inline float nc_get_adc_temp_stratix(struct nfb_device *dev, uint8_t channel)
{
	double ans;
	uint32_t output_chn = 0x10; // The first address of a temperature data register

	// Quick boundary check
	if (channel > 8) {
	    return NAN;
	}

	// Get temperature channel offset
	output_chn += 4*channel;

	// Using double because float had problems with conversions, the value would be off by up to 0xC0
	// in the positive or the negative direction. I'm not really sure why, but double does not seem to have
	// this problem.
	ans = (double)_nc_get_adc_value_stratix(dev, output_chn);

	// Getting the decimal portion of the number
	ans /= 0x100;

	// Calculation for 2's complement
	if (ans >= 0x800000) {
		ans -= 2*0x800000;
	}

	return (float)ans;
}

/**
 * Function used to get temperature from Intel FPGA devices via their Secure Device Manager component.
 */
static inline float nc_get_adc_temp_sdm(struct nfb_device *dev)
{
	int ret;
	struct nfb_boot_ioc_sensor sensor_ioc;

	sensor_ioc.sensor_id = 0;
	sensor_ioc.flags = 0;
#ifdef __KERNEL__
	ret = -ENOSYS;
	//ret = nfb_boot_get_sensor_ioc(dev, &sensor_ioc);
#else
	ret = nfb_sensor_get(dev, &sensor_ioc);
#endif

	if (ret)
		return NAN;

	return sensor_ioc.value / 1000.0f;
}

static inline float nc_adc_sensors_get_temp(struct nfb_device *dev)
{
	if (fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,intel_sdm_controller") >= 0) {
		return nc_get_adc_temp_sdm(dev);
	} else if (fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,stratix_adc_sensors") >= 0) {
		return nc_get_adc_temp_stratix(dev, 0);
	} else if (fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,idcomp") >= 0) {
		return nc_idcomp_sysmon_get_temp(dev);
	} else {
		return NAN;
	}
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_ADCSENSORS_H */
