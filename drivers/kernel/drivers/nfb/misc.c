/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Misc functions of the NFB platform
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/bug.h>
#include <linux/crc32.h>

#include "nfb.h"
#include "misc.h"

#define NFB_FDT_BURSTSIZE (16384)

uint64_t reverse(const uint64_t n, const uint64_t k)
{
        uint64_t i, r = 0;
        for (i = 0; i < k; ++i)
                r |= ((n >> i) & 1) << (k - i - 1);
        return r;
}

int nfb_net_set_dev_addr(struct nfb_device *nfb, struct net_device *dev, int index)
{
	int snshift = 4;
	u8 addr[ETH_ALEN];

	if (dev->addr_len != ETH_ALEN)
		return -1;

	addr[0] = 0x00;
	addr[1] = 0x11;
	addr[2] = 0x17;
	addr[3] = 0;
	if (nfb->nfb_pci_dev)
		addr[3] = reverse(nfb->nfb_pci_dev->card_type_id, 8);
	addr[4] = (nfb->serial << snshift) >> 8;
	addr[5] = ((nfb->serial << snshift) & 0xF0) | (index & 0x0F);
#ifdef CONFIG_HAVE_ETH_HW_ADDR_SET
	eth_hw_addr_set(dev, addr);
#else
	memcpy(dev->dev_addr, addr, sizeof(addr));
#endif

	return 0;
}

struct dtb_inject {
	char busname[32];
	char busaddr[32];
	size_t len;
	size_t off;
	unsigned long crc32;
	void * fdt;
};

static struct dtb_inject * dtb_inject_temp = NULL;
static struct dtb_inject * dtb_inject_valid = NULL;
static DEFINE_SPINLOCK(dtb_inject_lock);

void * nfb_dtb_inject_get_pci(const char *pci_dev)
{
	void * fdt = NULL;
	struct dtb_inject* dtb;
	size_t size;

	dtb = xchg_acquire(&dtb_inject_valid, NULL);
	if (dtb == NULL)
		goto err_no_valid_dtb;

	if (strcmp(dtb->busname, "pci") || strcmp(dtb->busaddr, pci_dev)) {
		/* The bus or the busaddr doesn't match, try return the dtb */
		if (cmpxchg(&dtb_inject_valid, NULL, dtb)) {
			kfree(dtb->fdt);
			kfree(dtb);
		}
		goto err_busaddr_not_match;
	}

	/* Resize FDT to be editable */
	size = max_t(size_t, fdt_totalsize(dtb->fdt), NFB_FDT_BURSTSIZE) * 4;

	fdt = krealloc(dtb->fdt, size, GFP_KERNEL);
	if (fdt == NULL)
		goto err_realloc;

	fdt_set_totalsize(fdt, size);

	/* dtb->fdt is freed by the caller */
	kfree(dtb);
	pr_info("nfb: using injected fdt on device %s\n", pci_dev);
	return fdt;

err_realloc:
	kfree(dtb->fdt);
	kfree(dtb);
err_busaddr_not_match:
err_no_valid_dtb:
	return NULL;
}

static ssize_t dtb_inject_meta_store(struct device_driver *driver, const char * buffer, size_t length)
{
	char *tmp_buf;
	ssize_t ret = -EINVAL;
	unsigned long value = 0;
	struct dtb_inject* dtb;

	tmp_buf = kmalloc(length+1, GFP_KERNEL);
	if (tmp_buf == NULL)
		return -ENOMEM;

	memcpy(tmp_buf, buffer, length);
	tmp_buf[length] = '\0';

	spin_lock(&dtb_inject_lock);

	/* invalidate and free already valid dtb_inject */
	dtb = xchg_acquire(&dtb_inject_valid, NULL);
	if (dtb) {
		kfree(dtb->fdt);
		kfree(dtb);
	}

	dtb = dtb_inject_temp;
	if (dtb == NULL) {
		dtb = dtb_inject_temp = kzalloc(sizeof(*dtb), GFP_KERNEL);
		if (dtb == NULL) {
			ret = -ENOMEM;
			goto err_dtballoc;
		}
	} else if (dtb->fdt) {
		kfree(dtb->fdt);
		dtb->fdt = NULL;
	}
	dtb->off = 0;
	dtb->len = 0;
	dtb->busname[0] = '\0';
	dtb->busaddr[0] = '\0';

	sscanf(tmp_buf, "len=%lu crc32=%lu busname=%31s busaddr=%31s", &value,
			&dtb->crc32, dtb->busname, dtb->busaddr);
	if (value == 0 || value > 1048576 ||
			strlen(dtb->busname) <= 0 ||
			strlen(dtb->busaddr) <= 0) {
		ret = -EINVAL;
		goto err_value;
	}

	dtb->fdt = kzalloc(value, GFP_KERNEL);
	if (dtb->fdt == NULL) {
		ret = -ENOMEM;
		goto err_value;
	}

	dtb->len = value;
	spin_unlock(&dtb_inject_lock);
	kfree(tmp_buf);
	return length;

err_value:
err_dtballoc:
	spin_unlock(&dtb_inject_lock);
	kfree(tmp_buf);
	return ret;
}

static ssize_t dtb_inject_meta_show(struct device_driver *driver, char * buffer)
{
	int ret = 0;
	struct dtb_inject* dtb;

	spin_lock(&dtb_inject_lock);
	dtb = dtb_inject_temp;
	if (dtb)
		ret = scnprintf(buffer, PAGE_SIZE,
				"len=%lu crc32=%lum busname=%s busaddr=%s\n",
				dtb->len, dtb->crc32, dtb->busname, dtb->busaddr);
	spin_unlock(&dtb_inject_lock);
	return ret;
}

static ssize_t dtb_inject_store(struct device_driver *file, const char * buffer, size_t length)
{
	uint32_t csum;
	ssize_t ret = -ENOMEM;
	struct dtb_inject* dtb;

	spin_lock(&dtb_inject_lock);
	dtb = dtb_inject_temp;

	if (dtb == NULL)
		goto err_no_dtb;

	if (dtb->len < dtb->off + length)
		length = dtb->len - dtb->off;
	if (length == 0)
		goto err_zero_len;

	memcpy(dtb->fdt + dtb->off, buffer, length);
	dtb->off += length;

	if (dtb->off == dtb->len) {
		/* The DTB upload is here complete */
		csum = crc32(0x80000000 ^ 0xffffffff, dtb->fdt, dtb->len);
		if ((csum ^ 0xffffffff) != dtb->crc32)
			goto err_fdt;

		if (dtb->len < sizeof(struct fdt_header) || fdt_check_header(dtb->fdt))
			goto err_fdt;

		if (dtb->len < fdt_totalsize(dtb->fdt))
			goto err_fdt;

		if (fdt_path_offset(dtb->fdt, "/firmware/") < 0 ||
				fdt_path_offset(dtb->fdt, "/system/") >= 0 ||
				fdt_path_offset(dtb->fdt, "/board/") >= 0 ||
				fdt_path_offset(dtb->fdt, "/drivers/") >= 0) {
			ret = -EBADF;
			goto err_fdt;
		}

		/* Mark dtb_inject as valid DTB */
		dtb = xchg_acquire(&dtb_inject_valid, dtb);
		if (dtb) {
			/* FDT in dtb_inject_valid must be always valid */
			kfree(dtb->fdt);
			kfree(dtb);
		}

		/* dtb was moved from dtb_inject_temp to dtb_inject_valid */
		dtb_inject_temp = NULL;
	}
	spin_unlock(&dtb_inject_lock);
	pr_info("nfb: fdt injected sucessfully, waiting for device\n");
	return length;

err_fdt:
	pr_warn("nfb: error while checking injected fdt\n");
err_zero_len:
err_no_dtb:
	spin_unlock(&dtb_inject_lock);
	return ret;
}

static DRIVER_ATTR_WO(dtb_inject);
static DRIVER_ATTR_RW(dtb_inject_meta);

int nfb_dtb_inject_init(struct pci_driver * nfb_driver)
{
	int ret;

	ret = driver_create_file(&nfb_driver->driver, &driver_attr_dtb_inject);
	if (ret)
		goto err_create_inject;

	ret = driver_create_file(&nfb_driver->driver, &driver_attr_dtb_inject_meta);
	if (ret)
		goto err_create_inject_len;

	return 0;

//	driver_remove_file(&nfb_driver->driver, &driver_attr_dtb_inject_len);
err_create_inject_len:
	driver_remove_file(&nfb_driver->driver, &driver_attr_dtb_inject);
err_create_inject:
	return ret;

}

void nfb_dtb_inject_exit(struct pci_driver * nfb_driver)
{
	struct dtb_inject* dtb;
	driver_remove_file(&nfb_driver->driver, &driver_attr_dtb_inject_meta);
	driver_remove_file(&nfb_driver->driver, &driver_attr_dtb_inject);

	/* reset / consume valid dtb_inject */
	dtb = xchg_acquire(&dtb_inject_valid, NULL);
	if (dtb) {
		kfree(dtb->fdt);
		kfree(dtb);
	}

	dtb = dtb_inject_temp;
	if (dtb) {
		if (dtb->fdt)
			kfree(dtb->fdt);
		kfree(dtb);
	}
}
