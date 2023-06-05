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
#include <libfdt.h>


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
static inline int nc_get_adc_volt_stratix(struct nfb_device *dev, uint8_t channel, uint32_t *val)
{
	uint32_t raw;
	uint32_t ans;
	uint32_t output_chn = 0x40; // The first address of a voltage data register

	// Quick boundary check
	if (channel > 15)
		return -EINVAL;


	// Get the address of the channel to be read
	output_chn += 4 * channel;

	// The Voltage Sensor IP core returns the sampled voltage in unsigned 32-bit fixed point
	// binary format, with 16 bits below binary point.
	raw = _nc_get_adc_value_stratix(dev, output_chn);

	// Getting the integer part of the number in millivolts
	ans = (raw >> 16) * 1000;

	// getting fractional part of the number in millivolts
	ans += (raw & 0xFFFF) * 1000 / 65536;

	*val = ans;
	return 0;
}

/**
 * Same as nc_get_adc_volt_stratix();
 * Unless external sensors are connected, channels larger than 1 should NOT be read from.
 */
static inline int nc_get_adc_temp_stratix(struct nfb_device *dev, uint8_t channel, int32_t *val)
{
	int32_t raw;
	int32_t ans;
	uint32_t output_chn = 0x10; // The first address of a temperature data register

	// Quick boundary check
	if (channel > 8)
		return -EINVAL;


	// Get temperature channel offset
	output_chn += 4 * channel;

	//The Temperature Sensor IP core returns the Celsius temperature value in signed 
	//32-bit fixed point binary format, with eight bits below binary point
	raw = _nc_get_adc_value_stratix(dev, output_chn);

	// Getting the integer part of the number and multiplying it to get millidegrees
	ans = (raw >> 8) * 1000; //shift is arithmetic because raw is signed

	//getting fractional part of the number in millidegrees
	ans += (raw & 0xFF) * 1000 / 256;

	*val = ans;
	return 0;
}

/**
 * Function used to get temperature from Intel FPGA devices via their Secure Device Manager component.
 */
static inline int nc_get_adc_temp_sdm(struct nfb_device *dev, int32_t *val)
{
#ifdef __KERNEL__
	struct nfb_boot *nfb_boot;
	int32_t temperature;
#endif
	int ret;
	struct nfb_boot_ioc_sensor sensor_ioc;

	sensor_ioc.sensor_id = 0;
	sensor_ioc.flags = 0;


#ifndef __KERNEL__
	ret = nfb_sensor_get(dev, &sensor_ioc);
#else
	nfb_boot = nfb_get_priv_for_attach_fn(dev, nfb_boot_attach);

	if (nfb_boot->sdm == NULL)
		return -ENODEV;

	ret = sdm_get_temperature(nfb_boot->sdm, &temperature);
	sensor_ioc.value = temperature * 1000 / 256;
#endif


	if (ret)
		return ret;

	*val = sensor_ioc.value;
	return 0;
}

static inline int nc_adc_sensors_get_temp(struct nfb_device *dev, int32_t *val)
{
	if (fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,intel_sdm_controller") >= 0) {
		return nc_get_adc_temp_sdm(dev, val);
	} else if (fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,stratix_adc_sensors") >= 0) {
		return nc_get_adc_temp_stratix(dev, 0, val);
	} else if (fdt_node_offset_by_compatible(nfb_get_fdt(dev), -1, "netcope,idcomp") >= 0) {
		return nc_idcomp_sysmon_get_temp(dev, val);
	} else {
		return -EINVAL;
	}
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_ADCSENSORS_H */
