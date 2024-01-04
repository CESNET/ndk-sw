/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - common code header
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <wordexp.h>

#include <nfb/ndp.h>
#include <netcope/nccommon.h>

enum progress_type {
	PT_NONE,
	PT_LETTER,
	PT_HEADER,
	PT_DATA,
	PT_ALL
};

enum thread_state {
	TS_NONE,
	TS_INIT,
	TS_RUNNING,
	TS_FINISHED,
};

typedef enum fastwrite_mode {
	FWMODE_NONE = 0,

	/* most packets will have specified length, some may differ */
	FWMODE_VARIABLE,

	/* all packets will have the same length
	 * (possibly different from the specified length) */
	FWMODE_APPROXIMATE,
} fwmode_t;

struct stats_info {
	unsigned long long packet_cnt;
	unsigned long long bytes_cnt;

	unsigned long long progress_counter;
	unsigned long long sampling;
	char progress_letter;
	enum progress_type progress_type;

	unsigned long long thread_packet_cnt;
	unsigned long long thread_bytes_cnt;
	unsigned long long thread_total_bytes_cnt;
	void *priv;
	bool incremental;

	struct timeval startTime;
	struct timeval endTime;

	double latency_sum;
};

struct ndp_mode_generate_params {
	struct list_range range;
	int srand;
	bool clear_data;
	fwmode_t mode;
};

/*!
 * \brief Parameters specific for transmit module
 */
struct ndp_mode_transmit_params {
	unsigned long      loops;          /*!< How many time to loop over the PCAP (0 = forever) */
	bool               do_cache;       /*!< Controls whether to pre-load PCAP file into RAM cache */
	unsigned long long mbps;           /*!< Replay packets at a given Mbps */
	bool               multiple_pcaps; /*!< Controls whether PCAP file is specified for each thread with '%d' as thread_id*/
	unsigned long      min_len;        /*!< Minimal allowed frame length that can be transferred. */
};

/*!
 * \brief Parameters specific for receive module
 */
struct ndp_mode_receive_params {
	int ts_mode;                /*!< Timestamp store mode, see TS_MODE_* in pcap.h for possible values */
	unsigned int trim;          /*!< Packet trim mode. Maximum size of the saved packet. */
};

// Purposely not a power of 2 but a prime number to avoid
// repeating of the same descriptors at the same position in buffers
#define PREGEN_SEQ_SIZE 5503

struct ndp_mode_loopback_hw_params {
	struct list_range range;
	int32_t srand;
	uint32_t pregen_ptr;
	uint16_t pregen_sizes[PREGEN_SEQ_SIZE * 2];
	uint32_t pregen_ids  [PREGEN_SEQ_SIZE * 2];
};

struct ndp_mode_dpdk_queue_data {
	unsigned queue_id;
	unsigned port_id;
	struct rte_mempool *pool;
};

struct ndp_mode_dpdk_params {
	// queue indexes as user passed then into the programm
	struct list_range queue_range;
	// number of aviable queues
	unsigned queues_available;
	// number of initialized queues
	unsigned queue_count;
	struct ndp_mode_dpdk_queue_data *queue_data_arr;
	wordexp_t args;

	// generate
	struct list_range range;
	int srand;
	
	// receive
	int ts_mode;                /*!< Timestamp store mode, see TS_MODE_* in pcap.h for possible values */
	unsigned int trim;          /*!< Packet trim mode. Maximum size of the saved packet. */

	// transmit
	unsigned long      loops;          /*!< How many time to loop over the PCAP (0 = forever) */
	bool               do_cache;       /*!< Controls whether to pre-load PCAP file into RAM cache */
	unsigned long long mbps;           /*!< Replay packets at a given Mbps */
	bool               multiple_pcaps; /*!< Controls whether PCAP file is specified for each thread with '%d' as thread_id*/
	unsigned long      min_len;        /*!< Minimal allowed frame length that can be transferred. */
};

typedef void (*update_stats_t)(struct ndp_packet *packets, int count, struct stats_info *si);

struct ndp_tool_params {
	struct nfb_device *dev;
	struct ndp_queue *rx;
	struct ndp_queue *tx;

	const char *nfb_path;
	int queue_index;
	update_stats_t update_stats;

	struct stats_info si;
	union {
		struct ndp_mode_generate_params generate;
		struct ndp_mode_transmit_params transmit;
		struct ndp_mode_receive_params receive;
		struct ndp_mode_loopback_hw_params loopback_hw;
		struct ndp_mode_dpdk_params dpdk;
	} mode;

	const char *pcap_filename;
	FILE *pcap_file;

	long long unsigned limit_packets;
	long long unsigned limit_bytes;

	int verbose;
	unsigned use_delay_nsec: 1;
	unsigned use_userspace_flag: 1;
};

struct thread_data {
	int thread_id;
	pthread_spinlock_t lock;
	struct ndp_tool_params params;
	int ret;
	enum thread_state state;
};

struct ndptool_module {
	const char *name;
	const char *short_help;
	const char *args;
	uint8_t flags;
	void (*print_help)(void);
	int (*init)(struct ndp_tool_params *p);
	int (*check)(struct ndp_tool_params *p);
	int (*parse_opt)(struct ndp_tool_params *p, int opt, char *optarg);
	int (*run_single)(struct ndp_tool_params *p);
	void *(*run_thread)(void *tmp);
	void (*destroy)(struct ndp_tool_params *p);
	void (*stats_cb)(struct stats_info *si);
};

extern volatile int stop;
extern volatile int stats;
extern struct ndptool_module *module;

extern unsigned RX_BURST;
extern unsigned TX_BURST;

void delay_usecs(unsigned int us);
void delay_nsecs(unsigned int ns);

void update_stats(struct ndp_packet *packets, int count, struct stats_info *si);
void update_stats_thread(struct ndp_packet *packets, int count, struct stats_info *si);

void update_stats_loop_thread(int interval, struct thread_data **pdata, int thread_cnt, struct list_range *qr, struct stats_info *si);

void gather_stats_info(struct stats_info *si, struct stats_info *thread);
void print_stats(struct stats_info *si);

enum ndp_modules {
	NDP_MODULE_READ,
	NDP_MODULE_GENERATE,
	NDP_MODULE_RECEIVE,
	NDP_MODULE_TRANSMIT,
	NDP_MODULE_LOOPBACK,
	NDP_MODULE_LOOPBACK_HW,
	NDP_MODULE_DPDK_GENERATE,
	NDP_MODULE_DPDK_READ,
	NDP_MODULE_DPDK_LOOPBACK,
	NDP_MODULE_DPDK_RECEIVE,
	NDP_MODULE_DPDK_TRANSMIT,
	/* NONE module must be last! */
	NDP_MODULE_NONE,
};

#define HEX_DUMP_BUF_SIZE 1024*32
static inline void hexdump(FILE *stream, const char *description, const void *data_addr, const unsigned int data_bytes)
{
	// <space> + <offset>
	const unsigned int line_chars = 1    // 1x space
	                              + 4    // hexa offset
	                              + 2    // 2x space
	                              + 3*16 // 16x (byte value and space)
	                              + 1    // 1x space
	                              + 16   // 16x byte ASCII
	                              + 1;   // EOL

	unsigned int i = 0, e;
	unsigned char buff[HEX_DUMP_BUF_SIZE];
	unsigned char *rdptr        = (unsigned char*)data_addr;
	unsigned char *offset_wrptr = (unsigned char*)buff;
	unsigned char *value_wrptr  = (unsigned char*)buff;
	unsigned char *ascii_wrptr  = (unsigned char*)buff;

	unsigned int rd_index = 0;

	unsigned int lines;
	unsigned int required_buf_size;

	// Copy description and add EOL
	if (description != NULL) {
		for (i = 0; i < HEX_DUMP_BUF_SIZE; i++) {
			if (description[i] == '\0') {
				*offset_wrptr = '\n';
				offset_wrptr++;
				i++;
				break;
			}
			*offset_wrptr = description[i];
			offset_wrptr++;
		}
	}

	lines = (data_bytes + 15) / 16;
	// Description + dump + \0
	required_buf_size = i + lines * line_chars + 1;
	if (required_buf_size > HEX_DUMP_BUF_SIZE) {
		fprintf(stderr,"%s: WARNING: Buffer size is %u; Required size to dump data is %u (description: %u + data: %u + 1)\n",__func__,HEX_DUMP_BUF_SIZE,required_buf_size,i,lines*line_chars);
		return;
	}

	// Init write pointers
	offset_wrptr = offset_wrptr;
	value_wrptr  = offset_wrptr + 1 + 4 + 2;
	ascii_wrptr  = value_wrptr  + 3*16 + 1;

	// For each line of data dump
	for (i = 0; i < lines; i++) {

		// Print address offset
		sprintf((char*)offset_wrptr," %04x  ",i*16);

		for (e = 0; e < 16; e++) {

			// If out of data, fill with spaces
			if (rd_index == data_bytes) {
				memset((void*)value_wrptr,' ',4);
				value_wrptr += 3;
				continue;
			}

			// Print data value
			sprintf((char*)value_wrptr,"%02x ",*rdptr);
			value_wrptr += 3;
			*value_wrptr = ' '; // rewrite the '\0' added by the sprintf

			// Print data value
			if (isprint(*rdptr) && !isspace(*rdptr))
				*ascii_wrptr = *rdptr;
			else
				*ascii_wrptr = '.';
			ascii_wrptr++;

			rdptr++;
			rd_index++;
		}

		// Add EOL
		*ascii_wrptr = '\n';
		ascii_wrptr++;

		// Shift pointers to next line
		offset_wrptr = ascii_wrptr;
		value_wrptr  = offset_wrptr + 1 + 4 + 2;
		ascii_wrptr  = value_wrptr  + 3*16 + 1;
	}

	// Add \0
	*ascii_wrptr = '\0';

	// Print to Output
	fprintf(stream,"%s",buff);
}
