/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware bus tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>
#include <time.h>

#include <stdlib.h>

#include <netcope/nccommon.h>
#include <libfdt.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#define ARGUMENTS "d:p:c:i:n:w:alh"

#define BUFFER_SIZE 256

int verbose = 0;
enum command_t {CMD_NONE, CMD_PRINT_COMPONENTS} command;

void usage(const char *me)
{
	printf("Usage: %s [-alh] [-d device] [-p path] [-c compatible] [-i index] [-n count] addr [val]\n", me);
	printf("-d device       Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-p path         Use component with specified path in Device Tree \n");
	printf("-c compatible   Set compatible string to use [default: \"netcope,bus,mi\"]\n");
	printf("-i index        Set index of component specified with compatible [default: 0]\n");
	printf("-n count        Read 'count' (dec) 32bit values [default: 1]\n");
	printf("-a              Print address\n");
	printf("-l              List of available components\n");
	printf("-h              Show this text\n");
	printf("addr            Hexadecimal offset in selected component\n");
	printf("val             Write value 'val' (hex), same as -w val\n");
}

void print_component_list(const void *fdt, int node_offset)
{
	int subnode_offset;
	int proplen;
	const fdt32_t *prop;

	char path[BUFFER_SIZE];
	const char *compatible;

	prop = fdt_getprop(fdt, node_offset, "reg", &proplen);
	if (proplen == sizeof(*prop) * 2) {
		compatible = fdt_getprop(fdt, node_offset, "compatible", NULL);
		if (fdt_get_path(fdt, node_offset, path, BUFFER_SIZE))
			strcpy(path, "N/A");
		if (compatible == NULL)
			compatible = "";
		printf("0x%08x: %-35s %s\n", fdt32_to_cpu(prop[0]), compatible, path);
	}

	fdt_for_each_subnode(subnode_offset, fdt, node_offset) {
		print_component_list(fdt, subnode_offset);
	}
}

int main(int argc, char *argv[])
{
	int c;
	int node;
	const char *path = nfb_default_dev_path();
	struct nfb_device *dev;
	struct nfb_comp *comp;

	int i;
	long param;
	unsigned offset;
	int show_address = 0;
	int do_write = 0;
	int index = 0;
	int count = 1;
	unsigned data;

	const char *compatible_mi = "netcope,bus,mi";
	const char *compatible = compatible_mi;
	const char *dtpath = NULL;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'd':
			path = optarg;
			break;
		case 'c':
			compatible = optarg;
			break;
		case 'p':
			dtpath = optarg;
			break;
		case 'n':
			count = strtoul(optarg, NULL, 10);
			if (count < 0)
				errx(1, "invalid count");
			break;
		case 'a':
			show_address = 1;
			break;
		case 'i':
			if (nc_strtol(optarg, &param) || param < 0)  {
				errx(EXIT_FAILURE, "Wrong index.");
			}
			index = param;
			break;
		case 'l':
			command = CMD_PRINT_COMPONENTS;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		case 'w':
			data = strtoul(optarg, NULL, 16);
			do_write = 1;
			break;
		default:
			errx(1, "unknown argument -%c", optopt);
		}
	}

	argc -= optind;
	argv += optind;

	if (command == CMD_PRINT_COMPONENTS) {
		dev = nfb_open(path);
		if (!dev)
			errx(1, "Can't open device file");

		print_component_list(nfb_get_fdt(dev), fdt_path_offset(nfb_get_fdt(dev), "/firmware"));

		nfb_close(dev);
		return 0;
	}

	if (argc == 0)
		errx(1, "address missing");
	if (argc > 2)
		errx(1, "stray arguments");

	offset = strtoul(argv[0], NULL, 16);

	if (argc == 2) {
		if (do_write)
			errx(1, "inconsistent usage");

		data = strtoul(argv[1], NULL, 16);
		do_write = 1;
	}

	dev = nfb_open(path);
	if (!dev)
		errx(1, "Can't open device file");

	if (dtpath)
		node = fdt_path_offset(nfb_get_fdt(dev), dtpath);
	else
		node = nfb_comp_find(dev, compatible, index);

	comp = nfb_comp_open(dev, node);
	if (comp == NULL) {
		if (dtpath == NULL && compatible == compatible_mi)
			errx(1, "Can't open MI bus, enable debug mode in driver");
		else
			errx(1, "Can't open component, check for valid FDT");
	}

	if (do_write) {
		nfb_comp_write32(comp, offset, data);
	} else {
		for (i = 0; i < count; i++) {
			data = nfb_comp_read32(comp, offset + i * 4);

			if (show_address && (i % 4) == 0)
				printf("%08x: ", offset + i * 4);
			/*else if (index && (i % 8) == 0)
				printf("%02x: ", i);*/
			printf("%08x%c", data,
					(((i + 1) % (show_address ? 4 : 8)) == 0 || i == (count - 1) ? '\n' : ' '));
		}
	}

	nfb_comp_close(comp);
	nfb_close(dev);
	return EXIT_SUCCESS;
}
