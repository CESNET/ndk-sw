/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - transceiver functions
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Martin Spinler <spinler@cesnet.cz>
 */

#ifndef NETCOPE_TRANSCEIVER_H
#define NETCOPE_TRANSCEIVER_H

#ifdef __cplusplus
extern "C" {
#endif


/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline int          nc_transceiver_statusreg_is_present(struct nfb_comp *comp);

/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline int          nc_transceiver_statusreg_is_present(struct nfb_comp *comp)
{
	return !(nfb_comp_read8(comp, 0) & (1 << 4));
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_TRANSCEIVER_H */
