/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include "bit_reverse_table.h"

enum bitstream_format {
	BITSTREAM_FORMAT_BPI16,
	BITSTREAM_FORMAT_SPI4,
	BITSTREAM_FORMAT_INTEL_AVST,
	BITSTREAM_FORMAT_INTEL_AS,
	BITSTREAM_FORMAT_NATIVE,
};

ssize_t nfb_fw_open_rpd(FILE *fd, void **pdata, enum bitstream_format f);
ssize_t nfb_fw_open_bit(FILE *fd, void **pdata, enum bitstream_format f);
ssize_t nfb_fw_open_mcs(FILE *fd, void **pdata);
