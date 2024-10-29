/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Data transmission tool - PCAP file handling header
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NDPTOOL_PCAP_H
#define NDPTOOL_PCAP_H

#include <inttypes.h>
#include <stdio.h>

struct pcap_hdr_s {
	uint32_t magic_number;  /* magic number */
	uint16_t version_major; /* major version number */
	uint16_t version_minor; /* minor version number */
	int32_t thiszone;       /* GMT to local correction */
	uint32_t sigfigs;       /* accuracy of timestamps */
	uint32_t snaplen;       /* max length of captured packets, in octets */
	uint32_t network;       /* data link type */
} __attribute__ ((packed));

struct pcaprec_hdr_s {
	uint32_t ts_sec;        /* timestamp seconds */
	uint32_t ts_nsec;       /* timestamp microseconds */
	uint32_t incl_len;      /* number of octets of packet saved in file */
	uint32_t orig_len;      /* actual length of packet */
} __attribute__ ((packed));

/* Timestamp store modes */
#define TS_MODE_NONE (-1)       /* Do not store timestamp */
#define TS_MODE_SYSTEM (-2)     /* Store timestamp obtained from system */
#define TS_MODE_HEADER (0)      /* Store timestamp obtained from packet header
                                   This value is zero bit offset of timestamp in NDP header */

FILE *pcap_read_begin(const char *filename);
FILE *pcap_write_begin(const char *filename);
int pcap_write_packet(struct ndp_packet *pkt, FILE *pcapfile, int ts_mode, unsigned trim);
int pcap_write_packet_burst(struct ndp_packet *burst, unsigned burst_size, FILE *pcapfile, int ts_mode, unsigned trim);

#endif /* NDPTOOL_PCAP_H */
