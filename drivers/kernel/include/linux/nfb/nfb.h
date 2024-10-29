/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * NFB driver public header file
 *
 * Copyright (C) 2017-2022 CESNET
 *
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef _LINUX_NFB_H_
#define _LINUX_NFB_H_

#include <linux/ioctl.h>

struct nfb_lock {
	char *path;
	uint64_t features;
};

/*
 * Ioctl definitions
 */
#define NFB_LOCK_IOC		'l'
#define NFB_LOCK_IOC_TRY_LOCK  _IOWR(NFB_LOCK_IOC, 2, struct nfb_lock)
#define NFB_LOCK_IOC_UNLOCK    _IOWR(NFB_LOCK_IOC, 3, struct nfb_lock)

#endif /* _LINUX_NFB_H_ */
