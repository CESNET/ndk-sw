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
#include <libgen.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/eventfd.h>

#include <libfdt.h>

#include <uapi/linux/nfb-fpga-image-load.h>

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

ssize_t nfb_fw_open_rbf(FILE *fd, void **pdata, enum bitstream_format f)
{
	const uint16_t FLAGS_HEADER_UNKNOWN = 0xFF05;
	const uint16_t FLAGS_BLOCK_UNKNOWN = 0x0401;
	const size_t START_ADDR = 0x20000;
	const size_t BLOCK_SIZE = 0x1000;

	uint16_t block_size;
	uint8_t *data8;
	uint16_t *data16;

	int i, readen;
	size_t offset, size_file, block_count, size_total;

	(void) f;

	/* get size of whole bitstream file in bytes */
	fseek(fd, 0, SEEK_END);
	size_file = ftell(fd);
	rewind(fd);

	block_count = (size_file + BLOCK_SIZE-1) / BLOCK_SIZE;

	/* total size in bytes aligned to block */
	size_total = START_ADDR + size_file + 4 * block_count;
	/* padding */
	size_total += BLOCK_SIZE - (size_total % BLOCK_SIZE);
	data8 = malloc(size_total);
	if (data8 == NULL)
		return -ENOMEM;

	data16 = (uint16_t*)data8;

	/* Fill header: start and end address */
	memset(data8, 0xFF, START_ADDR);
	data16[0] = (START_ADDR >> 2) >>  0;
	data16[1] = (START_ADDR >> 2) >> 16;
	data16[2] = (size_total >> 2) >>  0;
	data16[3] = (size_total >> 2) >> 16;
	data16[64] = FLAGS_HEADER_UNKNOWN;

	i = START_ADDR;
	offset = 0;
	block_size = BLOCK_SIZE;
	while (offset < size_file) {
		if (block_size > size_file - offset) {
			block_size = size_file - offset;
		}

		data16[(i >> 1) + 0] = FLAGS_BLOCK_UNKNOWN;
		data16[(i >> 1) + 1] = block_size;
		i += 4;

		readen = fread(data8 + i, block_size, 1, fd);
		if (readen != 1) {
			free(data8);
			return -ENOBUFS;
		}
		offset += block_size;
		i += block_size;
	}

	memset(data8 + i, 0xFF, size_total - i);
	*pdata = data8;
	return i;
}

ssize_t nfb_fw_read_for_dev(const struct nfb_device *dev, FILE *fd, void **data)
{
	ssize_t ret;

	int node = -1;
	int fdt_offset = -1;
	int proplen;
	const char *bi_type;
	const uint32_t* prop32;

	const void *fdt = nfb_get_fdt(dev);

	enum bitstream_format format = BITSTREAM_FORMAT_BPI16;

	fdt_for_each_compatible_node(fdt, node, "cesnet,pmci") {
		format = BITSTREAM_FORMAT_NATIVE;
		ret = nfb_fw_open_rpd(fd, data, format);
		return ret;
	}

	fdt_for_each_compatible_node(fdt, node, "bittware,bmc") {
		format = BITSTREAM_FORMAT_NATIVE;
		ret = nfb_fw_open_rpd(fd, data, format);
		return ret;
	}

	fdt_for_each_compatible_node(fdt, node, "brnologic,m10bmc_spi") {
		format = BITSTREAM_FORMAT_NATIVE;
		ret = nfb_fw_open_rpd(fd, data, format);
		return ret;
	}

	/* For Intel Stratix 10 and Agilex FPGAs */
	fdt_for_each_compatible_node(fdt, node, "netcope,intel_sdm_controller") {
		prop32 = fdt_getprop(fdt, node, "boot_en", &proplen);
		if (proplen == sizeof(*prop32) && fdt32_to_cpu(*prop32) != 0) {
			// TODO: figure out if bitswap needs to be used (currently YES by default)
			format = BITSTREAM_FORMAT_INTEL_AS;
			ret = nfb_fw_open_rpd(fd, data, format);
			return ret;
		}
	}

	fdt_for_each_compatible_node(fdt, node, "netcope,boot_controller") {
		fdt_offset = fdt_subnode_offset(fdt, node, "control-param");
		if (fdt_offset >= 0) {
			//fdt_getprop(fdt, node, "boot-interface-width", &proplen);
			bi_type = fdt_getprop(fdt, fdt_offset, "boot-interface-type", &proplen);
			if (proplen > 0) {
				if (!strcmp(bi_type, "SPI")) {
					format = BITSTREAM_FORMAT_SPI4;
				} else if (!strcmp(bi_type, "INTEL-AVST")) {
					format = BITSTREAM_FORMAT_INTEL_AVST;
				}
			}
		}
	}

	if (format == BITSTREAM_FORMAT_INTEL_AVST) {
		ret = nfb_fw_open_rbf(fd, data, format);
	} else {

		/* Try load as MCS first */
		ret = nfb_fw_open_mcs(fd, data);
		if (ret < 0) {
			ret = nfb_fw_open_bit(fd, data, format);
		}
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

int nfb_fw_load_fpga_image_load(const struct nfb_device *dev, void *data, size_t size, int flags, int slot_fdt_offset)
{
	int i;
	int processing_node;
	int fdt_offset;
	struct fpga_image_status fs;
	int ret;
	int efd;
	int pct;
	unsigned int prev_progress;
	const char *text = NULL;

	const void *fdt = nfb_get_fdt(dev);

	const uint64_t* prop_mod_off;
	const uint8_t*  prop_mod_mask;
	const uint8_t*  prop_mod_val;

	int proplen;
	int mod_len;
	uint64_t mod_off;
	uint8_t *udata = data;

	if (ioctl(dev->fd, FPGA_IMAGE_LOAD_STATUS, &fs)) {
		ret = errno;
		return ret;
	}

	if (fs.progress != FPGA_IMAGE_PROG_IDLE)
		return -EBUSY;

	efd = eventfd(0, 0);

	struct fpga_image_write fw = {
		.flags = 0,
		.size = size,
		.evtfd = efd,
		.buf = (uint64_t) data,
	};

	if (flags & NFB_FW_LOAD_FLAG_VERBOSE) {
		printf("Bitstream size: %lu B\n", size);
	}

	fdt_offset = fdt_subnode_offset(fdt, slot_fdt_offset, "image-prepare");

	fdt_for_each_subnode(processing_node, fdt, fdt_offset) {
		mod_off = 0;
		mod_len = 0;
		prop_mod_off = fdt_getprop(fdt, processing_node, "modify-offset", &proplen);

		if (proplen == sizeof(*prop_mod_off))
			mod_off = fdt64_to_cpu(*prop_mod_off);

		prop_mod_val  = fdt_getprop(fdt, processing_node, "modify-value", &proplen);
		if (proplen > 0)
			mod_len = proplen;

		prop_mod_mask = fdt_getprop(fdt, processing_node, "modify-mask", &proplen);
		if (proplen != mod_len)
			mod_len = 0;

		for (i = 0; i < mod_len; i++) {
			udata[mod_off + i] &= ~prop_mod_mask[i];
			udata[mod_off + i] |= prop_mod_val[i];
		}
	}

	ret = ioctl(dev->fd, FPGA_IMAGE_LOAD_WRITE, &fw);
	if (ret) {
		ret = errno;
		goto err_ioctl_write;
	}

	prev_progress = FPGA_IMAGE_PROG_IDLE;
	do {
		if (ioctl(dev->fd, FPGA_IMAGE_LOAD_STATUS, &fs)) {
			ret = errno;
			goto err_ioctl_status;
		}
		if (fs.err_code != 0) {
			ret = fs.err_code;
			goto err_ioctl_status;
		}
		if (flags & NFB_FW_LOAD_FLAG_VERBOSE) {
			if (prev_progress != fs.progress) {
				if (text) {
					nfb_fw_print_progress(text, 100);
					text = NULL;
				}
				prev_progress = fs.progress;

				if (fs.progress == FPGA_IMAGE_PROG_PREPARING)
					text = "Erasing Flash: %3d%%";
				else if (fs.progress == FPGA_IMAGE_PROG_WRITING)
					text = "Writing Flash: %3d%%";
				else if (fs.progress == FPGA_IMAGE_PROG_PROGRAMMING)
					text = "Staging Flash: %3d%%";
			}

			if (text) {
				pct = 0;
				if (fs.progress == FPGA_IMAGE_PROG_WRITING)
					pct = (size - fs.remaining_size) * 100 / size;
				nfb_fw_print_progress(text, pct);
			}
		}
		usleep(200000);
	} while (fs.progress != 0);

err_ioctl_status:
err_ioctl_write:
	close(efd);
	return ret;
}

int nfb_fw_load_boot_load(const struct nfb_device *dev, void *data, size_t size, int flags, int slot_fdt_offset, const char *filename)
{
	const int FDT_MAX_PATH_LENGTH = 512;

	int ret;
	int node_cp;
	struct nfb_boot_ioc_load load;
	const void *fdt = nfb_get_fdt(dev);

	char node_path[FDT_MAX_PATH_LENGTH];

	int32_t id = -1;
	uint32_t offset = 0xDEADBEEF;
	const void *prop;

	char *fn = NULL;

	ret = fdt_get_path(fdt, slot_fdt_offset, node_path, sizeof(node_path));
	if (ret < 0)
		return -EINVAL;

	//prop32 = fdt_getprop(fdt, node, "id", &proplen);
	fdt_getprop32(fdt, slot_fdt_offset, "id", &id);
	if (id == -1)
		return -EINVAL;
	prop = fdt_getprop(fdt, slot_fdt_offset, "empty", NULL);

	node_cp = fdt_subnode_offset(fdt, slot_fdt_offset, "control-param");
	fdt_getprop32(fdt, node_cp, "base", &offset);

	fn = strdup(filename ? filename : "cesnet-ndk-image.rbf");
	if (fn == NULL) {
		return -ENOMEM;
	}

	if (flags & NFB_FW_LOAD_FLAG_VERBOSE) {
		printf("Bitstream size: %lu B\n", size);
	}

	load.node = node_path;
	load.node_size = strlen(load.node) + 1;

	load.name = basename(fn);
	load.name_size = strlen(load.name) + 1;

	load.data = data;
	load.data_size = size;

	load.id = id;
	load.cmd = NFB_BOOT_IOC_LOAD_CMD_WRITE | (prop == NULL ? NFB_BOOT_IOC_LOAD_CMD_ERASE : 0);
	load.flags = NFB_BOOT_IOC_LOAD_FLAG_USE_NODE;

	ret = ioctl(dev->fd, NFB_BOOT_IOC_LOAD, &load);
	if (ret != 0)
		ret = -errno;

	free(fn);

	return ret;
}

int nfb_fw_load_ext_name(const struct nfb_device *dev, unsigned int image, void *data, size_t size, int flags, const char *filename)
{
	int ret;
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

	struct fpga_image_status fs;

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

	if (ioctl(dev->fd, FPGA_IMAGE_LOAD_STATUS, &fs) == 0) {
		if (errno != -ENXIO) {
			return nfb_fw_load_fpga_image_load(dev, data, size, flags, node);
		}
	}

	ret = nfb_fw_load_boot_load(dev, data, size, flags, node, filename);
	if (ret != -ENXIO)
		return ret;

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

int nfb_fw_load_ext(const struct nfb_device *dev, unsigned int image, void *data, size_t size, int flags)
{
	return nfb_fw_load_ext_name(dev, image, data, size, flags, NULL);
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
