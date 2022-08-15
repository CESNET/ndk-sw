/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Flash and boot tool
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <err.h>
#include <archive.h>
#include <archive_entry.h>

#include <nfb/nfb.h>
#include <nfb/boot.h>

#include <netcope/nccommon.h>

/* define input arguments of tool */
#define ARGUMENTS	"d:w:f:b:F:i:lqvh"

#define PRINT_WARNING_WHEN_USING_BITSTREAM 0
#define REQUIRE_FORCE_WHEN_USING_BITSTREAM 0
#define REQUIRE_FORCE_WHEN_CARD_MISMATCH   1

#define FLAG_QUIET      1
#define FLAG_FORCE      2
#define FLAG_BITSTREAM  4

enum fw_diff_values {
	DIFF_SAME = 0,
	DIFF_DIFFERENT,
	DIFF_ERROR,
	DIFF_CARD,
};

enum commands {
	CMD_UNKNOWN,
	CMD_USAGE,
	CMD_PRINT_SLOTS,
	CMD_PRINT_INFO,
	CMD_WRITE_AND_BOOT,
	CMD_BOOT,
	CMD_WRITE,
	CMD_QUICK_BOOT,
};

void usage(const char *me)
{
	printf("Usage: %s [-d device] [-b id file] [-f id file] [-F id] [-w id file] [-i file] [-hlqv]\n", me);
	printf("-d device       Path to device [default: %s]\n", NFB_DEFAULT_DEV_PATH);
	printf("-b slot file    Quick boot: Write configuration to device slot only when their signatures differs\n");
	printf("-w slot file    Write configuration from file to device slot\n");
	printf("-f slot file    Write configuration from file to device slot and boot device\n");
	printf("-F slot         Boot device from selected slot\n");
	printf("-i file         Print information about firmware file\n");
	printf("-q              Do not show boot progress\n");
	printf("-v              Be verbose\n");
	printf("-l              Print list of available slots\n");
	printf("-h              Print this help message\n");
}

ssize_t archive_read_first_file_with_extension(const char *filename, const char *ext, void **outp)
{
	int ret;
	ssize_t size = -ENOENT;
	struct archive_entry *entry;
	struct archive *a = archive_read_new();
	const char *fname, *fname_ext;

	archive_read_support_filter_gzip(a);
	archive_read_support_format_tar(a);

	ret = archive_read_open_filename(a, filename, 16384);
	if (ret != ARCHIVE_OK)
		return -EBADFD;
	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		fname = archive_entry_pathname(entry);
		fname_ext = strstr(fname, ext);
		if (fname_ext == fname + strlen(fname) - strlen(ext)) {
			*outp = malloc(archive_entry_size(entry));
			if (*outp)
				size = archive_read_data(a, *outp, archive_entry_size(entry));
			else
				size = -ENOMEM;
			break;
		}
	}

	archive_read_close(a);
	archive_read_free(a);

	return size;
}

enum fw_diff_values firmware_diff(const void *fdt, const void *ffdt)
{
	enum fw_diff_values ret;
	int fdt_offset, ffdt_offset;
	int len, flen;
	const void *prop, *fprop;
	const uint32_t *prop32, *fprop32;

	fdt_offset = fdt_path_offset(fdt, "/firmware/");
	ffdt_offset = fdt_path_offset(ffdt, "/firmware/");

	ret = DIFF_SAME;
	prop32 = fdt_getprop(fdt, fdt_offset, "build-time", &len);
	fprop32 = fdt_getprop(ffdt, ffdt_offset, "build-time", &flen);
	if (len != sizeof(*fprop32) || flen != sizeof(*fprop32))
		ret = DIFF_ERROR;
	else if (*prop32 != *fprop32)
		ret = DIFF_DIFFERENT;

	prop = fdt_getprop(fdt, fdt_offset, "project-name", &len);
	fprop = fdt_getprop(ffdt, ffdt_offset, "project-name", &flen);
	if (prop == NULL || fprop == NULL)
		ret = DIFF_ERROR;
	else if (strcmp(prop, fprop))
		ret = DIFF_DIFFERENT;

	prop = fdt_getprop(fdt, fdt_offset, "card-name", &len);
	fprop = fdt_getprop(ffdt, ffdt_offset, "card-name", &flen);
	if (prop == NULL || fprop == NULL)
		ret = DIFF_ERROR;
	else if (strcmp(prop, fprop))
		ret = DIFF_CARD;

	return ret;
}

int print_slots(const char *path)
{
	struct nfb_device *dev;

	dev = nfb_open(path);
	if (dev == NULL) {
		warn("can't open device file");
		return -ENODEV;
	}

	nfb_fw_print_slots(dev);
	nfb_close(dev);

	return 0;
}

int print_info(const char *filename, int verbose)
{
	static const int BUFFER_SIZE = 64;

	int i;
	int len;
	int node;
	int fdt_offset;
	void *fdt = NULL;
	const void *prop;
	const uint32_t *prop32;

	char buffer[BUFFER_SIZE];
	time_t build_time;

	archive_read_first_file_with_extension(filename, ".dtb", &fdt);
	if (fdt == NULL) {
		warnx("can't read firmware file");
		return EBADF;
	}

	printf("------------------------------------ Firmware info ----\n");

	fdt_offset = fdt_path_offset(fdt, "/firmware/");

	prop = fdt_getprop(fdt, fdt_offset, "card-name", &len);
	if (len > 0)
		printf("Card name                  : %s\n", (const char *)prop);

	prop = fdt_getprop(fdt, fdt_offset, "project-name", &len);
	if (len > 0)
		printf("Project name               : %s\n", (const char *)prop);

	prop32 = fdt_getprop(fdt, fdt_offset, "build-time", &len);
	if (len == sizeof(*prop32)) {
		build_time = fdt32_to_cpu(*prop32);
		strftime(buffer, BUFFER_SIZE, "%Y-%m-%d %H:%M:%S", localtime(&build_time));
		printf("Built at                   : %s\n", buffer);
	}

	prop = fdt_getprop(fdt, fdt_offset, "build-tool", &len);
	if (len > 0)
		printf("Build tool                 : %s\n", (const char *)prop);

	prop = fdt_getprop(fdt, fdt_offset, "build-author", &len);
	if (len > 0)
		printf("Build author               : %s\n", (const char *)prop);

	i = 0;
	fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
		i++;
	}
	printf("Network interfaces         : %d\n", i);
	if (verbose) {
		i = 0;
		fdt_for_each_compatible_node(fdt, node, "netcope,transceiver") {
			prop = fdt_getprop(fdt, node, "type", &len);
			printf(" * Interface %d             : %s\n", i,
					len > 0 ? (const char *) prop : "Unknown");
			i++;
		}
	}

	free(fdt);
	return 0;
}

int do_write_with_dev(struct nfb_device *dev, int slot, const char *filename, const void *fdt, int flags)
{
	unsigned int i;
	int ret;
	FILE *fd;
	void *fdata = NULL;
	void *data;
	ssize_t size;

	static const char *binary_suffixes[] = {
		".bit",
		".rbf",
		".rpd",
	};

	if (fdt == NULL) {
		/* RAW bitsream is supplied */

		if (PRINT_WARNING_WHEN_USING_BITSTREAM && !(flags & FLAG_FORCE))
			warnx("you're probably using raw bitstream file type, which is deprecated; please use the .nfw file type");

		if (REQUIRE_FORCE_WHEN_USING_BITSTREAM && !(flags & FLAG_FORCE)) {
			warnx("if you want to use raw bitstream file type, use --force parameter");
			ret = EBADF;
			goto err_no_force;
		}
	} else if (!(flags & FLAG_FORCE)) {
		if (firmware_diff(nfb_get_fdt(dev), fdt) == DIFF_CARD) {
			warnx("firmware file doesn't match card type");
			if (REQUIRE_FORCE_WHEN_CARD_MISMATCH) {
				warnx("if you want still use thie firmware file, use --force parameter");
				ret = EBADF;
				goto err_no_force;
			}
		}
	}

	for (i = 0; i < NC_ARRAY_SIZE(binary_suffixes); i++) {
		size = archive_read_first_file_with_extension(filename, binary_suffixes[i], &fdata);
		if (size >= 0)
			break;
	}

	if (fdata != NULL) {
		fd = fmemopen(fdata, size, "rb");
	} else {
		fd = fopen(filename, "r");
	}

	if (fd == NULL) {
		warnx("failed open firmware file");
		ret = ENOENT;
		goto err_fopen;
	}

	size = nfb_fw_read_for_dev(dev, fd, &data);
	if (size < 0) {
		warnx("can't load firmware file");
		ret = EBADF;
		goto err_nfb_fw_open;
	}

	ret = nfb_fw_load_ext(dev, slot, data, size, flags & FLAG_QUIET ? 0 : NFB_FW_LOAD_FLAG_VERBOSE);
	switch (ret) {
	case 0:
		break;
	case ENODEV:
		warnx("specified slot does not exists");
		break;
	default:
		errno = ret;
		warn("can't write firmware to device");
		break;
	}

	nfb_fw_close(data);

err_nfb_fw_open:
	fclose(fd);
err_fopen:
	if (fdata != NULL)
		free(fdata);
err_no_force:
	return ret;
}

int do_write(const char *path, int slot, const char *filename, int flags)
{
	int ret;
	struct nfb_device *dev;
	void *fdt = NULL;

	dev = nfb_open(path);
	if (dev == NULL) {
		warn("can't open device file");
		return ENODEV;
	}

	/* fdt can be NULL after this call */
	archive_read_first_file_with_extension(filename, ".dtb", &fdt);

	ret = do_write_with_dev(dev, slot, filename, fdt, flags);

	if (fdt)
		free(fdt);

	nfb_close(dev);
	return ret;
}

int do_boot(const char *path, int slot)
{
	int ret;
	ret = nfb_fw_boot(path, slot);

	switch (ret) {
	case 0:
		break;
	case ENODEV:
		warnx("specified slot does not exists");
		break;
	default:
		warnx("boot failed: %s", strerror(ret));
		break;
	}
	return ret;
}

int do_quick_boot(const char *path, int slot, const char *filename, int flags)
{
	int ret;
	enum fw_diff_values diff;
	void *fdt = NULL;
	struct nfb_device *dev;

	archive_read_first_file_with_extension(filename, ".dtb", &fdt);
	if (fdt == NULL) {
		warnx("can't read firmware file");
		ret = EBADF;
		goto err_read_archive;
	}

	ret = do_boot(path, slot);
	if (ret) {
		goto err_boot;
	}

	dev = nfb_open(path);
	if (dev == NULL) {
		ret = ENODEV;
		warn("can't open device file after boot");
		goto err_free_fdt;
	}

	diff = firmware_diff(nfb_get_fdt(dev), fdt);

	if (diff == DIFF_ERROR)
		warnx("can't check firmware difference, write enforced");

	if (diff != DIFF_SAME) {
		ret = do_write_with_dev(dev, slot, filename, fdt, flags);
		if (ret) {
			nfb_close(dev);
			goto err_do_write;
		}
		nfb_close(dev);
		ret = do_boot(path, slot);
	} else {
		nfb_close(dev);
	}

err_do_write:
err_free_fdt:
err_boot:
	free(fdt);
err_read_archive:
	return ret;
}

int main(int argc, char *argv[])
{
	int c;
	long slot = -1;
	char *slot_arg = NULL;
	char *path = NFB_DEFAULT_DEV_PATH;
	char *filename = NULL;
	int ret = 0;
	int flags = 0;
	int verbose = 0;
	enum commands cmd = CMD_UNKNOWN;

	if (!isatty(fileno(stdout)))
		flags |= FLAG_QUIET;

	while ((c = getopt(argc, argv, ARGUMENTS)) != -1) {
		switch (c) {
		case 'h':
			cmd = CMD_USAGE;
			break;
		case 'd':
			path = optarg;
			break;
		case 'l':
			cmd = CMD_PRINT_SLOTS;
			break;
		case 'w':
			cmd = CMD_WRITE;
			slot_arg = optarg;
			if (optind >= argc){
				errx(-1, "'-w' missing argument");
			}
			filename = argv[optind];
			break;
		case 'f':
			cmd = CMD_WRITE_AND_BOOT;
			slot_arg = optarg;
			if (optind >= argc){
				errx(-1, "'-f' missing argument");
			}
			filename = argv[optind];
			break;
		case 'F':
			cmd = CMD_BOOT;
			slot_arg = optarg;
			break;
		case 'i':
			cmd = CMD_PRINT_INFO;
			filename = optarg;
			break;
		case 'q':
			flags |= FLAG_QUIET;
			break;
		case 'b':
			cmd = CMD_QUICK_BOOT;
			slot_arg = optarg;
			if (optind >= argc){
				errx(-1, "'-b' missing argument");
			}
			filename = argv[optind];
			break;
		case 'v':
			verbose++;
			break;
		default:
			errx(-1, "unknown argument - %c", optopt);
		}
	}

	if (argc <= 1) {
		errx(-1, "no arguments, try -h for help");
	}

	if (slot_arg && nc_strtol(slot_arg, &slot)) {
		errx(-1, "wrong 'slot' argument");
	}


	switch (cmd) {
	case CMD_USAGE:
		usage(argv[0]);
		break;
	case CMD_PRINT_SLOTS:
		ret = print_slots(path);
		break;
	case CMD_PRINT_INFO:
		ret = print_info(filename, verbose);
		break;
	case CMD_QUICK_BOOT:
		ret = do_quick_boot(path, slot, filename, flags);
		break;
	case CMD_UNKNOWN:
		warnx("no command");
		ret = -1;
	default:
		break;
	}

	if (cmd == CMD_WRITE_AND_BOOT || cmd == CMD_WRITE) {
		ret = do_write(path, slot, filename, flags);
		if (ret)
			goto err;
	}

	if (cmd == CMD_WRITE_AND_BOOT || cmd == CMD_BOOT) {
		ret = do_boot(path, slot);
		if (ret)
			goto err;
	}

err:
	return ret;
}
