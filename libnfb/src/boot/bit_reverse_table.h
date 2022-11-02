/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - bit reverse table header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Tomas Hak <xhakto01@stud.fit.vutbr.cz>
 */

void nfb_fw_bitstream_reverse_bits_8(void *data, size_t size);
void nfb_fw_bitstream_reverse_bits_16(void *data, size_t size);
