/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HWMon driver module header of the NFB platform
 *
 * Copyright (C) 2017-2023 CESNET
 * Author(s):
 *   Richard Hyros <hyros@cesnet.cz>
 */

#ifndef NFB_HWMON_H
#define NFB_HWMON_H

int nfb_hwmon_attach(struct nfb_device * nfb, void **priv);

void nfb_hwmon_detach(struct nfb_device * nfb, void *priv);

#endif //NFB_HWMON_H
