/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * NDP driver of the NFB platform - sync module
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <assert.h>
#include <fcntl.h>
#include <numa.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <err.h>
#include <errno.h>
#include <poll.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <endian.h>
#include <stdint.h>

#include <linux/nfb/ndp.h>
#include <nfb/ndp.h>

#include "../nfb.h"

#include <netcope/ndp_base.h>

#include <netcope/ndp_core.h>
