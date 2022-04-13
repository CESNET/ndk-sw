/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP backend network interface driver of the NFB platform - compatibility hdr
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#ifndef NFB_NET_COMPAT_H
#define NFB_NET_COMPAT_H

#include <linux/version.h>

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) (((a) << 8) + (b))
#endif

#if (defined(RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5)) && \
	(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(8, 0)))
#define ndo_change_mtu ndo_change_mtu_rh74
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#define CONFIG_HAS_VOID_NDO_GET_STATS64
#else
#if (defined(RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5)))
#define CONFIG_HAS_VOID_NDO_GET_STATS64
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#define CONFIG_HAS_LINK_KSETTINGS
#else
#if (defined(RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5)))
#define CONFIG_HAS_LINK_KSETTINGS
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
#define CONFIG_HAS_TIMER_SETUP
#endif

#endif /* NFB_NET_COMPAT_H */
