/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network interface driver of the NFB platform - core
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#include <libfdt.h>

#include "../nfb.h"

#include "net.h"

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>

#include <netcope/eth.h>


static bool net_enable = 0;
static bool net_tsu_init = 0;

static inline uint64_t ns_to_64b_fr(uint64_t ns)
{
	return ns * 18446744073llu;
}

static inline uint64_t fr_64b_to_ns(uint64_t fr)
{
	return fr / 18446744073llu;
}

int nfb_tsu_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct nfb_net *net = container_of(ptp, struct nfb_net, ptp_info);
	uint64_t inc_prev, inc;
	const int64_t freq = net->tsu_freq;	/* must be signed */
	int64_t offset;

	offset = scaled_ppm * (4294967ll * 65536 / 1000) / freq;
	inc = ((uint64_t)-1) / freq + offset;
	inc_prev = nc_tsu_get_inc(net->ptp_tsu_comp);
	nc_tsu_set_inc(net->ptp_tsu_comp, inc);

//	printk("TSU adjfine %ld | mid offset: %lld |INC_REG: %llx <- %llx (change: %+lld)\n", scaled_ppm, offset, inc, inc_prev, inc- inc_prev);
	return 0;
}
int nfb_tsu_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	return -EOPNOTSUPP;
}

int nfb_tsu_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct nfb_net *net = container_of(ptp, struct nfb_net, ptp_info);
	struct nc_tsu_time rtr = nc_tsu_get_rtr(net->ptp_tsu_comp);

	/* delta is in ns */
	int64_t ns = delta % 1000000000;
	rtr.sec += delta / 1000000000;
	rtr.fraction += ns_to_64b_fr(ns);
	nc_tsu_set_rtr(net->ptp_tsu_comp, rtr);
//	printk("TSU adjtime: %lld (sec: %d, nsec :%d)\n", delta, delta / 1000000000l, delta % 1000000000l);
	return 0;
}

int nfb_tsu_gettimex64(struct ptp_clock_info *ptp, struct timespec64 *ts,
		struct ptp_system_timestamp *sts)
{
	struct nfb_net *net = container_of(ptp, struct nfb_net, ptp_info);

	#if 0
	struct nc_tsu_time rtr;

	ptp_read_system_prets(sts);
	rtr = nc_tsu_get_rtr(net->ptp_tsu_comp);
	ptp_read_system_postts(sts);
	ts->tv_sec = rtr.sec;
	ts->tv_nsec = fr_64b_to_ns(rtr.fraction);
	#else
	uint64_t ns_fr;

	ptp_read_system_prets(sts);
	nfb_comp_write32(nfb_user_to_comp(net->ptp_tsu_comp), TSU_REG_CONTROL, TSU_CMD_READ_RT);
	ns_fr         = nfb_comp_read32(nfb_user_to_comp(net->ptp_tsu_comp), TSU_REG_MI_DATA_LOW);
	ptp_read_system_postts(sts);

        ts->tv_sec    = nfb_comp_read32(nfb_user_to_comp(net->ptp_tsu_comp), TSU_REG_MI_DATA_HIGH);
	#if 0
	ts->tv_nsec   = nfb_comp_read32(nfb_user_to_comp(net->ptp_tsu_comp), TSU_REG_MI_DATA_MIDDLE);
	ts->tv_nsec <<= 32;
	ts->tv_nsec  |= nfb_comp_read32(nfb_user_to_comp(net->ptp_tsu_comp), TSU_REG_MI_DATA_LOW);
	#else
	ns_fr        |= ((uint64_t) nfb_comp_read32(nfb_user_to_comp(net->ptp_tsu_comp), TSU_REG_MI_DATA_MIDDLE)) << 32;
	ts->tv_nsec = fr_64b_to_ns(ns_fr);
	#endif
	#endif

//	printk("PTP TSU gettime, %lld, tv_nsec %lld \n", ts->tv_sec, ts->tv_nsec);
	return 0;
}

int nfb_tsu_settime64(struct ptp_clock_info *p, const struct timespec64 *ts)
{
	struct nfb_net *net = container_of(p, struct nfb_net, ptp_info);
	struct nc_tsu_time rtr;

	rtr.sec = ts->tv_sec;
	rtr.fraction = ns_to_64b_fr(ts->tv_nsec);
	nc_tsu_set_rtr(net->ptp_tsu_comp, rtr);
//	printk("PTP TSU settime: %lld . %lld\n", ts->tv_sec, ts->tv_nsec);
	return 0;
}
int nfb_tsu_enable(struct ptp_clock_info *ptp,
	      struct ptp_clock_request *request, int on)
{
	printk("PTP TSU enable: not supported\n");
	return -EOPNOTSUPP;
}
int nfb_tsu_verify(struct ptp_clock_info *ptp, unsigned int pin,
	      enum ptp_pin_function func, unsigned int chan)
{
	printk("PTP TSU verify: not supported\n");
	return -EOPNOTSUPP;
}

int nfb_net_attach(struct nfb_device *nfbdev, void **priv)
{
	struct nfb_net *module;
	struct nfb_net_device *device;

	unsigned rxqc, txqc;

	int fdt_offset;
	int ret = 0;
	int index;

	*priv = NULL;
	if (!net_enable) {
		return 0;
	}

	rxqc = 0;
	fdt_offset = fdt_path_offset(nfbdev->fdt, "/drivers/ndp/rx_queues");
	fdt_for_each_subnode(fdt_offset, nfbdev->fdt, fdt_offset) rxqc++;
	if (rxqc == 0) {
		dev_info(&nfbdev->pci->dev, "nfb_net: No RX queues available!\n");
		return 0;
	}

	txqc = 0;
	fdt_offset = fdt_path_offset(nfbdev->fdt, "/drivers/ndp/tx_queues");
	fdt_for_each_subnode(fdt_offset, nfbdev->fdt, fdt_offset) txqc++;
	if (txqc == 0) {
		dev_info(&nfbdev->pci->dev, "nfb_net: No TX queues available!\n");
		return 0;
	}

	*priv = module = kzalloc(sizeof(*module), GFP_KERNEL);
	if (!module) {
		ret = -ENOMEM;
		goto err_kmalloc;
	}

	INIT_LIST_HEAD(&module->list_devices);

	module->rxqc = rxqc;
	module->txqc = txqc;
	module->nfbdev = nfbdev;
	memset(&module->dev, 0, sizeof(struct device));

	device_initialize(&module->dev);
	module->dev.parent = nfbdev->dev;
	dev_set_name(&module->dev, "net");
	dev_set_drvdata(&module->dev, module);
	ret = device_add(&module->dev);
	if (ret) {
	 	goto err_device_add;
	}

	index = 0;
	fdt_for_each_compatible_node(nfbdev->fdt, fdt_offset, COMP_NETCOPE_ETH) {
		device = nfb_net_device_create(module, fdt_offset, index);
		if (device)
			list_add_tail(&device->list_item, &module->list_devices);
		index++;
	}

	dev_info(&nfbdev->pci->dev, "nfb_net: Attached successfully (%d ETH interfaces)\n", index);

#ifndef PTP_CLOCK_NAME_LEN
#define PTP_CLOCK_NAME_LEN 16
#endif
	snprintf(module->ptp_info.name, PTP_CLOCK_NAME_LEN, "nfb%d_tsu_ptp", nfbdev->minor);
	module->ptp_info.owner = THIS_MODULE;
	module->ptp_info.adjfine = nfb_tsu_adjfine;
	module->ptp_info.adjfreq = nfb_tsu_adjfreq;
	module->ptp_info.adjtime = nfb_tsu_adjtime;
	module->ptp_info.gettimex64 = nfb_tsu_gettimex64;
	module->ptp_info.settime64 = nfb_tsu_settime64;
	module->ptp_info.enable = nfb_tsu_enable;
	module->ptp_info.verify = nfb_tsu_verify;
	module->ptp_info.max_adj = 0;

	fdt_for_each_compatible_node(nfbdev->fdt, fdt_offset, COMP_NETCOPE_TSU) {
		module->ptp_tsu_comp = nc_tsu_open(nfbdev, fdt_offset);
		if (module->ptp_tsu_comp) {
			module->tsu_freq = nc_tsu_get_frequency(module->ptp_tsu_comp);
			module->ptp_info.max_adj = (((uint64_t)-1) / module->tsu_freq) / 64 / 2;
//			printk("TSU maxadj: %llx | %llx\n", module->ptp_info.max_adj, ((uint64_t)(module->ptp_info.max_adj * 65.536)));
//			printk("TSU maxadj: %lld | %lld\n", module->ptp_info.max_adj, ((uint64_t)(module->ptp_info.max_adj * 65.536)));
			break;
		}
	}

	module->ptp_clock = ERR_PTR(-ENODEV);

	if (module->ptp_tsu_comp && net_tsu_init) {
		struct nc_tsu_time rtr;
		uint64_t inc;

		rtr = nc_tsu_get_rtr(module->ptp_tsu_comp);
		inc = nc_tsu_get_inc(module->ptp_tsu_comp);

		if (rtr.sec == 0 || 1) {
			struct timespec64 ts;
			ktime_get_real_ts64(&ts);
			rtr.sec = ts.tv_sec;
			rtr.fraction = ns_to_64b_fr(ts.tv_nsec);
			nc_tsu_set_rtr(module->ptp_tsu_comp, rtr);
		}

		if (inc == 0 || 1) {
			inc = ((uint64_t)-1) / module->tsu_freq;
			nc_tsu_set_inc(module->ptp_tsu_comp, inc);
		}

//#define NFB_TSU_AUTOINIT
#ifdef NFB_TSU_AUTOINIT
		//nc_tsu_select_clk_source(module->ptp_tsu_comp);
		nc_tsu_enable(module->ptp_tsu_comp);
#endif
		module->ptp_clock = ptp_clock_register(&module->ptp_info, nfbdev->dev);
	}

	return ret;

err_device_add:
	kfree(module);
err_kmalloc:
	return ret;
}


void nfb_net_detach(struct nfb_device *nfb, void *priv)
{
	struct nfb_net *module = (struct nfb_net *) priv;
	struct nfb_net_device *device, *tmp;

	if (module == NULL) {
		return;
	}

	if (!IS_ERR(module->ptp_clock))
		ptp_clock_unregister(module->ptp_clock);

	if (module->ptp_tsu_comp) {
		nc_tsu_close(module->ptp_tsu_comp);
	}

	list_for_each_entry_safe(device, tmp, &module->list_devices, list_item) {
		nfb_net_device_destroy(device);
	}

	device_del(&module->dev);

	kfree(module);
}

module_param(net_enable, bool, S_IRUGO);
MODULE_PARM_DESC(net_enable, "Create netdevice for each Ethernet interface [no]");
module_param(net_tsu_init, bool, S_IRUGO);
MODULE_PARM_DESC(net_tsu_init, "Initialize the TSU component [no]");
