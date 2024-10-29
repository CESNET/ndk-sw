/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Interface to the NDP interface
 *
 * Copyright (C) 2017-2022 CESNET
 *
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef _LINUX_NDP_H_FILE_
#define _LINUX_NDP_H_FILE_

#include <linux/ioctl.h>
#include <linux/types.h>

#define NDP_CHANNEL_TYPE_RX             0x00
#define NDP_CHANNEL_TYPE_TX             0x01

/* Discard packets in case of descriptors shortage or application stall */
#define NDP_CHANNEL_FLAG_DISCARD        0x01
/* Channel can be subscribed by one subscriber only */
#define NDP_CHANNEL_FLAG_EXCLUSIVE      0x02
/* Use header buffer for 32b packet header */
#define NDP_CHANNEL_FLAG_USE_HEADER     0x04
/* Use offset buffer for specifying data positions in main buffer */
#define NDP_CHANNEL_FLAG_USE_OFFSET     0x08
/* Do not sync pointers with kernel (library manages the pointers itself); must be used together with flag EXCLUSIVE */
#define NDP_CHANNEL_FLAG_USERSPACE      0x10

/**
 * struct ndp_channel_request
 *
 * @index:	index within the group of same type
 * @type:	type from NDP_CHANNEL_TYPE
 * @flags:	bitmask of specific flags
 * @status:	status bitmask - locked / running / available
 * @numa_node:	node on which the channel is located
 */
struct ndp_channel_request {
	void *id;
	char *path;
	__u32 index;
	__u32 type;
	__u32 flags;
	__u32 status;
};

/**
 * struct ndp_subscription_sync
 *
 * @flags: reserved for future use
 * @size:  total size of locked area
 * @hwpointer: pointer written by hardware
 * @swpointer: pointer written by software
 */
struct ndp_subscription_sync {
	void *id;
	__u32 flags;	// timeout, interrupts? FIXME: unaligned
	__u64 size;
	__u64 hwptr;
	__u64 swptr;
};

/*
 * NDP_IOC_SUBSCRIBE: Subscripe channel selected by index and type
 * 	- reads: index, type, flags
*/

#define NDP_IOC			0xc0
#define NDP_IOC_SUBSCRIBE 	_IOWR(NDP_IOC, 16, struct ndp_channel_request)
#define NDP_IOC_START		_IOWR(NDP_IOC, 17, struct ndp_subscription_sync)
#define NDP_IOC_STOP 		_IOWR(NDP_IOC, 18, struct ndp_subscription_sync)
#define NDP_IOC_SYNC		_IOWR(NDP_IOC, 19, struct ndp_subscription_sync)

#endif /* _LINUX_NDP_H_FILE_*/
