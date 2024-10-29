/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * QDR driver module of the NFB platform - header file
 *
 * Copyright (C) 2017-2022 CESNET
 * Author(s):
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#ifndef NFB_QDR_MOD_H
#define NFB_QDR_MOD_H

int nfb_qdr_attach(struct nfb_device *nfb, void **priv);
void nfb_qdr_detach(struct nfb_device *nfb, void *priv);

#endif /* NFB_QDR_MOD_H */
