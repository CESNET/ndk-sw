/* SPDX-License-Identifier: BSD-3-Clause */
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

#define ARGUMENTS "d:p:c:i:n:w:ablh"

#define BUFFER_SIZE 256

int verbose = 0;
enum command_t {CMD_NONE, CMD_PRINT_COMPONENTS} command;

int xdigittoint(const char c)
{
	if (c > 'f')
		return -1;
	if (c >= 'a')
		return c - 'a' + 10;

	if (c > 'F')
		return -1;
	if (c >= 'A')
		return c - 'A' + 10;

	if (c > '9')
		return -1;
	if (c >= '0')
		return c - '0';

	return -1;
}

void usage(const char *me)
{
	printf("Usage: %s [-alh] [-d device] [-p path] [-c compatible] [-i index] [-n count] addr [val]\n", me);
	printf("-d device       Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-p path         Use component with specified path in Device Tree \n");
	printf("-c compatible   Set compatible string to use [default: \"netcope,bus,mi\"]\n");
	printf("-i index        Set index of component specified with compatible [default: 0]\n");
	printf("-n count        Read 'count' (dec) of N-byte values [default: 1]\n");
	printf("-a              Print address\n");
	printf("-b              Switch from dword mode (N=4) to byte mode (N=1)\n");
	printf("-l              List of available components\n");
	printf("-h              Show this text\n");
	printf("addr            Hexadecimal offset in selected component\n");
	printf("val             Write value 'val' (hex), same as -w val\n");
	printf("\n");
	printf("The input and output format differ depending on the selected mode:\n");
	printf(" - dword mode (default): hexadecimal number(s); LSB corresponds to the lower adress\n");
	printf(" - byte mode (with -b): hexadecimal char stream; leftmost byte corresponds to the lowest address\n");
	printf("\n");
	printf("Examples:\n");
	printf("%s -b 2 010203  Write 3 bytes to address 2\n", me);
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
	int ret;
	int c;
	int node;
	const char *path = nfb_default_dev_path();
	struct nfb_device *dev;
	struct nfb_comp *comp;

	int i;
	long param;
	unsigned offset;
	int show_address = 0;
	int use_32b = 1;
	int index = 0;
	long count = 1;
	char xc1, xc2;
	/* print newline each N bytes in read dword mode */
	unsigned newline_span = 32;
	char spacer;

	const char *wdata = NULL;
	uint8_t *data;
	uint32_t data32;

	const char *compatible_mi = "netcope,bus,mi";
	const char *compatible = compatible_mi;
	const char *dtpath = NULL;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'b':
			use_32b = 0;
			break;
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
			if (nc_strtol(optarg, &count))
				errx(1, "invalid count");
			break;
		case 'a':
			show_address = 1;
			newline_span = 16;
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
			wdata = optarg;
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
		if (wdata)
			errx(1, "inconsistent usage");

		wdata = argv[1];
	}

	/* dword mode supports only one write */
	if (use_32b && wdata)
		count = 1;

	if (use_32b) {
		count *= 4;
	} else if (wdata) {
		count = strlen(wdata);
		if (count & 1)
			errx(1, "Incomplete input data (1 hex byte = 2 characters)");

		/* Skip optional leading 0x */
		if (count >= 2 && (strncmp(wdata, "0x", 2) == 0 || strncmp(wdata, "0X", 2) == 0)) {
			wdata += 2;
			count -= 2;
		}

		count /= 2;
	}

	data = malloc(count);
	if (data == NULL)
		errx(1, "Memory allocation error");

	if (wdata) {
		/* Parse input string and fill to the data buffer */
		if (use_32b) {
			data32 = strtoul(wdata, NULL, 16);
			memcpy(data, &data32, count);
		} else {
			for (i = 0; i < count; i++) {
				xc1 = xdigittoint(wdata[i * 2 + 0]);
				xc2 = xdigittoint(wdata[i * 2 + 1]);
				if (xc1 < 0 || xc2 < 0)
					errx(1, "Non hexadecimal value at input");

				data[i] = (xc1 << 4) | (xc2 << 0);
			}
		}
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

	/* Check boundaries with zero-sized write. */
	if (nfb_comp_write(comp, NULL, 0, offset + count) != 0) {
		errx(1, "Required address space is outside the component range");
	}

	if (wdata) {
		ret = nfb_comp_write(comp, data, count, offset);
		if (ret != count)
			errx(1, "An error while write");
	} else {
		ret = nfb_comp_read(comp, data, count, offset);
		if (ret != count)
			errx(1, "An error while read");

		if (use_32b) {
			for (i = 0; i < count; i += 4) {
				spacer = (i % newline_span) ? ' ' : '\n';
				if (i)
					printf("%c", spacer);

				if (show_address && spacer == '\n')
					printf("%08x: ", offset + i * 4);

				memcpy(&data32, data + i, 4);
				printf("%08x", data32);
			}
		} else {
			for (i = 0; i < count; i++) {
				printf("%02x", data[i]);
			}
		}
		printf("\n");
	}
	free(data);

	nfb_comp_close(comp);
	nfb_close(dev);
	return EXIT_SUCCESS;
}
