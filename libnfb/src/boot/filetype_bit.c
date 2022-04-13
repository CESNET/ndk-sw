/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - bitstream file type
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "boot.h"

ssize_t nfb_fw_open_bit_raw(FILE *fd, void **pdata);

static const unsigned char bitReverseTable256[] =
{
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
	0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
	0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
	0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
	0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
	0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
};

/*
 * Function reverse bit order of 16 bit number - need conversion bitstream file which will be written to flash
 * @num - IN: input number return reversed bit order on num
 */
static inline uint16_t reverseBits16(uint16_t num)
{
	uint16_t ret = 0;
	ret |= bitReverseTable256[(num >>  0) & 0xff] <<  8;
	ret |= bitReverseTable256[(num >>  8) & 0xff] <<  0;
	return ret;
}

void nfb_fw_bitstream_reverse_bits_16(void *data, size_t size)
{
	size_t i;

	uint16_t *data16 = (uint16_t*) data;
	for (i = 0; i < size / 2; i++) {
		/* Swap bits in 16b words */
		*data16 = reverseBits16(*data16);
		data16++;
	}
}

/*
 * function load data from configuration .bit file
 * @file	IN:	file from where are data loaded
 * @size	OUT:	size of loaded data
 * @data	OUT:	pointer to valid loaded data
 */
ssize_t nfb_fw_open_bit(FILE *fd, void **pdata, enum bitstream_format f)
{
	ssize_t ret;
	ret = nfb_fw_open_bit_raw(fd, pdata);
	if (ret > 0 && f != BITSTREAM_FORMAT_SPI4)
		nfb_fw_bitstream_reverse_bits_16(*pdata, ret);
	return ret;
}

ssize_t nfb_fw_open_bit_raw(FILE *fd, void **pdata)
{
	ssize_t size;
	ssize_t readen;

	uint8_t *data8;
	uint32_t *data32;

	int valid_bitstream = 0;
	int i;
	int start_pos;

	int z;
	int j;
	int first_valid = 0;
	int second_valid = 0;

	int offset = 0;

	/* get size of whole bitstream file in bytes */
	fseek(fd, 0, SEEK_END);
	size = ftell(fd);
	rewind(fd);

	/* malloc exact space for data */
	data8 = malloc(1024);
	readen = fread(data8, 1024, 1, fd);
	if (readen != 1) {
		free(data8);
		return -1;
	}

	/* verify if bitsteam data is valid */
	/* verification is based on exact location and number of FFFFFFFF dummy words from specification of bitstream file for Xilinx FPGA */
	for (i = 0; i < 512; i++) {
		if (data8[i] == 0xFF ) {
			start_pos = i;
			first_valid = 0;
			second_valid = 0;
			for (j = i; j < (i+32); j++){
				if (data8[j] == 0xFF)
					first_valid++;
			}
			i = j;
			if (first_valid == 32) {
				for (z = i+8; z < (i+16); z++) {
					if (data8[z] == 0xFF)
						second_valid++;
				}
				i = z;
			}
			if (second_valid == 8) {
				valid_bitstream = 1;
				break;
			}
			// if not success, change i to start_pos and try find again
			i = start_pos;
		}
	}

	free(data8);

	if (valid_bitstream == 0) {
		fprintf(stderr, "Not valid bitstream\n");
		return -1;
	}

	i -= 48;
	offset = i;
	size -= offset;

	data32 = malloc(size);
	fseek(fd, offset, SEEK_SET);
	readen = fread(data32, size, 1, fd);
	if (readen != 1) {
		free(data32);
		return -1;
	}

	*pdata = data32;
	return size - offset;
}
