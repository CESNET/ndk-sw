/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Boot driver of the NFB platform - gecko module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include "../nfb.h"

void nfb_boot_gecko_read_card_type(struct nfb_device *nfb, struct nfb_comp *boot);
void nfb_boot_gecko_read_serial_number(struct nfb_device *nfb, struct nfb_comp *boot);
