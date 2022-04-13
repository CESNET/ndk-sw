/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

enum bitstream_format {
	BITSTREAM_FORMAT_BPI16,
	BITSTREAM_FORMAT_SPI4,
};

ssize_t nfb_fw_open_bit(FILE *fd, void **pdata, enum bitstream_format f);
ssize_t nfb_fw_open_mcs(FILE *fd, void **pdata);
