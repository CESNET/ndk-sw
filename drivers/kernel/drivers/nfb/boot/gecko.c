/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver of the NFB platform - gecko module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pci.h>

#include "../nfb.h"
#include "../pci.h"

#define RET_IF_ERROR(x)\
	do {\
		int ret;\
		if ((ret = x) != 0) return ret; \
	} while(0);


#define NFB_GECKO_CTRL_READY            0x80000
#define NFB_GECKO_CTRL_TIMEOUT          0x40000
#define NFB_GECKO_CTRL_DATA_MASK	0xFFFF

#define GECKO_CMD_READ_FLASH                            0x2
#define GECKO_SUBCMD_FLASH_READ_SILICOM_AREA_1          0x03

static int nfb_boot_gecko_wait_ready_data(struct nfb_comp *boot, uint16_t *data)
{
	uint32_t reg;
	int timeout = 20000;

	do {
		reg = nfb_comp_read32(boot, 0x0);
		if (reg & NFB_GECKO_CTRL_READY) {
			if (data)
				*data = reg & NFB_GECKO_CTRL_DATA_MASK;
			if (reg & NFB_GECKO_CTRL_TIMEOUT) {
				msleep(10);
				return 2;
			}
			return 0;
		}
		msleep(1);
	} while (--timeout);

	return 1;
}

static int nfb_boot_gecko_wait_ready(struct nfb_comp *boot)
{
	int ret = nfb_boot_gecko_wait_ready_data(boot, NULL);

	// ignore timeout flag when just waiting for ready
	if (ret == 2)
		return 0;

	return ret;
}

static int nfb_boot_gecko_send_command(struct nfb_comp *boot, const uint8_t cmd,
		uint16_t subCmd, uint32_t data)
{
	uint64_t reg = 0;
	reg = ((uint64_t) cmd & 0xF) << 60 | ((uint64_t) subCmd & 0xFFF) << 48 | data;

	// TODO: add check ready before write?
	if (nfb_boot_gecko_wait_ready(boot))
		return 1;

	nfb_comp_write64(boot, 0x0, reg);
	return 0;
}

static int nfb_boot_gecko_read(struct nfb_comp *boot, const int subcmd, const uint16_t offset, uint16_t *data)
{
	int ret;
	ret = nfb_boot_gecko_send_command(boot, GECKO_CMD_READ_FLASH, subcmd, offset);
	if (ret)
		return ret;

	ret = nfb_boot_gecko_wait_ready_data(boot, data);
	return ret;
}

static int nfb_boot_gecko_read_first_mac(struct nfb_comp *boot, uint64_t *mac)
{
	uint16_t data;
	uint16_t i;

	*mac = 0;
	for (i = 0; i < 6; i += 2) {
		RET_IF_ERROR(nfb_boot_gecko_read(boot, GECKO_SUBCMD_FLASH_READ_SILICOM_AREA_1, i, &data));
		*mac <<= 16;
		*mac |= ((data & 0xFF) << 8) | ((data & 0xFF00) >> 8);
	}
	return 0;
}

void nfb_boot_gecko_read_serial_number(struct nfb_device *nfb, struct nfb_comp *boot)
{
	uint64_t mac;
	if (nfb_boot_gecko_read_first_mac(boot, &mac))
		return;

	nfb->serial = (mac & 0xFFF0) >> 4;
}

void nfb_boot_gecko_read_card_type(struct nfb_device *nfb, struct nfb_comp *boot)
{
	uint64_t mac;
	uint8_t card;

	if (nfb_boot_gecko_read_first_mac(boot, &mac) == 0) {
		card = ((mac & 0xFF0000) >> 16);
		dev_info(&nfb->pci->dev, "nfb_boot: Gecko card type: 0x%x\n", card);
	} else {
		dev_info(&nfb->pci->dev, "nfb_boot: Gecko card type: unknown\n");
	}
}
