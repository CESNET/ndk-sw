/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - raw programming data file type
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Tomas Hak <xhakto01@stud.fit.vutbr.cz>
 */

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "boot.h"

ssize_t nfb_fw_open_rpd_raw(FILE *fd, void **pdata)
{
	ssize_t size;
	ssize_t data_read;
	uint32_t *data32;

	/* get size of whole bitstream file in bytes */
	fseek(fd, 0, SEEK_END);
	size = ftell(fd);
	rewind(fd);

	data32 = malloc(size);
	data_read = fread(data32, size, 1, fd);
	if (data_read != 1) {
		free(data32);
		return -1;
	}

	*pdata = data32;
	return size;
}

ssize_t nfb_fw_open_rpd(FILE *fd, void **pdata, enum bitstream_format f)
{
	ssize_t ret;
	ret = nfb_fw_open_rpd_raw(fd, pdata);
	if (ret > 0 && f == BITSTREAM_FORMAT_INTEL_AS) {
		nfb_fw_bitstream_reverse_bits_8(*pdata, ret);
	}
	return ret;
}
