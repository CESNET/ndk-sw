/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MDIO control tool
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

#include <nfb/nfb.h>
#include <netcope/nccommon.h>
#include <netcope/mdio.h>

#define ARGUMENTS       "d:i:p:lh"
#define BUFFER_SIZE     256

enum command_t {CMD_NONE, CMD_PRINT_COMPONENTS} command;

void usage(char *tool)
{
	printf("Usage: %s [-h] [-d device] [-l] [-i mdio_index] [-p port_addr] reg [value]\n", tool);
	printf("-d device       Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-i index        Set index of MDIO component [default: 0]\n");
	printf("-p port_addr    Access to specific port on MDIO controller [default: 0]\n");
	printf("reg             Device and register address in format D.R\n");
	printf("value           Value to write\n");
	printf("-l              List of available MDIO components\n");
	printf("-h              Show this text\n");
}

int mdio_list(const void *fdt, int node_offset, int *index)
{
	int ret = -FDT_ERR_NOTFOUND;
	int subnode_offset;
	const fdt32_t *prop;
	int proplen;

	char path[BUFFER_SIZE];
	const char *compatible;

	compatible = fdt_getprop(fdt, node_offset, "compatible", NULL);
	if (compatible != NULL && (
			strcmp(COMP_NETCOPE_DMAP, compatible) == 0 ||
			strcmp(COMP_NETCOPE_MDIO, compatible) == 0)) {
		/* Negative index means print all MDIOs */
		if (*index < 0) {
			if (fdt_get_path(fdt, node_offset, path, BUFFER_SIZE))
				strcpy(path, "N/A");
			prop = fdt_getprop(fdt, node_offset, "reg", &proplen);
			printf("[%d] 0x%08x: %-35s %s\n", (-*index - 1), fdt32_to_cpu(prop[0]), compatible, path);
		} else if(*index == 0) {
			return node_offset;
		}

		(*index)--;
	}

	fdt_for_each_subnode(subnode_offset, fdt, node_offset) {
		ret = mdio_list(fdt, subnode_offset, index);
		if (ret >= 0)
			return ret;
	}
	return ret;
}


int main(int argc, char *argv[])
{
	int c;
	int index = 0;
	int do_write = 0;
	int port_addr = 0, dev_addr, reg_addr;
	const char *path = nfb_default_dev_path();
	uint16_t val = 0;

	int node;
	struct nfb_device *dev;
	struct nc_mdio *mdio;

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
		case 'p':
			port_addr = strtol(optarg, 0, 0);
			break;
		case 'l':
			command = CMD_PRINT_COMPONENTS;
			index = -1;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	argc -= optind;
	argv += optind;

	if (command == CMD_PRINT_COMPONENTS) {
		dev = nfb_open(path);
		if (!dev)
			errx(1, "Can't open device file");

		mdio_list(nfb_get_fdt(dev), fdt_path_offset(nfb_get_fdt(dev), "/firmware"), &index);

		nfb_close(dev);
		return 0;
	}


	if (argc == 0)
		errx(1, "address missing");
	if (argc > 2)
		errx(1, "stray arguments");

	if (sscanf(argv[0], "%d.%d", &dev_addr, &reg_addr) != 2)
		errx(-1, "Cannot parse register address as format 'dev.reg'\n");

	if (argc == 2) {
		val = nc_xstrtoul(argv[1], 16);
		do_write = 1;
	}

	dev = nfb_open(path);
	if (!dev)
		errx(1, "Can't open device file");

	node = mdio_list(nfb_get_fdt(dev), fdt_path_offset(nfb_get_fdt(dev), "/firmware"), &index);
	mdio = nc_mdio_open(dev, node, -1);
	if (mdio == NULL) {
		errx(1, "Can't open MDIO");
	}

	if (do_write) {
		nc_mdio_write(mdio, port_addr, dev_addr, reg_addr, val);
	} else {
		val = nc_mdio_read(mdio, port_addr, dev_addr, reg_addr);
		printf("%04x\n", val);
	}

	nc_mdio_close(mdio);
	nfb_close(dev);

	return 0;
}
