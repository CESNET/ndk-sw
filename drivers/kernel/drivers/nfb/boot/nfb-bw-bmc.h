/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Boot driver module header for BittWare BMC
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

int nfb_boot_bw_bmc_attach(struct nfb_boot* boot);
void nfb_boot_bw_bmc_detach(struct nfb_boot* boot);
int nfb_boot_bw_bmc_load(struct nfb_boot *nfb_boot,
		struct nfb_boot_ioc_load __user *_load/*,
		struct nfb_boot_app_priv * app_priv*/);
int nfb_boot_bw_bmc_reload(struct nfb_boot *boot);
