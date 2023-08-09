/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Board identification tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/param.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <time.h>
#include <getopt.h>
#include <string.h>
#include <math.h>

#include <nfb/boot.h>
#include <nfb/nfb.h>

#include <libconfig.h>


#define ARGUMENTS	"d:hw:"

struct flash_ident {
	uint16_t id_magic;
	uint16_t card_type;
	uint32_t serial_number;
	uint32_t chip_id;
	uint32_t hw_rev;
	uint32_t birth_date;
	uint32_t distr;
	uint32_t reserved[2];
	char     card_id[32];
	char     card_spec[32];
};

struct card_ident {
	char     card_name[32];
	int      serial_number;
	uint32_t chip_id;
	uint32_t hw_rev;
	time_t   birth_date;
	int      distr;
	char     card_id[32];
	char     card_spec[32];
};

struct flash_info {
	const char *name;
	int mtd;
	size_t base;
	int card_type_id;
};

const struct flash_info flash_infos [] = {
	/* card name, 		MTD ID,	base address,	card type ID */
	{ "NFB-40G",		-1,	-1,		0x04 },

	{ "NFB-40G2",		0, 	0x00000000,	0x01 },
	{ "NFB-40G2_SG3",	0,	0x00000000,	0x03 },

	{ "NFB-100G1",		0,	0x00000000,	0x02 },

	{ "NFB-100G2",		0,	0x01fc0000,	0x00 },
	{ "NFB-100G2Q",		0,	0x01fc0000,	0x05 },
	{ "NFB-100G2C",		0,	0x01fc0000, 	0x08 },

	{ "NFB-200G2QL",	0,	0x03fc0000,	0x06 },

	{ "FB1CGG",		-1,	0x00000002,	0x07 },
	{ "FB2CGG3",		-1,	0x00000002,	0x09 },
	{ "FB4CGG3",		-1,	0x00000002,	0x0A },

	/* Last item */
	{ NULL,	       		-1,	-1,	 	0x00 },
};

void usage(char *tool)
{
	printf("Usage: %s [-hsw] [-d str] [-f str]\n", tool);
	printf("-d str      path to device file to use\n");
	printf("-w file     file with card identification to write\n");
	printf("-h          print this help, how to use flash tool\n");
}

int flash_ident_parse(struct card_ident *ci, const void *from)
{
	int ret = 0;
	int ct;

	const struct flash_info *flash_info = flash_infos;
	const struct flash_ident *fi = from;

	if (be16toh(fi->id_magic) != 0xA503)
		warnx("ID struct magic not present");

	ct = be16toh(fi->card_type);

	while (flash_info->name) {
		if (ct == flash_info->card_type_id)
			break;
		flash_info++;
	}

	if (flash_info->name == NULL)
		warnx("Card type %d not found in DB", ct);

	ci->serial_number = be32toh(fi->serial_number);
	ci->chip_id = be32toh(fi->chip_id);
	ci->hw_rev  = be32toh(fi->hw_rev);
	ci->birth_date = be32toh(fi->birth_date);
	ci->distr      = be32toh(fi->distr);

	memcpy(ci->card_id, fi->card_id, 32);
	memcpy(ci->card_spec, fi->card_spec, 32);

	return ret;
}

int flash_ident_store(void *to, const struct card_ident *ci)
{
	int ret = 0;

	const struct flash_info *flash_info = flash_infos;
	struct flash_ident *fi = (struct flash_ident*) to;

	while (flash_info->name) {
		if (strcmp(ci->card_name, flash_info->name) == 0)
			break;
		flash_info++;
	}

	if (flash_info->name == NULL)
		errx(1, "Card name %s found in DB", ci->card_name);

	fi->id_magic = htobe16(0xA503);
	fi->card_type = htobe32(flash_info->card_type_id);
	fi->serial_number = htobe32(ci->serial_number);
	fi->chip_id = htobe32(ci->chip_id);
	fi->hw_rev = htobe32(ci->hw_rev);
	fi->birth_date = htobe32(ci->birth_date);

	memcpy(fi->card_id, ci->card_id, 32);
	memcpy(fi->card_spec, ci->card_spec, 32);

	return ret;
}

int flash_ident_load_from_config(struct card_ident *ident, const char *file)
{
	int ret;
	const char *str;
	config_t cfg;
	struct tm tm;

	config_init(&cfg);
	ret = config_read_file(&cfg, file);
	if (!ret)
		err(1, "Unable to read config file: %s (line %d)", config_error_text(&cfg), config_error_line(&cfg));

	config_lookup_int(&cfg, "card.sn", &ident->serial_number);

	config_lookup_string(&cfg, "card.name", &str);
	memset(ident->card_name, 0, 32);
	memcpy(ident->card_name, str, MIN(strlen(str), 32));

	config_lookup_string(&cfg, "card.id", &str);
	memset(ident->card_id, 0, 32);
	memcpy(ident->card_id, str, MIN(strlen(str), 32));

	config_lookup_string(&cfg, "card.spec", &str);
	memset(ident->card_spec, 0, 32);
	memcpy(ident->card_spec, str, MIN(strlen(str), 32));

	config_lookup_string(&cfg, "card.birth_date", &str);

	strptime(str, "%F %T", &tm);
	ident->birth_date = mktime(&tm);

	config_destroy(&cfg);
	return 0;
}

void card_ident_print(struct card_ident *ci)
{
	printf("Card name                  : %s\n", ci->card_name);
	printf("Serial number              : %d\n", ci->serial_number);
	printf("Birth date                 : %s\n", ctime(&ci->birth_date));
	printf("Card ID                    : %.*s\n", 32, ci->card_id);
	printf("Card spec                  : %.*s\n", 32, ci->card_spec);
	printf("Chip ID                    : %x\n", ci->chip_id);
	printf("HW rev                     : %x\n", ci->hw_rev);
	printf("Distr                      : %x\n", ci->distr);
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int c;
	int fdt_offset;
	const char *card_name;
	char *file = NULL;
	char *path = NFB_DEFAULT_DEV_PATH;

	void *buffer;
	void *fip;
	const void *fdt;

	size_t erasesize;
	size_t base;
	size_t offset;

	struct nfb_device *dev;
	struct card_ident ident;
	const struct flash_info *flash_info = flash_infos;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
			case 'd':
				path = optarg;
				break;
			case 'h':
				usage(argv[0]);
				return 0;
				break;
			case 'w':
				file = optarg;
				break;
			default:
				fprintf(stderr, "unknown argument - %c\n", optopt);
				return -1;
		}
	}

	dev = nfb_open(path);
	if (dev == NULL) {
		err(1, "Can't open device");
	}

	fdt = nfb_get_fdt(dev);
	fdt_offset = fdt_path_offset(fdt, "/firmware/");

	if (dev == NULL) {
		err(1, "Can't open device");
	}

	card_name = fdt_getprop(fdt, fdt_offset, "card-name", NULL);
	if (card_name == NULL) {
		nfb_close(dev);
		errx(1, "Can't get card name");
	}

	while (flash_info->name) {
		if (strcmp(flash_info->name, card_name) == 0)
			break;
		flash_info++;
	}

	if (flash_info->name == NULL)
		errx(1, "Card %s not found in DB", card_name);

	erasesize = nfb_mtd_get_erasesize(dev, flash_info->mtd);

	buffer = malloc(erasesize);
	if (buffer == NULL)
		return -ENOMEM;

	base = flash_info->base & ~(erasesize - 1);
	offset = flash_info->base - base;

	if (nfb_mtd_read(dev, flash_info->mtd, base, buffer, erasesize) != 0)
		err(1, "Can't read data from flash");

	fip = (void *)(((uint8_t *)buffer) + offset);
	flash_ident_parse(&ident, fip);

	if (file) {
		flash_ident_load_from_config(&ident, file);
		flash_ident_store(fip, &ident);
		flash_ident_parse(&ident, fip);

		nfb_mtd_erase(dev, flash_info->mtd, base, erasesize);
		nfb_mtd_write(dev, flash_info->mtd, base, buffer, erasesize);
	}

	card_ident_print(&ident);

	nfb_close(dev);
	return ret;
}
