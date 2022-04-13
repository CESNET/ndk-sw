/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - common code
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <numa.h>
#include "common.h"

void delay_usecs(unsigned int us)
{
	struct timespec t1;
	struct timespec t2;

	if (us == 0)
		return;

	t1.tv_sec = (us / 1000000);
	t1.tv_nsec = (us % 1000000) * 1000;

	/* NB: Other variants of sleep block whole process. */
retry:
	if (nanosleep((const struct timespec *)&t1, &t2) == -1)
		if (errno == EINTR) {
			t1 = t2; /* struct copy */
			goto retry;
		}
	return;
}

