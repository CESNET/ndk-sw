/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NDP driver of the NFB platform - sync module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/unistd.h>
#include <linux/err.h>
#include <linux/errno.h>

#include <linux/nfb/ndp.h>
#include <nfb/ndp.h>

#include "../nfb.h"

#include "kndp.h"

#include <netcope/ndp.h>

#include <netcope/ndp_base.h>

#include <netcope/ndp_core.h>
