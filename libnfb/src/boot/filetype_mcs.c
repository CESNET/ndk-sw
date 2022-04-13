/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module - MCS file type
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <byteswap.h>

/*
 * function load data from configuration .mcs file
 * @file	IN:	file from where are data loaded
 * @size	OUT:	size of loaded data
 * @data	OUT:	pointer to valid loaded data
 */
ssize_t nfb_fw_open_mcs(FILE *fd, void **pdata)
{
	#define BUFFER_SIZE 128
	ssize_t size = 0;

	unsigned int word_size = 0;
	char buffer[BUFFER_SIZE];

	uint32_t *data;

	unsigned long i;
	unsigned int x;

	unsigned int t[4];

	if (fgets(buffer, BUFFER_SIZE, fd) == NULL)
		return -1;

	rewind(fd);

	/* Every line must contain data in specific format (we check the first line here): */
	/* Start code (':'), Byte count (1B), Address (2B), Record type (1B), Data (xB), Checksum (1B) */
	if (sscanf(buffer, ":%02x%04x%02x%02x", &t[0], &t[1], &t[2], &t[3]) != 4)
		return -1;

	/* first iteration through file - purpose: know exact size how much data we need to alocate */
	while (fgets(buffer, BUFFER_SIZE, fd) != NULL) {
		/* reference from Intel HEX - 8 and 9 CHAR is record type - if it's 00, that mean DATA record */
		if (buffer[7] == '0' && buffer[8] == '0' ) {
			/* again Intel HEX format specification - get size of data from 0 and 1 CHAR on line*/
			sscanf(buffer + 1, "%02X", &word_size);
			/* increment malloc_size on basis of word_size from line*/
			size += word_size;
		}
	}
	
	rewind(fd);

	/* malloc exact space for data */
	data = malloc(size);
	if (data == NULL) {
		fprintf(stderr,"error alocating data\n");
		return -1;
	}

	i = 0;
	/* second iteration through file - get raw data line by line */
	while (fgets(buffer, BUFFER_SIZE, fd) != NULL) {
		/* verify if is't data record */
		if (buffer[7] == '0' && buffer[8] == '0') {
			/* know how much data to read */
			sscanf(buffer + 1, "%02X", &word_size);
			/* read data */
			for (x = 0; x < word_size / 4; x++) {
				sscanf(buffer + 9 + (x * 8), "%08X", data + i);
				/* Swap bytes in 32b word */
				data[i] = bswap_32(data[i]);
				i++;
			}
		}
	}

	*pdata = data;
	return size;
}
