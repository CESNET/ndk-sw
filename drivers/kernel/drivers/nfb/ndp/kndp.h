/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP driver of the NFB platform - public header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/nfb/ndp.h>
#include "ndp.h"

struct ndp_queue;
int ndp_ctrl_v2_get_vmaps(struct ndp_channel *channel, void **hdr, void **off);

int ndp_queue_open_init(struct nfb_device *dev, struct ndp_queue *q, unsigned index, int type);

void ndp_close_queue(struct ndp_queue *q);
