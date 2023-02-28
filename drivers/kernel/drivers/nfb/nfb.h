/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Main driver private header of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_H
#define NFB_H

#include <asm/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include <linux/nfb/nfb.h>

#include <config.h>

#include "pci.h"
#include "fdt.h"


/* INFO: Synchronize this poswith nfb_register_embedded_drivers! */
#define NFB_DRIVER_NDP        2

#define NFB_DRIVERS_EARLY     2
#define NFB_DRIVERS_MAX       16

#define NFB_CARD_COUNT_MAX    8

#define MAX_FDT_PATH_LENGTH 512

enum nfb_device_status {NFB_DEVICE_STATUS_INIT, NFB_DEVICE_STATUS_OK, NFB_DEVICE_STATUS_RELEASE};
enum nfb_driver_status {NFB_DRIVER_STATUS_NONE, NFB_DRIVER_STATUS_OK, NFB_DRIVER_STATUS_ERROR};

typedef int (*nfb_driver_ops_attach_t)(struct nfb_device *nfb, void **priv);

struct nfb_driver_ops {
	nfb_driver_ops_attach_t attach;
	void (*detach)(struct nfb_device *nfb, void *priv);
	int (*open)(void *priv, void **app_priv, struct file *file);
	void (*release)(void *priv, void *app_priv, struct file *file);
	long (*ioctl)(void *priv, void *app_priv, struct file *file, unsigned int cmd, unsigned long arg);
	void (*numvfs_change)(void *priv, int numvfs);
	unsigned int ioc_type;
};

struct nfb_driver {
	struct list_head list;
	enum nfb_driver_status status;
	void *priv;
};

struct nfb_char_mmap_mod {
	struct list_head list;
	unsigned long size;
	unsigned long offset;
	int (*mmap)(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv);
	void *priv;
};

struct nfb_bus;
struct nfb_device;

typedef ssize_t (*nfb_bus_read_t)(struct nfb_bus *bus, void *buf, size_t nbyte, off_t offset);
typedef ssize_t (*nfb_bus_write_t)(struct nfb_bus *bus, const void *buf, size_t nbyte, off_t offset);

typedef int (*nfb_char_callback_t)(void *argp);

struct nfb_bus {
	nfb_bus_read_t read;
	nfb_bus_write_t write;
	struct list_head bus_list;
	void *priv;

	int access; 	// bitmask: Direct, R, W, bits?
	int type;	// ENUM

	char path[MAX_FDT_PATH_LENGTH];
};

struct nfb_comp {
	struct nfb_bus *bus;
	struct nfb_device *nfb;
	char *path;
	size_t offset;
	size_t size;
};

struct nfb_app {
	void *fdt;
	struct nfb_device* nfb;
	void *driver_private[NFB_DRIVERS_MAX];
};

struct nfb_lock_item {
	struct list_head list;
	struct nfb_app *app;
	char *path;
	int features;
};

/*
 * Top-level structure describing a NFB device
 */
struct nfb_device {
	struct pci_dev *pci;                   /* Associated PCI device (master) */
	int minor;                             /* Minor number assigned to this device (used for X in /dev/nfbX) */
	uint64_t serial;                       /* Card serial number */
	uint64_t dsn;                          /* FPGA chip unique identifier */
	enum nfb_device_status status;

	const struct nfb_pci_dev *nfb_pci_dev; /* Card-type specific data (driver-defined */
	struct device *dev;                    /* Linux generic 'device' (related to /sys files) */
	rwlock_t fdt_lock;                     /* Lock for DeviceTree modification */
	void  *fdt;                            /* DeviceTree description */
	atomic_t openers;                      /* Number of device openers */

	struct mutex list_lock;
	struct list_head list_mmap;
	struct nfb_driver list_drivers[NFB_DRIVERS_MAX];

	struct list_head buses;                /* List of buses (e.g. MI32) */
	nfb_char_callback_t char_lr_cb;        /* Release-time callback (used for reloading the card) */
	void *char_lr_data;                    /* Data for release-time callback */

	struct nfb_app kernel_app;

	struct list_head pci_devices;          /* Associated PCI device (master+slaves); struct nfb_pci_device type */

	struct mutex lock_mutex;
	struct list_head lock_list;
};

#define NFB_IS_SILICOM(nfb) ((nfb)->pci->vendor == 0x1c2c)
#define NFB_IS_TIVOLI(nfb) (NFB_IS_SILICOM(nfb) && ((nfb)->pci->device == 0x00d2 || (nfb)->pci->device == 0x00d3))

void *nfb_get_priv_for_attach_fn(struct nfb_device *nfb, nfb_driver_ops_attach_t attach);

void nfb_bus_register(struct nfb_device *nfb, struct nfb_bus *bus);
void nfb_bus_unregister(struct nfb_device *nfb, struct nfb_bus *bus);

#define nfb_get_fdt(dev) ((dev)->fdt)
int nfb_comp_count(const struct nfb_device *dev, const char *compatible);
int nfb_comp_find(const struct nfb_device *dev, const char *compatible, unsigned index);

struct nfb_comp *nfb_comp_open(const struct nfb_device *nfb, int fdtoffset);
struct nfb_comp *nfb_comp_open_ext(const struct nfb_device *nfb, int fdtoffset, size_t user_size);
void nfb_comp_close(struct nfb_comp *comp);

const char *nfb_comp_path(struct nfb_comp *comp);

#define nfb_user_to_comp(ptr) (((struct nfb_comp *) ptr) - 1)
#define nfb_comp_to_user(ptr) (((struct nfb_comp *) ptr) + 1)

int nfb_comp_lock(struct nfb_comp *comp, uint32_t features);
void nfb_comp_unlock(struct nfb_comp *comp, uint32_t features);

static inline ssize_t nfb_comp_read(struct nfb_comp *comp, void *buf, size_t nbyte, off_t offset)
{
	if (offset + nbyte > comp->size)
		return -1;
	return comp->bus->read(comp->bus, buf, nbyte, comp->offset + offset);
}
static inline ssize_t nfb_comp_write(struct nfb_comp *comp, const void *buf, size_t nbyte, off_t offset)
{
	if (offset + nbyte > comp->size)
		return -1;
	return comp->bus->write(comp->bus, buf, nbyte, comp->offset + offset);
}

#define __nfb_comp_write(bits) \
static inline void nfb_comp_write##bits(struct nfb_comp *comp, off_t offset, uint##bits##_t val) \
{ \
	nfb_comp_write(comp, &val, sizeof(val), offset); \
}
#define __nfb_comp_read(bits) \
static inline uint##bits##_t nfb_comp_read##bits(struct nfb_comp *comp, off_t offset) \
{ \
	uint##bits##_t val = 0; \
	nfb_comp_read(comp, &val, sizeof(val), offset); \
	return val; \
}

__nfb_comp_write(8)
__nfb_comp_write(16)
__nfb_comp_write(32)
__nfb_comp_write(64)
__nfb_comp_read(8)
__nfb_comp_read(16)
__nfb_comp_read(32)
__nfb_comp_read(64)

int nfb_driver_register(struct nfb_driver_ops ops);
void nfb_driver_unregister(struct nfb_driver_ops ops);

int nfb_pci_init(void);
void nfb_pci_exit(void);

struct nfb_device *nfb_create(void);
int nfb_probe(struct nfb_device *nfb);
void nfb_remove(struct nfb_device *nfb);
void nfb_destroy(struct nfb_device *nfb);

void nfb_fdt_init(struct nfb_device *nfb);
int nfb_char_create(struct nfb_device *nfb);
int nfb_char_probe(struct nfb_device *nfb);
void nfb_char_remove(struct nfb_device* nfb);

int nfb_char_init(void);
void nfb_char_exit(void);

int nfb_char_register_mmap(struct nfb_device* nfb, size_t size, size_t *offset, int (*mmap)(struct vm_area_struct *vma, unsigned long offset, unsigned long size, void *priv), void *priv);
int nfb_char_unregister_mmap(struct nfb_device* nfb, size_t offset);

int nfb_char_set_lr_callback(struct nfb_device *nfb, nfb_char_callback_t cb,
		void *argp);

int nfb_lock_probe(struct nfb_device *nfb);
int nfb_lock_remove(struct nfb_device *nfb);
int nfb_lock_open(struct nfb_device *nfb, struct nfb_app *app);
void nfb_lock_release(struct nfb_device *nfb, struct nfb_app *app);
long nfb_lock_ioctl(struct nfb_device *nfb, struct nfb_app *app, unsigned int cmd, unsigned long arg);

int nfb_lock_try_lock(struct nfb_device *nfb, struct nfb_app *app, struct nfb_lock lock);
int nfb_lock_unlock(struct nfb_device *nfb, struct nfb_app *app, struct nfb_lock lock);

int nfb_net_set_dev_addr(struct nfb_device *nfb, struct net_device *dev, int index);

#endif // NFB_H
