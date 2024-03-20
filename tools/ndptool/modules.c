/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - modules
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include "common.h"

#ifdef USE_DPDK
int dpdk_generate_init(struct ndp_tool_params *p);
int dpdk_generate_check(struct ndp_tool_params *p);
int dpdk_generate_parseopt(struct ndp_tool_params *p, int opt, char *optarg);
int dpdk_generate_run_single(struct ndp_tool_params *p);
void dpdk_generate_destroy(struct ndp_tool_params *p);
void *dpdk_generate_run_thread(void *tmp);
void dpdk_generate_print_help();

int dpdk_read_init(struct ndp_tool_params *p);
int dpdk_read_check(struct ndp_tool_params *p);
int dpdk_read_parseopt(struct ndp_tool_params *p, int opt, char *optarg);
int dpdk_read_run_single(struct ndp_tool_params *p);
void dpdk_read_destroy(struct ndp_tool_params *p);
void *dpdk_read_run_thread(void *tmp);
void dpdk_read_print_help();

int dpdk_loopback_init(struct ndp_tool_params *p);
int dpdk_loopback_check(struct ndp_tool_params *p);
int dpdk_loopback_parseopt(struct ndp_tool_params *p, int opt, char *optarg);
int dpdk_loopback_run_single(struct ndp_tool_params *p);
void dpdk_loopback_destroy(struct ndp_tool_params *p);
void *dpdk_loopback_run_thread(void *tmp);
void dpdk_loopback_print_help();

int dpdk_receive_init(struct ndp_tool_params *p);
int dpdk_receive_check(struct ndp_tool_params *p);
int dpdk_receive_parseopt(struct ndp_tool_params *p, int opt, char *optarg);
int dpdk_receive_run_single(struct ndp_tool_params *p);
void dpdk_receive_destroy(struct ndp_tool_params *p);
void *dpdk_receive_run_thread(void *tmp);
void dpdk_receive_print_help();

int dpdk_transmit_init(struct ndp_tool_params *p);
int dpdk_transmit_check(struct ndp_tool_params *p);
int dpdk_transmit_parseopt(struct ndp_tool_params *p, int opt, char *optarg);
int dpdk_transmit_run_single(struct ndp_tool_params *p);
void dpdk_transmit_destroy(struct ndp_tool_params *p);
void *dpdk_transmit_run_thread(void *tmp);
void dpdk_transmit_print_help();
#endif // USE_DPDK

int ndp_mode_generate_init(struct ndp_tool_params *p);
int ndp_mode_receive_init(struct ndp_tool_params *p);
int ndp_mode_transmit_init(struct ndp_tool_params *p);
int ndp_mode_loopback_hw_init(struct ndp_tool_params *p);

void ndp_mode_generate_print_help(void);
void ndp_mode_receive_print_help(void);
void ndp_mode_transmit_print_help(void);
void ndp_mode_loopback_hw_print_help(void);
void ndp_mode_loopback_hw_print_latency(struct stats_info *si);

int ndp_mode_generate_parseopt(struct ndp_tool_params *p, int opt, char *optarg, int option_index);
int ndp_mode_receive_parseopt(struct ndp_tool_params *p, int opt, char *optarg, int option_index);
int ndp_mode_transmit_parseopt(struct ndp_tool_params *p, int opt, char *optarg, int option_index);
int ndp_mode_loopback_hw_parseopt(struct ndp_tool_params *p, int opt, char *optarg, int option_index);

int ndp_mode_generate_check(struct ndp_tool_params *p);
int ndp_mode_receive_check(struct ndp_tool_params *p);
int ndp_mode_transmit_check(struct ndp_tool_params *p);
int ndp_mode_loopback_hw_check(struct ndp_tool_params *p);

int ndp_mode_read(struct ndp_tool_params *p);
int ndp_mode_write(struct ndp_tool_params *p);
int ndp_mode_receive(struct ndp_tool_params *p);
int ndp_mode_transmit(struct ndp_tool_params *p);
int ndp_mode_generate(struct ndp_tool_params *p);
int ndp_mode_loopback(struct ndp_tool_params *p);
int ndp_mode_loopback_hw(struct ndp_tool_params *p);

void *ndp_mode_read_thread(void *tmp);
void *ndp_mode_generate_thread(void *tmp);
void *ndp_mode_loopback_thread(void *tmp);
void *ndp_mode_receive_thread(void *tmp);
void *ndp_mode_transmit_thread(void *tmp);
void *ndp_mode_loopback_hw_thread(void *tmp);

void ndp_mode_generate_destroy(struct ndp_tool_params *p);
void ndp_mode_loopback_hw_destroy(struct ndp_tool_params *p);

struct ndptool_module modules[] = {
	[NDP_MODULE_READ] = {
		.name = "read",
		.short_help = "Read packets",
		.args = "",
		.run_single = ndp_mode_read,
		.run_thread = ndp_mode_read_thread,
	},
	[NDP_MODULE_GENERATE] = {
		.name = "generate",
		.short_help = "Generate packets",
		.print_help = ndp_mode_generate_print_help,
		.init = ndp_mode_generate_init,
		.args = "s:C",
		.parse_opt = ndp_mode_generate_parseopt,
		.check = ndp_mode_generate_check,
		.run_single = ndp_mode_generate,
		.run_thread = ndp_mode_generate_thread,
		.destroy = ndp_mode_generate_destroy,
	},
	[NDP_MODULE_RECEIVE] = {
		.name = "receive",
		.short_help = "Receive packets to file",
		.print_help = ndp_mode_receive_print_help,
		.init = ndp_mode_receive_init,
		.args = "f:t:r:",
		.parse_opt = ndp_mode_receive_parseopt,
		.check = ndp_mode_receive_check,
		.run_single = ndp_mode_receive,
		.run_thread = ndp_mode_receive_thread,
	},
	[NDP_MODULE_TRANSMIT] = {
		.name = "transmit",
		.short_help = "Transmit packets from file",
		.print_help = ndp_mode_transmit_print_help,
		.init = ndp_mode_transmit_init,
		.args = "f:l:s:L:Zm",
		.parse_opt = ndp_mode_transmit_parseopt,
		.check = ndp_mode_transmit_check,
		.run_single = ndp_mode_transmit,
		.run_thread = ndp_mode_transmit_thread,
	},
	[NDP_MODULE_LOOPBACK] = {
		.name = "loopback",
		.short_help = "Transmit received packets",
		.args = "",
		.run_single = ndp_mode_loopback,
		.run_thread = ndp_mode_loopback_thread,
	},
	[NDP_MODULE_LOOPBACK_HW] = {
		.name = "loopback-hw",
		.short_help = "Transmit packets and receive them back",
		.print_help = ndp_mode_loopback_hw_print_help,
		.init = ndp_mode_loopback_hw_init,
		.args = "s:l",
		.parse_opt = ndp_mode_loopback_hw_parseopt,
		.check = ndp_mode_loopback_hw_check,
		.run_single = ndp_mode_loopback_hw,
		.run_thread = ndp_mode_loopback_hw_thread,
		.destroy = ndp_mode_loopback_hw_destroy,
		.flags = 0,
	},
#ifdef USE_DPDK
	[NDP_MODULE_DPDK_GENERATE] = {
		.name = "dpdk-generate",
		.short_help = "dpdk version of generate app",
		.print_help = dpdk_generate_print_help,
		.init = dpdk_generate_init,
		.args = "a:ns:C",
		.parse_opt = dpdk_generate_parseopt,
		.check = dpdk_generate_check,
		.run_single = dpdk_generate_run_single,
		.run_thread = dpdk_generate_run_thread,
		.destroy = dpdk_generate_destroy,
	},
	[NDP_MODULE_DPDK_READ] = {
		.name = "dpdk-read",
		.short_help = "dpdk version of read app",
		.print_help = dpdk_read_print_help,
		.init = dpdk_read_init,
		.args = "a:nx",
		.parse_opt = dpdk_read_parseopt,
		.check = dpdk_read_check,
		.run_single = dpdk_read_run_single,
		.run_thread = dpdk_read_run_thread,
		.destroy = dpdk_read_destroy,
	},
	[NDP_MODULE_DPDK_LOOPBACK] = {
		.name = "dpdk-loopback",
		.short_help = "dpdk version of loopback app",
		.print_help = dpdk_loopback_print_help,
		.init = dpdk_loopback_init,
		.args = "a:nx",
		.parse_opt = dpdk_loopback_parseopt,
		.check = dpdk_loopback_check,
		.run_single = dpdk_loopback_run_single,
		.run_thread = dpdk_loopback_run_thread,
		.destroy = dpdk_loopback_destroy,
	},
	[NDP_MODULE_DPDK_RECEIVE] = {
		.name = "dpdk-receive",
		.short_help = "dpdk version of receive app",
		.print_help = dpdk_receive_print_help,
		.init = dpdk_receive_init,
		.args = "a:nxf:t:r:",
		.parse_opt = dpdk_receive_parseopt,
		.check = dpdk_receive_check,
		.run_single = dpdk_receive_run_single,
		.run_thread = dpdk_receive_run_thread,
		.destroy = dpdk_receive_destroy,
	},
	[NDP_MODULE_DPDK_TRANSMIT] = {
		.name = "dpdk-transmit",
		.short_help = "dpdk version of transmit app",
		.print_help = dpdk_transmit_print_help,
		.init = dpdk_transmit_init,
		.args = "a:nf:l:s:L:Zm",
		.parse_opt = dpdk_transmit_parseopt,
		.check = dpdk_transmit_check,
		.run_single = dpdk_transmit_run_single,
		.run_thread = dpdk_transmit_run_thread,
		.destroy = dpdk_transmit_destroy,
	},
#endif // USE_DPDK
	[NDP_MODULE_NONE] = {
		.name = NULL,
	},
};
