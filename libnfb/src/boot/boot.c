/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb - boot module
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
#include <sys/ioctl.h>
#include <sys/fcntl.h>

#include <libfdt.h>

#include <linux/nfb/boot.h>
#include <nfb/nfb.h>
#include <nfb/boot.h>

#include "../nfb.h"
#include "boot.h"

int nfb_fw_boot(const char *devname, unsigned int image)
{
	int ret;
	struct nfb_device *dev;
	uint64_t img;

	int node;
	int fdt_offset = -1;
	int proplen;
	const void *fdt;
	const fdt32_t *prop;

	dev = nfb_open_ext(devname, O_APPEND);
	if (!dev) {
		ret = errno;
		goto err_open_dev;
	}

	fdt = nfb_get_fdt(dev);

	fdt_for_each_compatible_node(fdt, node, "netcope,binary_slot") {
		prop = fdt_getprop(fdt, node, "id", &proplen);

		if (proplen != sizeof(*prop))
			continue;
		if (fdt32_to_cpu(*prop) == image) {
			fdt_offset = node;
			break;
		}
	}
	if (fdt_offset < 0) {
		ret = ENODEV;
		goto err_no_slot;
	}

	prop = fdt_getprop(fdt, fdt_offset, "boot_id", &proplen);
	if (proplen != sizeof(*prop)) {
		ret = EBADF;
		goto err_no_boot;
	}

	img = fdt32_to_cpu(*prop);

	if (ioctl(dev->fd, NFB_BOOT_IOC_ERRORS_DISABLE))
		warnx("Cannot disable errors");
	ret = ioctl(dev->fd, NFB_BOOT_IOC_RELOAD, &img);
	if (ret) {
		ret = errno;
		goto err_ioctl;
	}

	nfb_close(dev);

	return 0;

err_ioctl:
err_no_slot:
err_no_boot:
	nfb_close(dev);
err_open_dev:
	return ret;
}

ssize_t nfb_fw_read_bit(FILE *f, void **data)
{
	return nfb_fw_open_bit(f, data, BITSTREAM_FORMAT_BPI16);
}

void nfb_fw_print_progress(const char *text, int percent)
{
	int i;
	int termwidth;
	struct winsize uk;

	if (ioctl(0, TIOCGWINSZ, &uk) != 0)
		termwidth = 80;
	else
		termwidth = uk.ws_col;

	i = printf(text, percent);
	i += printf(" [");
	termwidth -= i + 2;

	for (i = 0; i < termwidth ; i++) {
		if (i < (percent * termwidth / 100)) {
			putchar('=');
		} else if (i == (percent * termwidth / 100)) {
			putchar('>');
		} else {
			putchar(' ');
		}
	}
	putchar(']');
	putchar(percent == 100 ? '\n' : '\r');
	fflush(stdout);
}

ssize_t nfb_fw_open(const char *path, void **data)
{
	FILE *fd;
	ssize_t ret;

	/* Open firmware file */
	if ((fd = fopen(path, "r")) == NULL) {
		return -EBADF;
	}

	/* Try load as MCS first */
	ret = nfb_fw_open_mcs(fd, data);
	if (ret < 0) {
		ret = nfb_fw_open_bit(fd, data, BITSTREAM_FORMAT_BPI16);
	}
	fclose(fd);
	return ret;
}

ssize_t nfb_fw_read_for_dev(const struct nfb_device *dev, FILE *fd, void **data)
{
	ssize_t ret;

	int node = -1;
	int fdt_offset = -1;
	int proplen;
	const char *bi_type;

	const void *fdt = nfb_get_fdt(dev);

	enum bitstream_format format = BITSTREAM_FORMAT_BPI16;

	fdt_for_each_compatible_node(fdt, node, "netcope,boot_controller") {
		fdt_offset = fdt_subnode_offset(fdt, node, "control-param");
		if (fdt_offset >= 0) {
			//fdt_getprop(fdt, node, "boot-interface-width", &proplen);
			bi_type = fdt_getprop(fdt, fdt_offset, "boot-interface-type", &proplen);
			if (proplen > 0) {
				if (!strcmp(bi_type, "SPI")) {
					format = BITSTREAM_FORMAT_SPI4;
				}
			}
		}
	}

	/* Try load as MCS first */
	ret = nfb_fw_open_mcs(fd, data);
	if (ret < 0) {
		ret = nfb_fw_open_bit(fd, data, format);
	}

	return ret;
}

void nfb_fw_close(void *data)
{
	free(data);
}

int nfb_fw_load(const struct nfb_device *dev, unsigned int image, void *data, size_t size)
{
	return nfb_fw_load_ext(dev, image, data, size, NFB_FW_LOAD_FLAG_VERBOSE);
}

int nfb_fw_load_ext(const struct nfb_device *dev, unsigned int image, void *data, size_t size, int flags)
{
	int node = -1;
	int fdt_offset = -1;
	int proplen;
	const fdt32_t *prop;

	int i;
	int blocks;
	int last_block_size;
	unsigned long address;
	const void *fdt = nfb_get_fdt(dev);

	struct nfb_boot_ioc_mtd mtd;
	struct nfb_boot_ioc_mtd_info mtd_info;

	fdt_for_each_compatible_node(fdt, node, "netcope,binary_slot") {
		prop = fdt_getprop(fdt, node, "id", &proplen);

		if (proplen != sizeof(*prop))
			continue;
		if (fdt32_to_cpu(*prop) == image) {
			fdt_offset = node;
			break;
		}
	}
	if (fdt_offset < 0)
		return ENODEV;

	fdt_offset = fdt_subnode_offset(fdt, node, "control-param");
	if (fdt_offset < 0)
		return ENODEV;

	prop = fdt_getprop(fdt, fdt_offset, "ro", &proplen);
	if (prop)
		return EROFS;

	prop = fdt_getprop(fdt, fdt_offset, "bitstream-offset", &proplen);
	if (proplen == sizeof(*prop)) {
		size -= fdt32_to_cpu(*prop);
		data = (char *) data + fdt32_to_cpu(*prop);
	}

	prop = fdt_getprop(fdt, fdt_offset, "mtd", &proplen);
	if (proplen != sizeof(*prop))
		return EBADF;
	mtd.mtd = fdt32_to_cpu(*prop);
	mtd_info.mtd = mtd.mtd;

	prop = fdt_getprop(fdt, fdt_offset, "base", &proplen);
	if (proplen != sizeof(*prop))
		return EBADF;
	address = fdt32_to_cpu(*prop);

	prop = fdt_getprop(fdt, fdt_offset, "size", &proplen);
	if (proplen != sizeof(*prop))
		return EBADF;
	if (size > fdt32_to_cpu(*prop))
		return ENOMEM;

	if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_INFO, &mtd_info) == -1) {
		return errno;
	}

	blocks = ((size - 1) / mtd_info.erasesize) + 1;
	last_block_size = size % mtd_info.erasesize;

	mtd.size = mtd_info.erasesize;
	mtd.addr = address;

	if (flags & NFB_FW_LOAD_FLAG_VERBOSE)
		printf("Bitstream size: %lu B (%d blocks)\n", size, blocks);

	for (i = 0; i < blocks; i++) {
		if (flags & NFB_FW_LOAD_FLAG_VERBOSE)
			nfb_fw_print_progress("Erasing Flash: %3d%%", i * 100 / blocks);
		if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_ERASE, &mtd) == -1) {
			return errno;
		}
		mtd.addr += mtd_info.erasesize;
	}
	if (flags & NFB_FW_LOAD_FLAG_VERBOSE)
		nfb_fw_print_progress("Erasing Flash: %3d%%", 100);

	mtd.addr = address;
	mtd.data = data;
	for (i = 0; i < blocks; i++) {
		if (flags & NFB_FW_LOAD_FLAG_VERBOSE)
			nfb_fw_print_progress("Writing Flash: %3d%%", i * 100 / blocks);
		if (i == blocks - 1 && last_block_size != 0)
			mtd.size = last_block_size;
		if (ioctl(dev->fd, NFB_BOOT_IOC_MTD_WRITE, &mtd) == -1) {
			return errno;
		}
		mtd.addr += mtd_info.erasesize;
		mtd.data += mtd_info.erasesize;
	}
	if (flags & NFB_FW_LOAD_FLAG_VERBOSE)
		nfb_fw_print_progress("Writing Flash: %3d%%", 100);
	return 0;
}

void nfb_fw_print_slots(const struct nfb_device *dev)
{
	int id;
	int node;
	int proplen;
	const void *fdt;
	const char *module;
	const char *title;
	const fdt32_t *prop;

	fdt = nfb_get_fdt(dev);

	fdt_for_each_compatible_node(fdt, node, "netcope,binary_slot") {
		prop = fdt_getprop(fdt, node, "id", &proplen);

		if (proplen != sizeof(*prop))
			continue;

		id = fdt32_to_cpu(*prop);

		title = fdt_getprop(fdt, node, "title", &proplen);
		if (proplen <= 0)
			continue;
		module = fdt_getprop(fdt, node, "module", &proplen);
		if (proplen <= 0)
			continue;

		printf("%d: %s (%s)\n", id, title, module);
	}
}
