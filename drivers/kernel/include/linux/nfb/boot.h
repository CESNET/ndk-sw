/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * NFB driver Boot component public header
 *
 * Copyright (C) 2017-2022 CESNET
 *
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef _LINUX_NFB_BOOT_H_
#define _LINUX_NFB_BOOT_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct nfb_boot_ioc_mtd_info - argument for NFB_BOOT_IOC_MTD_INFO
 * @mtd:  W: Index of MTD on card. Typically is available only MTD with index 0.
 * @size: R: Total size of selected MTD.
 * @erasesize: R: Erase size of selected MTD.
 */
struct nfb_boot_ioc_mtd_info
{
        int mtd;
        int size;
        int erasesize;
};

/**
 * struct nfb_boot_ioc_mtd
 * @mtd:  W: Index of MTD on card.
 * @addr: W: Address passed to MTD.
 * @size: W: Size of data
 * @data: W: Valid pointer to data, which will be written or readen to.
 *           For NFB_BOOT_IOC_MTD_READ is filled with data readen from MTD.
 *           For NFB_BOOT_IOC_MTD_WRITE is content written to MTD.
 *           For NFB_BOOT_IOC_MTD_ERASE is not used.
 */
struct nfb_boot_ioc_mtd
{
        int mtd;
        int addr;
        int size;
        char *data;
};

/**
 * struct nfb_boot_ioc_sensor
 * @sensor_id:  W: Index of the requested sensor.
 * @flags:     WR: Additional parameter for sensor access. Currently not used.
 * @value:      R: Readen sensor value.
 */
struct nfb_boot_ioc_sensor {
	__u32 sensor_id;
	__u32 flags;
	long value;
};


#define NFB_BOOT_IOC_LOAD_CMD_NONE      0
#define NFB_BOOT_IOC_LOAD_CMD_ERASE     (1 << 0)
#define NFB_BOOT_IOC_LOAD_CMD_WRITE     (1 << 1)
#define NFB_BOOT_IOC_LOAD_CMD_PRIORITY  (1 << 2)

#define NFB_BOOT_IOC_LOAD_FLAG_USE_ID   (1 << 0)
#define NFB_BOOT_IOC_LOAD_FLAG_USE_NODE (1 << 1)

struct nfb_boot_ioc_load
{
	__u64 cmd; /* bitmask: erase, write */
	__u64 flags;
	__u64 id;
        __u64 data_size;

	__u32 node_size; /* \0 must be included */
	__u32 name_size; /* \0 must be included */
        char *data;
        char *node; /* device tree path to unit / image */
        const char *name; /* name of the image */
};


/*
 * Ioctl definitions
 */
#define NFB_BOOT_IOC                'b'
#define NFB_BOOT_IOC_RELOAD         _IOR(NFB_BOOT_IOC, 192, int)
#define NFB_BOOT_IOC_ERRORS_DISABLE _IO(NFB_BOOT_IOC, 193)

#define NFB_BOOT_IOC_MTD_INFO   _IOWR(NFB_BOOT_IOC, 1, struct nfb_boot_ioc_mtd_info)
#define NFB_BOOT_IOC_MTD_READ   _IOR (NFB_BOOT_IOC, 2, struct nfb_boot_ioc_mtd)
#define NFB_BOOT_IOC_MTD_WRITE  _IOW (NFB_BOOT_IOC, 3, struct nfb_boot_ioc_mtd)
#define NFB_BOOT_IOC_MTD_ERASE  _IOW (NFB_BOOT_IOC, 4, struct nfb_boot_ioc_mtd)

#define NFB_BOOT_IOC_SENSOR_READ _IOR(NFB_BOOT_IOC, 5, struct nfb_boot_ioc_sensor)

#define NFB_BOOT_IOC_LOAD       _IOWR(NFB_BOOT_IOC, 6, struct nfb_boot_ioc_load)


#endif /* _LINUX_NFB_BOOT_H_ */
