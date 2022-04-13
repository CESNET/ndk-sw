/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data transmission tool - structured packet manipulations
 *
 * Copyright (C) 2021-2022 CESNET
 * Author(s):
 *   Jan Kubalek <kubalek@cesnet.cz>
 */

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <wchar.h>

#define get_size_hash(x) ((uint8_t)(((uint16_t)x >> 8) + ((uint16_t)x)))
#define get_size32(x) (((uint16_t)x + 3) / 4)

#define LATENCY_FLAG 1

// Structured Packet can be used to generate packet data with a specific
// structure, which can later be checked for its correctness independently
typedef struct {
	uint16_t queue_id; // ID of the packet's original queue

	uint16_t size; // Size of the packet in bytes
	uint16_t size32; // Size of the packet in 4-byte blocks

	uint8_t  size_hash; // Hash calculated from Size
	uint16_t burst_id; // ID of burst to which the packet belongs
	uint8_t  packet_id; // ID of the packet within its original burst
	uint64_t utime; // Timestamp of a packet
	uint32_t data_block; // 4-byte wide content of the packet
} structured_packet_t;

// Reports info on a Structured Packet
// packet_name and data can be NULL
static inline void sp_print(FILE *stream, structured_packet_t *sp, char *packet_name, void *data)
{
	char description[512];
	static const char default_name[] = "Structured Packet";
	const char *name = (packet_name == NULL) ? default_name : packet_name;
	uint16_t size = (data == NULL) ? 0 : sp->size;

	snprintf(description,512,"%s\n"
	                         "	Queue ID  : 0x%8"PRIx16" (%5"PRIu16")\n"
	                         "	Size      : 0x%8"PRIx16" (%5"PRIu16")\n"
	                         "	Size Hash : 0x%8"PRIx8 "\n"
	                         "	Burst ID  : 0x%8"PRIx16" (%5"PRIu16")\n"
	                         "	Packet ID : 0x%8"PRIx8 " (%5"PRIu8 ")\n"
	                         "	Timestamp : 0x%16"PRIx64" (%20"PRIu64 ")\n"
	                         "	Data Block: 0x%8"PRIx32,
	                         name,
	                         sp->queue_id,sp->queue_id,
	                         sp->size,sp->size,
	                         sp->size_hash,
	                         sp->burst_id,sp->burst_id,
	                         sp->packet_id,sp->packet_id,
	                         be64toh(sp->utime), be64toh(sp->utime),
	                         be32toh(sp->data_block));

	hexdump(stream,description,data,size);
}

// Defines a new Structured Packet according to given attributes
static inline void sp_init(structured_packet_t *sp, uint16_t queue_id, uint16_t size, uint16_t burst_id, uint8_t packet_id, uint64_t utime)
{
	sp->queue_id  = queue_id;
	sp->size      = size;
	sp->size32    = get_size32(size);
	sp->size_hash = get_size_hash(size);
	sp->burst_id  = burst_id;
	sp->packet_id = packet_id;
	sp->utime     = htobe64(utime);

	sp->data_block   = 0;
	sp->data_block  |= sp->size_hash;
	sp->data_block <<= 16;
	sp->data_block  |= sp->burst_id;
	sp->data_block <<= 8;
	sp->data_block  |= sp->packet_id;

	// Convert to big-endian to be human-readable in hexdump
	sp->data_block = htobe32(sp->data_block);
}

// Defines a new Structured Packet based on given Data Block, Queue ID and expected Size
static inline void sp_reconstruct(structured_packet_t *sp, uint32_t data_block, uint16_t queue_id, uint16_t size, uint64_t utime_block)
{
	uint32_t conv_data_block = be32toh(data_block);

	sp->data_block = data_block;

	sp->packet_id     = conv_data_block;
	conv_data_block >>= 8;
	sp->burst_id      = conv_data_block;
	conv_data_block >>= 16;
	sp->size_hash     = conv_data_block;

	sp->queue_id = queue_id;
	sp->size     = size;
	sp->size32   = get_size32(size);
	sp->utime    = utime_block;
}

// Generates a Structured Packet into space pointed to by data pointer
// WARNING: Expects space aligned to a multiple of 4 bytes ((sp->size + 3 ) & 0xFFFC)
static inline void sp_generate_data_fast(structured_packet_t *sp, uint32_t *data)
{
	wmemset((wchar_t*)data,sp->data_block,sp->size32);
	if(module->flags & LATENCY_FLAG)
		*(uint64_t*)data = sp->utime;
}

// Checks correctness of Structured Packet in space pointed to by data pointer
// Prints ERROR to stderr on fail and return non-zero value
// WARNING: Expects space aligned to a multiple of 4 bytes ((sp->size + 3 ) & 0xFFFC)
static inline int sp_check_data_fast(structured_packet_t *sp, uint32_t *data)
{
	uint16_t i;
	uint16_t size32_down = sp->size / 4;
	uint32_t *data_rdptr = data;
	uint8_t  invalid_bytes_cnt = (sp->size32 * 4) - sp->size;

	if (sp->size == 0) {
		sp_print(stderr,sp,"ERROR: Zero-sized packet!",data);
		return -1;
	}
	if (sp->size_hash != get_size_hash(sp->size)) {
		sp_print(stderr,sp,"ERROR: Size Hash mismatch!",data);
		return -1;
	}

	// Skip Timestamp
	if(module->flags & LATENCY_FLAG) {
		i = 2;
		data_rdptr += 2;
	} else {
		i = 0;
	}

	// Check aligned portion of data
	for (; i < size32_down; i++) {
		if (*data_rdptr != sp->data_block) {
			sp_print(stderr,sp,"ERROR: Data mismatch!",data);
			return -1;
		}
		data_rdptr++;
	}

	// Check last, non-aligned portion of data if there is such
	if (invalid_bytes_cnt) {
		if ((*data_rdptr << 8*invalid_bytes_cnt) != (sp->data_block << 8*invalid_bytes_cnt)) {
			sp_print(stderr,sp,"ERROR: Data mismatch!",data);
			return -1;
		}
	}

	return 0;
}

// // Structured Packet
// inline void sp_ (structured_packet_t *sp)
// {
// }
