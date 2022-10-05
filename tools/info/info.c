/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Basic information tool
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
#include <inttypes.h>

#include <libfdt.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <netcope/adc_sensors.h>
#include <netcope/eth.h>
#include <netcope/nccommon.h>

#define ARGUMENTS "d:q:hvV"

#define BUFFER_SIZE 64

enum commands {
	CMD_PRINT_STATUS,
	CMD_USAGE,
	CMD_VERSION,
};

// this enum need to corespond with queries[] array
enum queries {
	QUERY_PROJECT,
	QUERY_BUILD,
	QUERY_RX,
	QUERY_TX,
	QUERY_ETHERNET,
	QUERY_PORT,
	QUERY_CARD,
	QUERY_PCI,
	QUERY_NUMA,
};
static const char * const queries[] = {
	"project",
	"build",
	"rx",
	"tx",
	"ethernet",
	"port",
	"card",
	"pci",
	"numa"
};

void usage(const char *progname, int verbose)
{
	printf("Usage: %s [-hv] [-d path]\n", progname);
	printf("-d path         Path to device [default: %s]\n", NFB_DEFAULT_DEV_PATH);
	printf("-q query        Get specific informations%s\n", verbose ? "" : " (-v for more info)");
	if (verbose) {
		printf(" * project          Project name\n");
		printf(" * build            Build time\n");
		printf(" * rx               RX queues\n");
		printf(" * tx               TX queues\n");
		printf(" * ethernet         Ethernet channels\n");
		printf(" * port             Ethernet ports\n");
		printf(" * card             Card name\n");
		printf(" * pci              PCI slot\n");
		printf(" * numa             NUMA node\n");
		printf(" example of usage: '-q project,build,card'\n");
	}
	printf("-v              Increase verbosity\n");
	printf("-V              Show version\n");
	printf("-h              Show this text\n");
}

void print_version()
{
#ifdef PACKAGE_VERSION
	printf(PACKAGE_VERSION "\n");
#else
	printf("Unknown\n");
#endif
}

int print_specific_info(struct nfb_device *dev, int query)
{
	int unknown = 0;

	int len;
	int fdt_offset;
	const void *prop = NULL;
	const uint32_t *prop32;
	const void *fdt;

	char buffer[BUFFER_SIZE];
	time_t build_time;

	fdt = nfb_get_fdt(dev);
	fdt_offset = fdt_path_offset(fdt, "/firmware/");

	/* FDT String properties */
	switch (query) {
	case QUERY_PROJECT: prop = fdt_getprop(fdt, fdt_offset, "project-name", &len); break;
	case QUERY_CARD: prop = fdt_getprop(fdt, fdt_offset, "card-name", &len); break;
	case QUERY_PCI:
		fdt_offset = fdt_path_offset(fdt, "/system/device/endpoint0");
		prop = fdt_getprop(fdt, fdt_offset, "pci-slot", &len);
		break;
	default: unknown = 1; break;
	}

	if (unknown == 0) {
		if (len <= 0)
			return -1;

		printf("%s", (const char *)prop);
		return 0;
	}

	unknown = 0;

	/* FDT Number properties */
	switch (query) {
	case QUERY_NUMA:
		fdt_offset = fdt_path_offset(fdt, "/system/device/endpoint0");
		prop32 = fdt_getprop(fdt, fdt_offset, "numa-node", &len);
		break;
	default: unknown = 1; break;
	}

	if (unknown == 0) {
		if (len != sizeof(*prop32))
			return -1;
		printf("%d", fdt32_to_cpu(*prop32));
		return 0;
	}

	/* Others */
	switch (query) {
	case QUERY_BUILD:
		prop32 = fdt_getprop(fdt, fdt_offset, "build-time", &len);
		if (len != sizeof(*prop32))
			return -1;
		build_time = fdt32_to_cpu(*prop32);
		strftime(buffer, BUFFER_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&build_time));
		printf("%s", buffer);
		break;
	case QUERY_RX: printf("%d", ndp_get_rx_queue_count(dev)); break;
	case QUERY_TX: printf("%d", ndp_get_tx_queue_count(dev)); break;
	case QUERY_ETHERNET: printf("%d", nc_eth_get_count(dev)); break;
	case QUERY_PORT:
		len = 0;
		fdt_for_each_compatible_node(fdt, fdt_offset, "netcope,transceiver") {
			len++;
		}
		printf("%d", len);
		break;
	default: return -1;
	}

	return 0;
}

enum pci_bus_speed {
	PCIE_SPEED_2_5GT		= 0x14,
	PCIE_SPEED_5_0GT		= 0x15,
	PCIE_SPEED_8_0GT		= 0x16,
	PCIE_SPEED_16_0GT		= 0x17,
	PCIE_SPEED_32_0GT		= 0x18,
};

const char *pci_speed_string(enum pci_bus_speed speed)
{
	return
		(speed) == PCIE_SPEED_32_0GT ?  "32 GT/s" :
		(speed) == PCIE_SPEED_16_0GT ?  "16 GT/s" :
		(speed) == PCIE_SPEED_8_0GT  ?   "8 GT/s" :
		(speed) == PCIE_SPEED_5_0GT  ?   "5 GT/s" :
		(speed) == PCIE_SPEED_2_5GT  ? "2.5 GT/s" :
		"Unknown speed";
}

void print_endpoint_info(struct nfb_device *dev, int fdt_offset)
{
	int len;
	int dev_id = -1;
	const void *prop;
	const uint32_t *prop32;
	const void *fdt;
	const char* node_name;

	fdt = nfb_get_fdt(dev);

	node_name = fdt_get_name(fdt, fdt_offset, NULL);
	if (node_name && strlen(node_name) > 8 && strncmp(node_name, "endpoint", 8) == 0) {
		dev_id = strtoul(node_name+8, NULL, 10);
	}

	printf("PCIe Endpoint %d:\n", dev_id);

	prop = fdt_getprop(fdt, fdt_offset, "pci-slot", &len);
	if (prop)
		printf(" * PCI slot                : %s\n", (const char*)prop);

	prop32 = fdt_getprop(fdt, fdt_offset, "pci-speed", &len);
	if (prop32)
		printf(" * PCI speed               : %s\n", pci_speed_string(fdt32_to_cpu(*prop32)));

	prop32 = fdt_getprop(fdt, fdt_offset, "pcie-link-width", &len);
	if (prop32)
		printf(" * PCI link width          : x%d\n", fdt32_to_cpu(*prop32));

	prop32 = fdt_getprop(fdt, fdt_offset, "numa-node", &len);
	if (len == sizeof(*prop32))
		printf(" * NUMA node               : %i\n", fdt32_to_cpu(*prop32));
}

void print_common_info(struct nfb_device *dev, int verbose)
{
	int i;
	int len;
	int node;
	int fdt_offset;
	int count1, count2;
	const void *prop;
	const uint32_t *prop32;
	const uint64_t *prop64;
	const void *fdt;

	char buffer[BUFFER_SIZE];
	time_t build_time;

	fdt = nfb_get_fdt(dev);
	printf("--------------------------------------- Board info ----\n");

	fdt_offset = fdt_path_offset(fdt, "/board/");
	prop = fdt_getprop(fdt, fdt_offset, "card-name", &len);
	if (len > 0)
		printf("Card name                  : %s\n", (const char *)prop);

	prop32 = fdt_getprop(fdt, fdt_offset, "serial-number", &len);
	if (len == sizeof(*prop32))
		printf("Serial number              : %d\n", fdt32_to_cpu(*prop32));

	prop64 = fdt_getprop(fdt, fdt_offset, "fpga-uid", &len);
	if (verbose > 1 && len == sizeof(*prop64))
		printf("FPGA unique ID             : 0x%" PRIx64 "\n", fdt64_to_cpu(*prop64));

	i = 0;
	fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
		i++;
	}
	printf("Network interfaces         : %d\n", i);
	if (verbose) {
		i = 0;
		fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
			prop = fdt_getprop(fdt, node, "type", &len);
			printf(" * Interface %d             : %s\n", i,
					len > 0 ? (const char *) prop : "Unknown");
			i++;
		}
	}

	if (verbose) {
		printf("Temperature                : %.1f C\n", nc_adc_sensors_get_temp(dev));
	}

	printf("------------------------------------ Firmware info ----\n");

	fdt_offset = fdt_path_offset(fdt, "/firmware/");

	prop = fdt_getprop(fdt, fdt_offset, "project-name", &len);
	if (len > 0)
		printf("Project name               : %s\n", (const char *)prop);

	prop = fdt_getprop(fdt, fdt_offset, "project-variant", &len);
	if (len > 0)
		printf("Project variant            : %s\n", (const char *)prop);

	prop = fdt_getprop(fdt, fdt_offset, "project-version", &len);
	if (len > 0)
		printf("Project version            : %s\n", (const char *)prop);

	prop32 = fdt_getprop(fdt, fdt_offset, "build-time", &len);
	if (len == sizeof(*prop32)) {
		build_time = fdt32_to_cpu(*prop32);
		strftime(buffer, BUFFER_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&build_time));
		printf("Built at                   : %s\n", buffer);
	}

	prop = fdt_getprop(fdt, fdt_offset, "build-tool", &len);
	if (len > 0)
		printf("Build tool                 : %s\n", (const char *)prop);

	prop = fdt_getprop(fdt, fdt_offset, "build-author", &len);
	if (len > 0)
		printf("Build author               : %s\n", (const char *)prop);

	count1 = ndp_get_rx_queue_count(dev);
	count2 = ndp_get_rx_queue_available_count(dev);
	if (count1 != count2)
		printf("RX queues                  : %d (only %d available)\n", count1, count2);
	else
		printf("RX queues                  : %d\n", count1);

	count1 = ndp_get_tx_queue_count(dev);
	count2 = ndp_get_tx_queue_available_count(dev);
	if (count1 != count2)
		printf("TX queues                  : %d (only %d available)\n", count1, count2);
	else
		printf("TX queues                  : %d\n", count1);

	printf("ETH channels               : %d\n", nc_eth_get_count(dev));

	if (verbose) {
		i = 0;
		fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
			fdt_offset = fdt_node_offset_by_phandle_ref(fdt, node, "pcspma");
			prop = fdt_getprop(fdt, fdt_offset, "type", &len);
			printf(" * Channel %d               : %s\n", i,
					len > 0 ? (const char *) prop : "Unknown");
			i++;
		}
	}

	printf("-------------------------------------- System info ----\n");
	fdt_offset = fdt_path_offset(fdt, "/system/device/");

	fdt_for_each_subnode(node, fdt, fdt_offset) {
		print_endpoint_info(dev, node);
	}
}

int main(int argc, char *argv[])
{
	int c;
	int ret = EXIT_SUCCESS;

	char *path = NFB_DEFAULT_DEV_PATH;
	struct nfb_device *dev;
	const char *query = NULL;
	char *index;
	int size;

	enum commands command = CMD_PRINT_STATUS;
	int verbose = 0;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'd':
			path = optarg;
			break;
		case 'h':
			command = CMD_USAGE;
			break;
		case 'v':
			verbose++;
			break;
		case 'q':
			query = optarg;
			break;
		case 'V':
			command = CMD_VERSION;
			break;
		default:
			errx(1, "unknown argument -%c", optopt);
		}
	}

	if (command == CMD_USAGE) {
		usage(argv[0], verbose);
		return 0;
	} else if (command == CMD_VERSION) {
		print_version();
		return 0;
	}
	argc -= optind;
	argv += optind;

	if (argc)
		errx(1, "stray arguments");

	dev = nfb_open(path);
	if (!dev)
		errx(1, "can't open device file");

	if (query) {
		size = nc_query_parse(query, queries,  NC_ARRAY_SIZE(queries), &index);
		if (size <= 0) {
			nfb_close(dev);
			return -1;
		}
		for (int i=0; i<size; ++i) {
			ret = print_specific_info(dev, index[i]);
			if (ret) {
				free(index);
				nfb_close(dev);
				return ret;
			}
			printf("\n");
		}
		free(index);
	} else {
		print_common_info(dev, verbose);
	}

	nfb_close(dev);
	return ret;
}
