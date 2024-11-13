/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
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

struct ndp_queue *ndp_open_queue(struct nfb_device *dev, unsigned index, int dir, int flags);

void ndp_close_queue(struct ndp_queue *q);

typedef int ndp_open_flags_t;
int ndp_base_queue_open(struct nfb_device *dev, void *dev_priv, unsigned index, int dir, ndp_open_flags_t flags, struct ndp_queue ** pq);
void ndp_base_queue_close(void *priv);
