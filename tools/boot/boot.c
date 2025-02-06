/* SPDX-License-Identifier: BSD-3-Clause */
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
#include <pthread.h>
#include <linux/limits.h>

#include <nfb/nfb.h>
#include <nfb/boot.h>
#include <zlib.h>

#include <netcope/nccommon.h>

/* define input arguments of tool */
#define ARGUMENTS	"d:D:w:f:b:F:i:I:lqvh"

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
	CMD_INJECT_DTB,
	CMD_DELETE,
};

struct progress_state {
	void *priv;
	int done;
};


void usage(const char *me)
{
	printf("Usage: %s [-d device] [-b id file] [-f id file] [-F id] [-w id file] [-i file] [-hlqv]\n", me);
	printf("-d device       Path to device [default: %s]\n", nfb_default_dev_path());
	printf("-F slot         Boot device from selected slot\n");
	printf("-w slot file    Write configuration from file to device slot\n");
	printf("-f slot file    Write configuration from file to device slot and boot device\n");
	printf("-b slot file    Quick boot, see below\n");
	printf("-D slot         Delete device slot\n");
	printf("-i file         Print information about configuration file\n");
	printf("-I dtb          Inject DTB to PCI device\n");
	printf("                The device arg should be in BDF+domain notation: dddd:BB:DD.F\n");
	printf("-q              Do not show boot progress\n");
	printf("-v              Be verbose\n");
	printf("-l              Print list of available slots\n");
	printf("-h              Print this help message\n");
	printf("\n");
	printf("Quick boot:\n");
	printf("Boot the device from selected slot and check if the signature\n");
	printf("of running firmware is equal to the requested configuration file.\n");
	printf("If is not equal, do the write + boot action, as with parameter -f\n");
}

int inject_fdt(const char *device, const char *dtb_filename, int flags)
{
	size_t ret;
	char buf[512] = {0};

	FILE *f;
	void *dtb;
	const void *fdt;
	int fdt_offset;
	size_t dtb_size;
	struct nfb_device *dev = NULL;

	char pci_dev[32] = {0};
	uint32_t csum;

	(void) flags;

	/* Read DTB */
	f = fopen(dtb_filename, "r");
	if (f == NULL) {
		ret = -ENOENT;
		goto err_dtb_open;
	}

	fseek(f, 0L, SEEK_END);
	dtb_size = ftell(f);
	fseek(f, 0L, SEEK_SET);

	dtb = malloc(dtb_size);
	if (dtb == NULL) {
		fclose(f);
		ret = -ENOMEM;
		goto err_dtb_alloc;
	}

	ret = fread(dtb, 1, dtb_size, f);
	fclose(f);
	if (ret != dtb_size) {
		ret = -EBADF;
		goto err_dtb_read;
	}

	csum = crc32(0x80000000, dtb, dtb_size);


	/* Try to open the NFB device associated with PCIe slot */
	/* - try as classic NFB path */
	dev = nfb_open(device);
	if (dev == NULL) {
		/* - try as BDF notation */
		snprintf(buf, sizeof(buf), "/dev/nfb/by-pci-slot/%s", device);
		dev = nfb_open(buf);
		/* If dev is still invalid, assume the device is not attached to kernel driver */
	}

	if (dev) {
		fdt = nfb_get_fdt(dev);
		fdt_offset = fdt_path_offset(fdt, "/system/device/endpoint0");
		strncpy(pci_dev, fdt_getprop(fdt, fdt_offset, "pci-slot", NULL), sizeof(pci_dev)-1);

		nfb_close(dev);

		/* Open the device in exclusive mode: ensure no other userspace tool is running */
		dev = nfb_open_ext(strlen(buf) == 0 ? device : buf, O_APPEND);
		if (!dev) {
			warnx("Can't open the NFB device in exclusive mode!");
			ret = errno;
			goto err_open_dev;
		}

		/* This is not optimal as there is a time gap when other tool can open the device */
		nfb_close(dev);
	} else {
		/* Can't open NFB device: assume the device is in the BDF format */
		strncpy(pci_dev, device, sizeof(pci_dev)-1);
	}

	ret = -EBADF;

	snprintf(buf, sizeof(buf), "/sys/bus/pci/devices/%s/", pci_dev);
	if (access(buf, F_OK) != 0) {
		ret = -EINVAL;
		warnx("The device path doesn't exists: %s", buf);
		goto err_unbind;
	}

	/* Unbind device from any driver */
	snprintf(buf, sizeof(buf), "/sys/bus/pci/devices/%s/driver/unbind", pci_dev);
	/* If the device is not bound, it is not error */
	if (access(buf, F_OK) == 0) {
		if (access(buf, W_OK) != 0) {
			ret = -EACCES;
			warnx("Insufficient privileges");
			goto err_unbind;
		}
		snprintf(buf, sizeof(buf), "echo %s > /sys/bus/pci/devices/%s/driver/unbind", pci_dev, pci_dev);
		if (system(buf)) {
			warnx("device unbind from driver failed");
			goto err_unbind;
		}
	}

	/* Write device tree metadata */
	snprintf(buf, sizeof(buf),
			"echo \"len=%lu crc32=%lu busname=%s busaddr=%s\" > /sys/bus/pci/drivers/nfb/dtb_inject_meta",
			dtb_size, (long unsigned) csum, "pci", pci_dev);
	if (system(buf)) {
		warnx("dtb metadata write failed");
		goto err_dtb_write;
	}

	/* Write device tree */
	f = fopen("/sys/bus/pci/drivers/nfb/dtb_inject", "wb");
	if (f == NULL) {
		ret = -EBADF;
		goto err_dtb_write;
	}
	ret = fwrite(dtb, dtb_size, 1, f);
	fclose(f);
	if (ret != 1) {
		ret = -EBADF;
		goto err_dtb_write;
	}

	ret = -EBADF;

	/* Probe device into NFB driver */
	snprintf(buf, sizeof(buf), "echo nfb > /sys/bus/pci/devices/%s/driver_override", pci_dev);
	if (system(buf)) {
		warnx("driver override failed");
		goto err_dtb_write;
	}

	snprintf(buf, sizeof(buf), "echo %s > /sys/bus/pci/drivers_probe", pci_dev);
	if (system(buf)) {
		warnx("drivers probe failed");
		goto err_dtb_write;
	}

	snprintf(buf, sizeof(buf), "echo > /sys/bus/pci/devices/%s/driver_override", pci_dev);
	if (system(buf)) {
		warnx("driver override restore failed");
	}

	return 0;


err_dtb_write:
err_open_dev:
err_unbind:
err_dtb_read:
	free(dtb);
err_dtb_alloc:
err_dtb_open:
	return ret;
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

int get_path_by_pci_slot(const char *path, char *path_by_pci_slot, size_t size)
{
	int ret;
	struct nfb_device *dev;
	const void *fdt;

	const char *slot;

	if (size < 1)
		return -EINVAL;

	dev = nfb_open(path);
	if (dev == NULL)
		return -ENODEV;

	fdt = nfb_get_fdt(dev);
	ret = fdt_path_offset(fdt, "/system/device/endpoint0");
	slot = fdt_getprop(fdt, ret, "pci-slot", NULL);
	if (slot == NULL)
		return -ENODEV;

	ret = snprintf(path_by_pci_slot, size, "/dev/nfb/by-pci-slot/%s", slot);

	nfb_close(dev);
	return ret;
}

int check_boot_success(const char *path_by_pci, int cmd, const char *filename)
{
	int ret = 0;
	struct nfb_device *dev;
	void *fdt = NULL;
	int diff;

	dev = nfb_open(path_by_pci);
	if (dev == NULL) {
		warnx("can't open device file after boot; can be caused by a corrupted configuration file or unsupported hotplug on this platform");
	} else {
		if (cmd == CMD_WRITE_AND_BOOT) {
			archive_read_first_file_with_extension(filename, ".dtb", &fdt);
			if (fdt == NULL) {
				warnx("can't read firmware info from configuration file, after-boot checks are not performed");
			} else {
				diff = firmware_diff(nfb_get_fdt(dev), fdt);

				if (diff == DIFF_ERROR) {
					warnx("can't check equality of the running firmware to the requested configuration file");
				} else if (diff != DIFF_SAME) {
					warnx("boot failed: the signature of running firmware is not equal to signature of written configuration file");
					ret = EBADF;
				}
			}
		}
		nfb_close(dev);
	}
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

int do_delete(const char *path, int slot)
{
	int ret;
	struct nfb_device *dev;

	dev = nfb_open(path);
	if (dev == NULL) {
		warn("can't open device file");
		return -ENODEV;
	}

	ret = nfb_fw_delete(dev, slot);
	nfb_close(dev);
	return ret;
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

static void *show_progress(void *arg)
{
	struct progress_state *ps = arg;
	do {
		nfb_fw_load_progress_print(ps->priv);
		usleep(200000);
	} while (ps->done == 0);

	return NULL;
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
		".bin",
	};

	pthread_t pt;
	struct progress_state ps;

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

	if ((flags & FLAG_QUIET) == 0 ) {
		ps.done = 0;
		ps.priv = nfb_fw_load_progress_init(dev);
		pthread_create(&pt, NULL, show_progress, &ps);
	}

	ret = nfb_fw_load_ext_name(dev, slot, data, size, flags & FLAG_QUIET ? 0 : NFB_FW_LOAD_FLAG_VERBOSE, filename);

	if ((flags & FLAG_QUIET) == 0 ) {
		ps.done = 1;
		pthread_join(pt, NULL);
		nfb_fw_load_progress_destroy(ps.priv);
	}

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

	/* Let settle links in the /dev/ */
	usleep(100000);
	return ret;
}

int card_requires_sleep(const void *fdt) {
	int fdt_offset;
	const void *prop;
	int len;

	fdt_offset = fdt_path_offset(fdt, "/firmware/");
	prop = fdt_getprop(fdt, fdt_offset, "card-name", &len);
	if (prop != NULL) {
		if (!strcmp(prop, "N6010")) {
			return 1;
		}
	}
	return 0;
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
		warnx("can't check firmware equality, write enforced");

	if (diff != DIFF_SAME) {
		if (card_requires_sleep(nfb_get_fdt(dev))) {
			usleep(1000000);
		}
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
	const char *path = nfb_default_dev_path();
	char path_by_pci[PATH_MAX];
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
		case 'D':
			cmd = CMD_DELETE;
			slot_arg = optarg;
			break;
		case 'i':
			cmd = CMD_PRINT_INFO;
			filename = optarg;
			break;
		case 'I':
			cmd = CMD_INJECT_DTB;
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
	case CMD_INJECT_DTB:
		ret = inject_fdt(path, filename, flags);
		break;
	case CMD_DELETE:
		ret = do_delete(path, slot);
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
		c = get_path_by_pci_slot(path, path_by_pci, sizeof(path_by_pci));
		ret = do_boot(path, slot);
		if (ret) {
			if (cmd == CMD_WRITE_AND_BOOT)
				warnx("however, the configuration was successfully written into device slot");
			goto err;
		}

		if (c < 0) {
			warnx("can't get device path by PCI slot, after-boot checks skipped");
		} else {
			ret = check_boot_success(path_by_pci, cmd, filename);
		}
	}

err:
	return ret;
}
