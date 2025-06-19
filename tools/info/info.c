/* SPDX-License-Identifier: BSD-3-Clause */
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
#include <dirent.h>

#include <libfdt.h>
#include <nfb/nfb.h>
#include <nfb/ndp.h>

#include <nfb/boot.h>
#include <netcope/info.h>
#include <netcope/adc_sensors.h>
#include <netcope/eth.h>
#include <netcope/nccommon.h>

#include <netcope/ni.h>

#define ARGUMENTS "d:q:hjlvV"

#define BUFFER_SIZE 64

#define NFB_PATH_MAXLEN 64
#define NFB_BASE_DEV_PATH "/dev/nfb/"

enum commands {
	CMD_PRINT_STATUS,
	CMD_USAGE,
	CMD_VERSION,
	CMD_LIST,
};

// this enum need to corespond with queries[] array
enum queries {
	QUERY_PROJECT,
	QUERY_PROJECT_VARIANT,
	QUERY_PROJECT_VERSION,
	QUERY_BUILD,
	QUERY_RX,
	QUERY_TX,
	QUERY_ETHERNET,
	QUERY_PORT,
	QUERY_CARD,
	QUERY_PCI,
	QUERY_PCIPATH,
	QUERY_PCIPATH_S,
	QUERY_DEFAULT_DEV,
	QUERY_DEFAULT_DEV_S,
	QUERY_NUMA,
};
static const char * const queries[] = {
	"project",
	"project-variant",
	"project-version",
	"build",
	"rx",
	"tx",
	"ethernet",
	"port",
	"card",
	"pci",
	"pci-path",
	"pp",
	"default-dev",
	"dd",
	"numa"
};

void usage(const char *progname, int verbose)
{
	printf("Usage: %s [-hv] [-d path]\n", progname);
	printf("-d path         Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-q query        Get specific informations%s\n", verbose ? "" : " (-v for more info)");
	if (verbose) {
		printf(" * project          Project name\n");
		printf(" * project-variant  Project variant\n");
		printf(" * project-version  Project version\n");
		printf(" * build            Build time\n");
		printf(" * rx               RX queues\n");
		printf(" * tx               TX queues\n");
		printf(" * ethernet         Ethernet channels\n");
		printf(" * port             Ethernet ports\n");
		printf(" * card             Card name\n");
		printf(" * pci              PCI slot\n");
		printf(" * pci-path / pp    Fixed path to device\n");
		printf(" * default-dev / dd Command for setting as default device in current shell\n");
		printf(" * numa             NUMA node\n");
		printf(" example of usage: '-q project,build,card'\n");
	}
	printf("-l              Print list of available devices\n");
	printf("-j              Print output in JSON\n");
	printf("-v              Increase verbosity\n");
	printf("-V              Show version\n");
	printf("-h              Show this text\n");
}

NI_DEFAULT_ITEMS(ni_common_item_callbacks, )


enum NI_ITEMS {
	NI_SEC_ROOT = 0,
	NI_SEC0_BOARD_INFO,
	NI_BOARD_NAME,
	NI_SERIAL_NUMBER,
	NI_FPGA_UNIQUE_ID,
	NI_NET_IFCS,
	NI_LIST_NET_IFCS,
	NI_SEC1_NET_IFCS,
	NI_IFC_ID,
	NI_IFC_TYPE,
	NI_TEMPERATURE,
	NI_SEC0_FIRMWARE,
	NI_CARD_NAME,
	NI_PROJECT_NAME,
	NI_PROJECT_VARIANT,
	NI_PROJECT_VERSION,
	NI_BUILT_TIME,
	NI_BUILT_TIMESTAMP,
	NI_BUILD_TOOL,
	NI_BUILD_AUTHOR,
	NI_BUILD_REVISION,
	NI_RX_QUEUES_ALL,
	NI_RX_QUEUES_EQ_AV,
	NI_RX_QUEUES_NEQ_AV,
	NI_TX_QUEUES_ALL,
	NI_TX_QUEUES_EQ_AV,
	NI_TX_QUEUES_NEQ_AV,
	NI_ETH_CHANNELS,
	NI_LIST_ETH_CHANNELS,
	NI_SEC1_ETH_CHANNELS,
	NI_ETH_CHANNEL_ID,
	NI_ETH_CHANNEL_TYPE,
	NI_SEC0_SYSTEM,
	NI_SEC1_PCIEP,
	NI_LIST_PCIEP,
	NI_PCI_ID,
	NI_PCI_SLOT,
	NI_PCI_LINK_SPEED,
	NI_PCI_LINK_WIDTH,
	NI_NUMA,
	NI_LIST_PCI_BAR,
	NI_SEC2_PCI_BAR,
	NI_BAR_ID,
	NI_BAR_SIZE_STR,
	NI_BAR_SIZE,

	/* Card list */
	NI_LIST_NFB,
	NI_SEC_LIST_NFB,
	NI_L_NFB_ID,
	NI_L_LPATH,
	NI_L_BFN,
	NI_L_CARD_NAME,
	NI_L_SN,
	NI_L_PROJECT_NAME,
	NI_L_PROJECT_VAR,
	NI_L_PROJECT_VER,
	NI_L_PROJECT_END,
};

#define NUF_N   (NI_USER_ITEM_F_NO_NEWLINE)
#define NUF_NDA (NI_USER_ITEM_F_NO_NEWLINE | NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN)
#define NUF_DA  (NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN)
#define NUF_DAV (NI_USER_ITEM_F_NO_DELIMITER | NI_USER_ITEM_F_NO_ALIGN | NI_USER_ITEM_F_NO_VALUE)
#define NUFA(x) ni_user_f_align(x)
#define NUFW(x) ni_user_f_width(x)
#define NJFD(x) ni_json_f_decim(x)

struct ni_context_item_default ni_items[] = {
	[NI_SEC_ROOT]           = {ni_json_e,                           ni_user_n},

	[NI_SEC0_BOARD_INFO]    = {ni_json_k("board"),                  ni_user_l("Board info")},
	[NI_BOARD_NAME]         = {ni_json_k("board_name"),             ni_user_l("Board name")},
	[NI_SERIAL_NUMBER]      = {ni_json_k("serial_number"),          ni_user_l("Serial number")},
	[NI_FPGA_UNIQUE_ID]     = {ni_json_k("fpga_unique_id"),         ni_user_l("FPGA unique ID")},
	[NI_NET_IFCS]           = {ni_json_k("network_interfaces"),     ni_user_l("Network interfaces")},

	[NI_LIST_NET_IFCS]      = {ni_json_k("interfaces"),             ni_user_n},
	[NI_SEC1_NET_IFCS]      = {ni_json_e,                           ni_user_n},
	[NI_IFC_ID]             = {ni_json_k("id"),                     ni_user_f(" * Interface ", NUF_NDA)},
	[NI_IFC_TYPE]           = {ni_json_k("type"),                   ni_user_f("", 0)},
	[NI_TEMPERATURE]        = {ni_json_f("temperature", NJFD(1)),   ni_user_v("Temperature", ni_user_f_decim(1), NULL, " C")},
	[NI_SEC0_FIRMWARE]      = {ni_json_k("firmware"),               ni_user_l("Firmware info")},
	[NI_CARD_NAME]          = {ni_json_k("card_name"),              ni_user_l("Card name")},
	[NI_PROJECT_NAME]       = {ni_json_k("project_name"),           ni_user_l("Project name")},
	[NI_PROJECT_VARIANT]    = {ni_json_k("project_variant"),        ni_user_l("Project variant")},
	[NI_PROJECT_VERSION]    = {ni_json_k("project_version"),        ni_user_l("Project version")},
	[NI_BUILT_TIME]         = {ni_json_n,                           ni_user_l("Built at")},
	[NI_BUILT_TIMESTAMP]    = {ni_json_k("build_time"),             ni_user_n},
	[NI_BUILD_TOOL]         = {ni_json_k("build_tool"),             ni_user_l("Build tool")},
	[NI_BUILD_AUTHOR]       = {ni_json_k("build_author"),           ni_user_l("Build author")},
	[NI_BUILD_REVISION]     = {ni_json_k("build_revision"),         ni_user_l("Build revision")},

	[NI_RX_QUEUES_ALL]      = {ni_json_k("rx_queues"),              ni_user_f("RX queues", NUF_N)},
	[NI_RX_QUEUES_EQ_AV]    = {ni_json_k("rx_queues_available"),    ni_user_f(NULL, NUF_DAV)},
	[NI_RX_QUEUES_NEQ_AV]   = {ni_json_k("rx_queues_available"),    ni_user_v(" (only ", NUF_DA, NULL, " available)")},

	[NI_TX_QUEUES_ALL]      = {ni_json_k("tx_queues"),              ni_user_f("TX queues", NUF_N)},
	[NI_TX_QUEUES_EQ_AV]    = {ni_json_k("tx_queues_available"),    ni_user_f(NULL, NUF_DAV)},
	[NI_TX_QUEUES_NEQ_AV]   = {ni_json_k("tx_queues_available"),    ni_user_v(" (only ", NUF_DA, NULL, " available)")},

	[NI_ETH_CHANNELS]       = {ni_json_n,                           ni_user_l("ETH channels")},
	[NI_LIST_ETH_CHANNELS]  = {ni_json_k("eth_channels"),           ni_user_l(NULL)},
	[NI_SEC1_ETH_CHANNELS]  = {ni_json_e,                           ni_user_l(NULL)},
	[NI_ETH_CHANNEL_ID]     = {ni_json_k("id"),                     ni_user_f(" * Channel ", NUF_NDA)},
	[NI_ETH_CHANNEL_TYPE]   = {ni_json_k("type"),                   ni_user_l("")},

	[NI_SEC0_SYSTEM]        = {ni_json_k("system"),                 ni_user_l("System info")},
	[NI_SEC1_PCIEP]         = {ni_json_e,                           ni_user_l(NULL)},
	[NI_LIST_PCIEP]         = {ni_json_k("pci"),                    ni_user_l(NULL)},
	[NI_PCI_ID]             = {ni_json_k("id"),                     ni_user_v("PCIe Endpoint ", NUF_DA, NULL, ":")},
	[NI_PCI_SLOT]           = {ni_json_k("pci_bdf"),                ni_user_l(" * PCI slot")},
	[NI_PCI_LINK_SPEED]     = {ni_json_k("pci_link_speed_str"),     ni_user_l(" * PCI link speed")},
	[NI_PCI_LINK_WIDTH]     = {ni_json_k("pci_link_width"),         ni_user_v(" * PCI link width", 0, "x", NULL)},
	[NI_NUMA]               = {ni_json_k("numa"),                   ni_user_l(" * NUMA node")},
	[NI_LIST_PCI_BAR]       = {ni_json_k("bar"),                    ni_user_l(NULL)},
	[NI_SEC2_PCI_BAR]       = {ni_json_e,                           ni_user_l(NULL)},
	[NI_BAR_ID]             = {ni_json_k("id"),                     ni_user_v(" * MI BAR ", NUF_NDA, NULL, " size ")},
	[NI_BAR_SIZE_STR]       = {ni_json_n,                           ni_user_f("", 0)},
	[NI_BAR_SIZE]           = {ni_json_k("size"),                   ni_user_n},

	/* Card list */
	[NI_LIST_NFB]           = {ni_json_k("card_list"),
		ni_user_v("ID  Base path   PCI address   Card name         Serial number   Firmware info - project", 0, NULL, "\n")},
	[NI_SEC_LIST_NFB]       = {ni_json_e,                           ni_user_v(NULL, 0, NULL, "  ")},
	[NI_L_NFB_ID]           = {ni_json_k("id"),                     ni_user_f("", NUF_NDA | NUFW(2))},
	[NI_L_LPATH]            = {ni_json_k("path"),                   ni_user_v("", NUF_NDA | NUFA(-10), "  ", NULL)},
	[NI_L_BFN]              = {ni_json_k("pci_bdf"),                ni_user_v("", NUF_NDA | NUFA(-12), "  ", NULL)},
	[NI_L_CARD_NAME]        = {ni_json_k("card_name"),              ni_user_v("", NUF_NDA | NUFA(-16), "  ", NULL)},
	[NI_L_SN]               = {ni_json_k("serial_number"),          ni_user_v("", NUF_NDA | NUFA(-14), "  ", NULL)},
	[NI_L_PROJECT_NAME]     = {ni_json_k("project_name"),           ni_user_v("", NUF_NDA | NUFA(0), "  ", NULL)},
	[NI_L_PROJECT_VAR]      = {ni_json_k("project_variant"),        ni_user_v("", NUF_NDA | NUFA(0), "  ", NULL)},
	[NI_L_PROJECT_VER]      = {ni_json_k("project_version"),        ni_user_v("", NUF_NDA | NUFA(0), "  ", NULL)},
	[NI_L_PROJECT_END]      = {ni_json_n,                           ni_user_f("", NUF_DAV)},
};

void print_version()
{
#ifdef PACKAGE_VERSION
	printf(PACKAGE_VERSION "\n");
#else
	printf("Unknown\n");
#endif
}

void print_device_list(struct ni_context *ctx)
{
	int ret;
	int len;
	int fdt_offset;

	DIR * d;
	struct dirent *dir;
	struct nc_composed_device_info info;
	struct nfb_device *dev;

	char path[NFB_PATH_MAXLEN];
	char lpath[PATH_MAX];

	const void *fdt;
	const char *prop;
	const uint32_t *prop32;

	d = opendir(NFB_BASE_DEV_PATH "by-pci-slot/");
	ni_list(ctx, NI_LIST_NFB);
	while (d && (dir = readdir(d)) != NULL) {
		ret = snprintf(path, NFB_PATH_MAXLEN, NFB_BASE_DEV_PATH "by-pci-slot/%s", dir->d_name);
		if (ret <= 0 || ret >= NFB_PATH_MAXLEN) {
			continue;
		}

		realpath(path, lpath);

		dev = nfb_open(path);
		if (dev) {
			ni_section(ctx, NI_SEC_LIST_NFB);

			fdt = nfb_get_fdt(dev);
			fdt_offset = fdt_path_offset(fdt, "/firmware/");

			ret = nc_get_composed_device_info_by_pci(dev, NULL, &info);

			ni_item_int(ctx, NI_L_NFB_ID, ret == 0 ? info.nfb_id : -1);
			ni_item_str(ctx, NI_L_LPATH, lpath);
			ni_item_str(ctx, NI_L_BFN, dir->d_name);
			ni_fdt_prop_str(ctx, NI_L_CARD_NAME, fdt, fdt_offset, "card-name", &len);

			fdt_offset = fdt_path_offset(fdt, "/board/");

			prop = NULL;
			prop32 = fdt_getprop(fdt, fdt_offset, "serial-number", &len);
			if (len == sizeof(*prop32)) {
				snprintf(path, NFB_PATH_MAXLEN, "%d", fdt32_to_cpu(*prop32));
				prop = path;
			} else {
				prop = fdt_getprop(fdt, fdt_offset, "serial-number-string", &len);
			}
			if (prop)
				ni_item_str(ctx, NI_L_SN, prop);

			fdt_offset = fdt_path_offset(fdt, "/firmware/");
			ni_fdt_prop_str(ctx, NI_L_PROJECT_NAME, fdt, fdt_offset, "project-name", &len);
			ni_fdt_prop_str(ctx, NI_L_PROJECT_VAR, fdt, fdt_offset, "project-variant", &len);
			ni_fdt_prop_str(ctx, NI_L_PROJECT_VER, fdt, fdt_offset, "project-version", &len);
			ni_item_int(ctx, NI_L_PROJECT_END, 0);
			nfb_close(dev);

			ni_endsection(ctx, NI_SEC_LIST_NFB);
		}
	}

	ni_endlist(ctx, NI_LIST_NFB);
}

int print_specific_info(struct nfb_device *dev, int query)
{
	int unknown = 0;

	int len;
	int fdt_offset;
	const void *prop = NULL;
	const uint32_t *prop32;
	const void *fdt;

	const char *prefix = "";

	char buffer[BUFFER_SIZE];
	time_t build_time;

	fdt = nfb_get_fdt(dev);
	fdt_offset = fdt_path_offset(fdt, "/firmware/");

	switch (query) {
	case QUERY_PCIPATH:
	case QUERY_PCIPATH_S:
		prefix = "/dev/nfb/by-pci-slot/";
		query = QUERY_PCI;
		break;
	case QUERY_DEFAULT_DEV:
	case QUERY_DEFAULT_DEV_S:
		prefix = "export LIBNFB_DEFAULT_DEV=/dev/nfb/by-pci-slot/";
		query = QUERY_PCI;
		break;
	}

	/* FDT String properties */
	switch (query) {
	case QUERY_PROJECT: prop = fdt_getprop(fdt, fdt_offset, "project-name", &len); break;
	case QUERY_PROJECT_VARIANT: prop = fdt_getprop(fdt, fdt_offset, "project-variant", &len); break;
	case QUERY_PROJECT_VERSION: prop = fdt_getprop(fdt, fdt_offset, "project-version", &len); break;
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

		printf("%s%s", prefix, (const char *)prop);
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

void sprint_size(char *str, unsigned long size)
{
	static const char *units[] = {
		"B", "KiB", "MiB", "GiB"};
	unsigned int i = 0;
	while (size >= 1024 && i < NC_ARRAY_SIZE(units)) {
		size >>= 10;
		i++;
	}
	sprintf(str, "%lu %s", size, units[i]);
}

void print_endpoint_info(struct nfb_device *dev, int fdt_offset, struct ni_context *ctx)
{
	int len;
	int bar;
	int dev_id = -1;
	uint64_t bar_size;
	const uint32_t *prop32;
	const uint64_t *prop64;
	const void *fdt;
	const char* node_name;
	char nodename[64];

	fdt = nfb_get_fdt(dev);

	node_name = fdt_get_name(fdt, fdt_offset, NULL);
	if (node_name && strlen(node_name) > 8 && strncmp(node_name, "endpoint", 8) == 0) {
		dev_id = strtoul(node_name+8, NULL, 10);
	}

	ni_item_int(ctx, NI_PCI_ID, dev_id);

	ni_fdt_prop_str(ctx, NI_PCI_SLOT, fdt, fdt_offset, "pci-slot", &len);
	prop32 = fdt_getprop(fdt, fdt_offset, "pci-speed", &len);
	if (prop32)
		ni_item_str(ctx, NI_PCI_LINK_SPEED, pci_speed_string(fdt32_to_cpu(*prop32)));
	ni_fdt_prop_32(ctx, NI_PCI_LINK_WIDTH, fdt, fdt_offset, "pcie-link-width");
	ni_fdt_prop_32(ctx, NI_NUMA, fdt, fdt_offset, "numa-node");

	ni_list(ctx, NI_LIST_PCI_BAR);
	for (bar = 0; bar < 6; bar++) {
		snprintf(nodename, sizeof(nodename), "/drivers/mi/PCI%d,BAR%d", dev_id, bar);
		fdt_offset = fdt_path_offset(fdt, nodename);
		if (fdt_offset >= 0) {
			ni_section(ctx, NI_SEC2_PCI_BAR);

			prop64 = fdt_getprop(fdt, fdt_offset, "mmap_size", &len);
			if (len != sizeof(*prop64))
				continue;
			bar_size = fdt64_to_cpu(*prop64);
			ni_item_int(ctx, NI_BAR_ID, bar);
			if (bar_size)
				sprint_size(nodename, bar_size);
			ni_item_str(ctx, NI_BAR_SIZE_STR, (bar_size == 0 ? "unmapped!" : nodename));
			ni_item_int(ctx, NI_BAR_SIZE, bar_size);

			ni_endsection(ctx, NI_SEC2_PCI_BAR);
		}
	}
	ni_endlist(ctx, NI_LIST_PCI_BAR);
}

void print_common_info(struct nfb_device *dev, int verbose, struct ni_context *ctx)
{
	int i;
	int len;
	int node;
	int fdt_offset;
	int count1, count2;
	int32_t val = 0;
	const void *prop;
	const uint32_t *prop32;
	const void *fdt;

	char buffer[BUFFER_SIZE];
	time_t build_time;

	fdt = nfb_get_fdt(dev);

	ni_section(ctx, NI_SEC0_BOARD_INFO);

	fdt_offset = fdt_path_offset(fdt, "/board/");

	ni_fdt_prop_str(ctx, NI_BOARD_NAME, fdt, fdt_offset, "board-name", &len);

	prop = NULL;
	prop32 = fdt_getprop(fdt, fdt_offset, "serial-number", &len);
	if (len == sizeof(*prop32)) {
		snprintf(buffer, BUFFER_SIZE, "%d", fdt32_to_cpu(*prop32));
		prop = buffer;
	} else {
		prop = fdt_getprop(fdt, fdt_offset, "serial-number-string", &len);
	}
	if (prop)
		ni_item_str(ctx, NI_SERIAL_NUMBER, prop);


	if (verbose > 1)
		ni_fdt_prop_uint64_tx(ctx, NI_FPGA_UNIQUE_ID, fdt, fdt_offset, "fpga-uid");

	i = 0;
	fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
		i++;
	}
	ni_item_int(ctx, NI_NET_IFCS, i);

	if (verbose) {
		ni_list(ctx, NI_LIST_NET_IFCS);
		i = 0;
		fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
			ni_section(ctx, NI_SEC1_NET_IFCS);
			prop = fdt_getprop(fdt, node, "type", &len);
			ni_item_int(ctx, NI_IFC_ID, i);
			ni_item_str(ctx, NI_IFC_TYPE, len > 0 ? (const char *) prop : "Unknown");
			i++;
			ni_endsection(ctx, NI_SEC1_NET_IFCS);
		}
		ni_endlist(ctx, NI_LIST_NET_IFCS);
	}

	if (verbose) {
		nc_adc_sensors_get_temp(dev, &val);
		ni_item_double(ctx, NI_TEMPERATURE, val / 1000.0f);
	}

	ni_endsection(ctx, NI_SEC0_BOARD_INFO);

	ni_section(ctx, NI_SEC0_FIRMWARE);

	fdt_offset = fdt_path_offset(fdt, "/firmware/");

	ni_fdt_prop_str(ctx, NI_CARD_NAME, fdt, fdt_offset, "card-name", &len);
	ni_fdt_prop_str(ctx, NI_PROJECT_NAME, fdt, fdt_offset, "project-name", &len);
	ni_fdt_prop_str(ctx, NI_PROJECT_VARIANT, fdt, fdt_offset, "project-variant", &len);
	ni_fdt_prop_str(ctx, NI_PROJECT_VERSION, fdt, fdt_offset, "project-version", &len);

	prop32 = fdt_getprop(fdt, fdt_offset, "build-time", &len);
	if (len == sizeof(*prop32)) {
		build_time = fdt32_to_cpu(*prop32);
		strftime(buffer, BUFFER_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&build_time));
		ni_item_str(ctx, NI_BUILT_TIME, buffer);
		ni_item_int(ctx, NI_BUILT_TIMESTAMP, build_time);
	}

	ni_fdt_prop_str(ctx, NI_BUILD_TOOL, fdt, fdt_offset, "build-tool", &len);
	ni_fdt_prop_str(ctx, NI_BUILD_AUTHOR, fdt, fdt_offset, "build-author", &len);
	ni_fdt_prop_str(ctx, NI_BUILD_REVISION, fdt, fdt_offset, "build-revision", &len);

	count1 = ndp_get_rx_queue_count(dev);
	count2 = ndp_get_rx_queue_available_count(dev);

	ni_item_int(ctx, NI_RX_QUEUES_ALL, count1);
	ni_item_int(ctx, count1 == count2 ? NI_RX_QUEUES_EQ_AV: NI_RX_QUEUES_NEQ_AV, count2);

	count1 = ndp_get_tx_queue_count(dev);
	count2 = ndp_get_tx_queue_available_count(dev);

	ni_item_int(ctx, NI_TX_QUEUES_ALL, count1);
	ni_item_int(ctx, count1 == count2 ? NI_TX_QUEUES_EQ_AV: NI_TX_QUEUES_NEQ_AV, count2);

	ni_item_int(ctx, NI_ETH_CHANNELS, nc_eth_get_count(dev));

	if (verbose) {
		i = 0;
		ni_list(ctx, NI_LIST_ETH_CHANNELS);
		fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
			ni_section(ctx, NI_SEC1_ETH_CHANNELS);
			fdt_offset = fdt_node_offset_by_phandle_ref(fdt, node, "pcspma");
			prop = fdt_getprop(fdt, fdt_offset, "type", &len);
			ni_item_int(ctx, NI_ETH_CHANNEL_ID, i);
			ni_item_str(ctx, NI_ETH_CHANNEL_TYPE, len > 0 ? (const char *) prop : "Unknown");
			i++;

			ni_endsection(ctx, NI_SEC1_ETH_CHANNELS);
		}
		ni_endlist(ctx, NI_LIST_ETH_CHANNELS);
	}
	ni_endsection(ctx, NI_SEC0_FIRMWARE);

	ni_section(ctx, NI_SEC0_SYSTEM);
	fdt_offset = fdt_path_offset(fdt, "/system/device/");

	ni_list(ctx, NI_LIST_PCIEP);
	fdt_for_each_subnode(node, fdt, fdt_offset) {
		ni_section(ctx, NI_SEC1_PCIEP);
		print_endpoint_info(dev, node, ctx);
		ni_endsection(ctx, NI_SEC1_PCIEP);
	}
	ni_endlist(ctx, NI_LIST_PCIEP);

	ni_endsection(ctx, NI_SEC0_SYSTEM);
}

int main(int argc, char *argv[])
{
	int c;
	int ret = EXIT_SUCCESS;

	const char *path = nfb_default_dev_path();
	struct nfb_device *dev;
	const char *query = NULL;
	char *index;
	int size;
	int js = NI_DRC_USER;

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
		case 'l':
			command = CMD_LIST;
			break;
		case 'v':
			verbose++;
			break;
		case 'q':
			query = optarg;
			break;
		case 'j':
			js = NI_DRC_JSON;
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
	} else if (command == CMD_LIST) {
		struct ni_context *ctx;
		ctx = ni_init_root_context_default(js, ni_items, &ni_common_item_callbacks[js]);
		print_device_list(ctx);
		ni_close_root_context(ctx);
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
		struct ni_context *ctx;
		ctx = ni_init_root_context_default(js, ni_items, &ni_common_item_callbacks[js]);

		ni_section(ctx, NI_SEC_ROOT);
		print_common_info(dev, verbose, ctx);
		ni_endsection(ctx, NI_SEC_ROOT);
		ni_close_root_context(ctx);
	}

	nfb_close(dev);
	return ret;
}
