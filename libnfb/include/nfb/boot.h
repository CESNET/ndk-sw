/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libnfb public header file - boot module
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef LIBNFB_BOOT_H
#define LIBNFB_BOOT_H

#include <linux/nfb/boot.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFB_FW_LOAD_FLAG_VERBOSE 0x01

struct nfb_device;

ssize_t        nfb_mtd_get_size(struct nfb_device *dev, int index);
ssize_t        nfb_mtd_get_erasesize(struct nfb_device *dev, int index);
int            nfb_mtd_read (struct nfb_device *dev, int index, size_t addr, void *data, size_t size);
int            nfb_mtd_write(struct nfb_device *dev, int index, size_t addr, void *data, size_t size);
int            nfb_mtd_erase(struct nfb_device *dev, int index, size_t addr, size_t size);

int nfb_fw_boot(const char *devname, unsigned int image);
int nfb_fw_load(const struct nfb_device *dev, unsigned int image, void *data, size_t size);
int nfb_fw_load_ext(const struct nfb_device *dev, unsigned int image, void *data, size_t size, int flags);
int nfb_fw_load_ext_name(const struct nfb_device *dev, unsigned int image, void *data, size_t size, int flags, const char *filename);

int nfb_fw_delete(const struct nfb_device *dev, unsigned int image);

ssize_t nfb_fw_read_for_dev(const struct nfb_device *dev, FILE *fd, void **data);

/* deprecated: nfb_fw_open */
ssize_t nfb_fw_open(const char *path, void **data);

/* deprecated: nfb_fw_read_bit */
ssize_t nfb_fw_read_bit(FILE *fp, void **data);

void nfb_fw_close(void *data);

void nfb_fw_print_slots(const struct nfb_device *dev);

void *nfb_fw_load_progress_init(struct nfb_device *dev);
void nfb_fw_load_progress_destroy(void *priv);
void nfb_fw_load_progress_print(void *priv);

int nfb_sensor_get(struct nfb_device *dev, struct nfb_boot_ioc_sensor *s);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBNFB_BOOT_H */
