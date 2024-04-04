/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ethernet interface configuration tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <unistd.h>
#include <stdio.h>

#include <nfb/nfb.h>
#include <libfdt.h>

#include <netcope/idcomp.h>
#include <netcope/nccommon.h>

#include "eth.h"

#define ARGUMENTS	":hd:i:q:e:rtRSl:L:p:m:u:a:c:M:ovPT"

#define RXMAC       1
#define TXMAC       2
#define PCSPMA      4
#define TRANSCEIVER 8

// TODO add usage for queries
void usage(const char *progname, int verbose)
{
	printf("Usage: %s  [-rtPTvhRS] [-d path] [-i index] [-e 1|0] [-p repeater_cfg]\n"
		   "                [-l min_length] [-L max_length] [-m err_mask]\n"
		   "                [-M mac_cmd] [opt_param]\n", progname);

	printf("Only one command may be used at a time.\n");
	printf("-d path         Path to device [default: %s]\n", NFB_DEFAULT_DEV_PATH);
	printf("-i indexes      Interfaces numbers to use - list or range, e.g. \"0-5,7\" [default: all]\n");
	printf("-r              Use RXMAC [default]\n");
	printf("-t              Use TXMAC [default]\n");
	printf("-P              Use PCS/PMA\n");
	printf("-T              Use transceiver\n");
	printf("-e 1|0          Enable [1] / disable [0] interface\n");
	printf("-R              Reset frame counters\n");
	printf("-S              Show etherStats counters\n");
	printf("-l length       Minimal allowed frame length\n");
	printf("-L length       Maximal allowed frame length\n");
/*  printf("-m mask         Set error mask 0-31\n"); */
	printf("-c type         Set PMA type/mode by name or enable/disable feature (+feat/-feat)\n");
	printf("-p repeater_cfg Set transmit data source%s\n", verbose ? "" : " (-hv for more info)");
	if (verbose) {
		printf(" * normal       Transmit data from application\n");
		printf(" * repeat       Transmit data from RXMAC\n");
		printf(" * idle         Transmit disabled\n");
	}
	printf("-M command      MAC filter settings (RXMAC only)%s\n", verbose ? "" : " (-hv for more info)");
	if (verbose) {
		printf(" * add          Add MAC address specified in [opt_param] to table\n");
		printf(" * remove       Remove MAC address specified in [opt_param] from table\n");
		printf(" * show         Show content of MAC address table\n");
		printf(" * clear        Clear content of MAC address table\n");
		printf(" * fill         Fill MAC address table with values from stdin\n");
		printf(" * promiscuous  Pass all traffic\n");
		printf(" * normal       Pass only MAC addresses present in table\n");
		printf(" * broadcast    Pass MAC addresses present in table and broadcast traffic\n");
		printf(" * multicast    Pass MAC addresses present in table, broadcast and multicast traffic\n");
	}
	printf("-q query        Get specific informations%s\n", verbose ? "" : " (-v for more info)");
	if (verbose) {
        	printf(" * rx_status\n");
        	printf(" * rx_octets\n");
        	printf(" * rx_processed\n");
        	printf(" * rx_erroneous\n");
        	printf(" * rx_link\n");
        	printf(" * rx_received\n");
        	printf(" * rx_overflowed\n");
        	printf(" * tx_status\n");
        	printf(" * tx_octets\n");
        	printf(" * tx_processed\n");
        	printf(" * tx_erroneous\n");
        	printf(" * tx_transmitted\n");
        	printf(" * pma_type\n");
        	printf(" * pma_speed\n");
		printf(" example of usage: '-q rx_link,tx_octets,pma_speed'\n");
	}

	printf("-v              Increase verbosity\n");
	printf("-h              Show this text\n");
	printf("Examples:\n");
	printf("%s -Pv\n"
	       "                Print all supported PMA types/modes and features.\n", progname);
	printf("%s -Pc \"+PMA local loopback\"\n"
	       "                Enable local loopback on all PMAs.\n", progname);
}

int main(int argc, char *argv[])
{
	char *file = NFB_DEFAULT_DEV_PATH;
	struct nfb_device *dev;
	const char *query = NULL;
	char *queries_index = NULL;
	int size = 0;
	const void *fdt;

	int i, c;
	int ret;
	char cmds = 0;
	int used = 0;

	unsigned mv[6];
	int use = 0;
	int node;
	int fdt_offset;
	struct nc_rxmac *rxmac;
	struct nc_txmac *txmac;
	struct eth_params p, p2;
	struct list_range index_range;

	enum nc_idcomp_repeater repeater_status = IDCOMP_REPEATER_NORMAL;
	const char *repeater_str;

	p.command = CMD_PRINT_STATUS;
	p.verbose = 0;
	p.ether_stats = false;
	p.index = 0;

	list_range_init(&index_range);

	opterr = 0;
	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch(c) {
		/* Common parameters */
		case 'v':
			p.verbose++;
			break;
		case 'h':
			p.command = CMD_USAGE;
			break;
		case 'd':
			file = optarg;
			break;
		case 'q':
			p.command = CMD_QUERY;
			query = optarg;
			break;
		case 'i':
			if (list_range_parse(&index_range, optarg) < 0)
				errx(EXIT_FAILURE, "Cannot parse interface number.");
			break;

		/* Modules */
		case 'r':
			use |= RXMAC;
			break;
		case 't':
			use |= TXMAC;
			break;
		case 'P':
			use |= PCSPMA;
			break;
		case 'T':
			use |= TRANSCEIVER;
			break;

		/* Commands */
		case 'e':
			if (nc_strtol(optarg, &p.param) ||
					(p.param != 0 && p.param != 1))
				errx(EXIT_FAILURE, "Wrong enable value [0|1].");
			p.command = CMD_ENABLE;
			cmds++;
			break;
		case 'R':
			p.command = CMD_RESET;
			cmds++;
			break;

		case 'S':
			p.ether_stats = true;
			break;

		case 'l':
			if (nc_strtol(optarg, &p.param) || p.param <= 0) {
				errx(EXIT_FAILURE, "Wrong minimal frame length.");
			}
			p.command = CMD_SET_MIN_LENGTH;
			cmds++;
			break;
		case 'L':
			if (nc_strtol(optarg, &p.param) ||
					p.param <= 0) {
				errx(EXIT_FAILURE, "Wrong maximal frame length.");
			}
			p.command = CMD_SET_MAX_LENGTH;
			cmds++;
			break;
		case 'm':
			if (nc_strtol(optarg, &p.param) ||
					p.param < 0 || p.param > 31) {
				errx(EXIT_FAILURE, "Wrong error mask.");
			}
			p.command = CMD_SET_MASK;
			cmds++;
			break;

		case 'M':
			switch (tolower(optarg[0])) {
			case 's': p.command = CMD_SHOW_MACS; break;
			case 'f': p.command = CMD_FILL_MACS; break;
			case 'c': p.command = CMD_CLEAR_MACS; break;
			case 'a': p.command = CMD_ADD_MAC; break;
			case 'r': p.command = CMD_REMOVE_MAC; break;
			case 'p': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_PROMISCUOUS; break;
			case 'n': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_TABLE; break;
			case 'b': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_TABLE_BCAST; break;
			case 'm': p.command = CMD_MAC_CHECK_MODE; p.param = RXMAC_MAC_FILTER_TABLE_BCAST_MCAST; break;
			default:
				errx(EXIT_FAILURE, "Wrong MAC filter settings.");
			break;
			}
			cmds++;
			break;
		case 'c':
			if (optarg[0] == '+' || optarg[0] == '-') {
				p.command = CMD_SET_PMA_FEATURE;
				p.string = optarg + 1;
				p.param = optarg[0] == '+' ? 1 : 0;
			} else {
				p.command = CMD_SET_PMA_TYPE;
				p.string = optarg;
			}
			break;
		case 'p':
			switch (tolower(optarg[0])) {
			case 'n': repeater_status = IDCOMP_REPEATER_NORMAL; break;
			case 'r': repeater_status = IDCOMP_REPEATER_REPEAT; break;
			case 'i': repeater_status = IDCOMP_REPEATER_IDLE; break;
			default:
				errx(EXIT_FAILURE, "Wrong repeater settings.");
			}
			p.command = CMD_SET_REPEATER;
			cmds++;
			break;
		case '?':
			errx(EXIT_FAILURE, "Unknown argument '%c'", optopt);
		case ':':
			errx(EXIT_FAILURE, "Missing parameter for argument '%c'", optopt);
		default:
			errx(EXIT_FAILURE, "Unknown error");
		}
	}

	if (p.command == CMD_USAGE) {
		usage(argv[0], p.verbose);
		return EXIT_SUCCESS;
	}

	argc -= optind;
	argv += optind;

	if (p.command == CMD_ADD_MAC || p.command == CMD_REMOVE_MAC) {
		if (argc < 1) {
			errx(EXIT_FAILURE, "Missing MAC address for for argument 'M'");
		}
		if (6 == sscanf(argv[0], "%x:%x:%x:%x:%x:%x%*c", mv+0, mv+1, mv+2, mv+3, mv+4, mv+5)) {
			p.mac_address = 0;
			for (i = 0; i < 6; i++) {
				p.mac_address <<= 8;
				p.mac_address |= mv[i] & 0xFF;
			}
		} else {
			errx(EXIT_FAILURE, "Can't parse MAC address, excepted in format AA:BB:CC:DD:EE:FF");
		}
		argc--;
		argv++;
	}

	if (argc > 0) {
		errx(EXIT_FAILURE, "Stray arguments");
	}

	if (cmds > 1) {
		errx(EXIT_FAILURE, "More than one operation requested. Please select just one.");
	}

	dev = nfb_open(file);
	if (!dev) {
		err(EXIT_FAILURE, "nfb_open failed");
	}

	fdt = nfb_get_fdt(dev);

	if (p.command == CMD_QUERY) {
		size = nc_query_parse(query, queries, NC_ARRAY_SIZE(queries), &queries_index);
		if (size <= 0) {
			nfb_close(dev);
			return -1;
		}
	} else {
		if (use == 0)
			use = RXMAC | TXMAC;
		else if (use & TRANSCEIVER)
			use &= TRANSCEIVER;
	}

	fdt_for_each_compatible_node(fdt, node, COMP_NETCOPE_ETH) {
		if (use & TRANSCEIVER)
			continue;

		if (list_range_empty(&index_range) || list_range_contains(&index_range, p.index)) {
			if (p.command == CMD_SET_REPEATER) {
				used++;
				if (nc_idcomp_repeater_set(dev, p.index, repeater_status)) {
					errx(EXIT_FAILURE, "Can't set repeater mode. Use loopback features in PCS/PMA section");
				}
			} else {
				if (p.command == CMD_PRINT_STATUS) {
					if (used++)
						printf("\n");
					printf("----------------------------- Ethernet interface %d ----\n", p.index);
					p2 = p;
					p2.command = CMD_PRINT_SPEED;
					pcspma_execute_operation(dev, node, &p2);
					transceiver_print_short_info(dev, node, &p);
				}

				if (p.command == CMD_QUERY) {
					used++;
					ret = query_print(fdt, node, queries_index, size, dev, p.index);
					if (ret) {
						free(queries_index);
						list_range_destroy(&index_range);
						nfb_close(dev);
						return EXIT_FAILURE;
					}
				}

				if (use & RXMAC) {
					used++;
					fdt_offset = nc_eth_get_rxmac_node(fdt, node);
					rxmac = nc_rxmac_open(dev, fdt_offset);
					if (!rxmac) {
						warnx("Cannot open RXMAC for ETH%d", p.index);
					} else {
						if (rxmac_execute_operation(rxmac, &p) != 0)
							warnx("Cannot perform a command on RXMAC%d", p.index);
						nc_rxmac_close(rxmac);
					}
				}

				if (use & TXMAC) {
					used++;
					fdt_offset = nc_eth_get_txmac_node(fdt, node);
					txmac = nc_txmac_open(dev, fdt_offset);
					if (!txmac) {
						warnx("Cannot open TXMAC for ETH%d", p.index);
					} else {
						if (txmac_execute_operation(txmac, &p) != 0)
							warnx("Cannot perform a command on TXMAC%d", p.index);
						nc_txmac_close(txmac);
					}
				}

				if (p.command == CMD_PRINT_STATUS && (use & RXMAC || use & TXMAC)) {
					used++;
					repeater_status = nc_idcomp_repeater_get(dev, p.index);
					switch (repeater_status) {
					case IDCOMP_REPEATER_NORMAL: repeater_str = "Normal  (transmit data from application)"; break;
					case IDCOMP_REPEATER_REPEAT: repeater_str = "Repeat  (transmit data from RXMAC)"; break;
					case IDCOMP_REPEATER_IDLE:   repeater_str = "Idle    (transmit disabled)"; break;
					default:                     repeater_str = "Unknown (use the PCS/PMA features)"; break;
					}
					printf("Repeater status            : %s\n", repeater_str);
				}

				if (p.command == CMD_PRINT_STATUS) {
					used++;
					if (use & PCSPMA)
						pcspma_execute_operation(dev, node, &p);
				} else if (use & PCSPMA) {
					used++;
					ret = pcspma_execute_operation(dev, node, &p);
					if (ret)
						warnx("PCS/PMA command failed");
				}
			}

		}
		p.index++;
	}

	if (use & TRANSCEIVER) {
		fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
			if (list_range_empty(&index_range) || list_range_contains(&index_range, p.index)) {
				if (p.command == CMD_PRINT_STATUS) {
					if (used++)
						printf("\n");
					transceiver_print(dev, node, p.index);
				} else {
					used++;
					ret = transceiver_execute_operation(dev, node, &p);
					if (ret)
						warnx("Transceiver command failed");
				}
			}
			p.index++;
		}
	}

	if (!used) {
		warnx("No such interface");
	}

	if (query)
		free(queries_index);
	list_range_destroy(&index_range);

	nfb_close(dev);
	return EXIT_SUCCESS;
}
