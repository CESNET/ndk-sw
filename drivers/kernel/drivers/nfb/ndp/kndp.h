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

#include <netcope/ndp_core_queue.h>

struct ndp_queue;
int ndp_ctrl_v2_get_vmaps(struct ndp_channel *channel, void **hdr, void **off);

struct ndp_queue *ndp_open_queue(struct nfb_device *dev, unsigned index, int dir, int flags);

void ndp_close_queue(struct ndp_queue *q);
