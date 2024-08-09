/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Flash/MTD memory read tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <err.h>

#include <linux/nfb/boot.h>
#include <nfb/nfb.h>
#include <nfb/boot.h>

#define ARGUMENTS	"d:i:a:s:h"

void usage(char *tool)
{
	printf("Usage: %s [-h] [-d device] [-i mtd_index] [-a offset] [-s size]\n", tool);
}

int main(int argc, char *argv[])
{
	int c;
	int index = 0;
	size_t bs;
	size_t size = 0, address = 0;
	const char *path = nfb_default_dev_path();
	char *buffer;

	struct nfb_device *dev;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'd':
			path = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		case 'i':
			index = strtol(optarg, 0, 0);
			break;
		case 'a':
			address = strtol(optarg, 0, 0);
			break;
		case 's':
			size = strtol(optarg, 0, 0);
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	dev = nfb_open(path);

	bs = nfb_mtd_get_size(dev, index);

	if (address >= bs)
		err(1, "Address out of Flash range");

	if (size == 0 || size > bs - address)
		size = bs - address;

	buffer = malloc(size);
	if (buffer == NULL)
		return -ENOMEM;

	if (nfb_mtd_read(dev, index,  address, buffer, size) != 0)
		err(1, "Can't read data");

	fwrite(buffer, size, 1, stdout);

	free(buffer);

	nfb_close(dev);

	return 0;
}
