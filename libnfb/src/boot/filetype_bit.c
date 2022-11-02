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
