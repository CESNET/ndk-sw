/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header for misc functionality of the NFB platform
 *
 * Copyright (C) 2017-2024 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NFB_MISC_H
#define NFB_MISC_H

int nfb_dtb_inject_init(struct pci_driver * nfb_driver);
void nfb_dtb_inject_exit(struct pci_driver * nfb_driver);
void * nfb_dtb_inject_get_pci(const char *pci_name);

#endif // NFB_MISC_H
