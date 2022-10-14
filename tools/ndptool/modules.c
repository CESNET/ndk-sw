/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - modules
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include "common.h"

int ndp_mode_generate_init(struct ndp_tool_params *p);
int ndp_mode_receive_init(struct ndp_tool_params *p);
int ndp_mode_transmit_init(struct ndp_tool_params *p);
int ndp_mode_loopback_hw_init(struct ndp_tool_params *p);

void ndp_mode_generate_print_help(void);
void ndp_mode_receive_print_help(void);
void ndp_mode_transmit_print_help(void);
void ndp_mode_loopback_hw_print_help(void);
void ndp_mode_loopback_hw_print_latency(struct stats_info *si);

int ndp_mode_generate_parseopt(struct ndp_tool_params *p, int opt, const char *optarg);
int ndp_mode_receive_parseopt(struct ndp_tool_params *p, int opt, const char *optarg);
int ndp_mode_transmit_parseopt(struct ndp_tool_params *p, int opt, const char *optarg);
int ndp_mode_loopback_hw_parseopt(struct ndp_tool_params *p, int opt, const char *optarg);

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
	[NDP_MODULE_NONE] = {
		.name = NULL,
	},
};
