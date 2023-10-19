/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * NDP driver of the NFB platform - private header
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/nfb/ndp.h>
#include <nfb/ext.h>

#ifndef _NDP_QUEUE_H
#define _NDP_QUEUE_H

enum ndp_queue_status {
	NDP_QUEUE_RUNNING,
	NDP_QUEUE_STOPPED,
};

struct ndp_queue {
	void *priv;

	struct ndp_queue_ops ops;
	struct nfb_device *dev;
	enum ndp_queue_status status;
	int numa;
	uint16_t dir;
	uint16_t index;

#ifdef __KERNEL__
	int alloc;
#endif
};

#endif // _NDP_QUEUE_H
