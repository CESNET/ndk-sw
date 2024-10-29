/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Data transmission tool - PCAP file handling
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <time.h>

#include "common.h"
#include "pcap.h"

FILE *pcap_read_begin(const char *filename)
{
	struct pcap_hdr_s hdr;
	FILE *f = fopen(filename, "rb");
	if (f == NULL)
		return NULL;

	if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
		warn("Could not read PCAP header from '%s'", filename);
		fclose(f);
		return NULL;
	}

	return f;
}

FILE *pcap_write_begin(const char *filename)
{
	FILE *f;
	struct pcap_hdr_s hdr = {
		.magic_number   = 0xa1b23c4d,
		.version_major  = 2,
		.version_minor  = 4,
		.thiszone       = 0,
		.sigfigs        = 0,
		.snaplen        = 65535,
		.network        = 1 /* LINKTYPE_ETHERNET */
	};

	f = fopen(filename, "wb");
	if (f == NULL) {
		return NULL;
	}

	if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
		warnx("Could not write PCAP header to '%s'", filename);
		return NULL;
	}

	return f;
}

static inline uint32_t min(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

int pcap_write_packet(struct ndp_packet *pkt, FILE *file, int ts_mode, unsigned trim)
{
	struct timespec ts;
	struct pcaprec_hdr_s hdr;

	if (ts_mode == TS_MODE_SYSTEM) {
		clock_gettime(CLOCK_REALTIME, &ts);
		hdr.ts_sec = ts.tv_sec;
		hdr.ts_nsec = ts.tv_nsec;
	} else if (ts_mode >= 0) {
		if ((unsigned) ts_mode + 64 > pkt->header_length * 8) {
			warnx("Packet header is too short (%d bits) for specified timestamp "
					"value offset (bits %d-%d)", pkt->header_length * 8, ts_mode, ts_mode + 63);
			hdr.ts_sec = 0;
			hdr.ts_nsec = 0;
		} else {
			hdr.ts_sec = (*((uint64_t*)(pkt->header + ts_mode / 8 + 4))) >> (ts_mode % 8);
			hdr.ts_nsec = (*((uint64_t*)(pkt->header + ts_mode / 8 + 0))) >> (ts_mode % 8);
		}
	} else {
		hdr.ts_sec = 0;
		hdr.ts_nsec = 0;
	}

	hdr.orig_len = pkt->data_length;
	hdr.incl_len = min(pkt->data_length, trim);

	if (fwrite(&hdr, sizeof(hdr), 1, file) != 1) {
		warn("Writing PCAP packet header failed");
		return -1;
	}

	if (fwrite(pkt->data, hdr.incl_len, 1, file) != 1) {
		warn("Writing PCAP packet data failed");
		return -1;
	}

	return 0;
}

int pcap_write_packet_burst(struct ndp_packet *pkts, unsigned pkt_count, FILE *file, int ts_mode, unsigned trim)
{
	int ret;

	for (unsigned i = 0; i < pkt_count; i++) {
		ret = pcap_write_packet(&(pkts[i]), file, ts_mode, trim);
		if (ret)
			return ret;
	}

	return 0;
}
