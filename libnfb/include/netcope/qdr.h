/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Network component library - QDR controller
 *
 * Copyright (C) 2018-2022 CESNET
 * Author(s):
 *   Jan Kucera <jan.kucera@cesnet.cz>
 */

#ifndef NETCOPE_QDR_H
#define NETCOPE_QDR_H

#ifdef __cplusplus
extern "C" {
#endif


/* ~~~~[ DATA TYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
struct nc_qdr {
    int _unused;
};


/* ~~~~[ PROTOTYPES ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_qdr *nc_qdr_open(struct nfb_device *dev, int fdt_offset);
static inline struct nc_qdr *nc_qdr_open_index(struct nfb_device *dev, unsigned index);
static inline void           nc_qdr_close(struct nc_qdr *qdr);
static inline void           nc_qdr_start(struct nc_qdr *qdr);
static inline void           nc_qdr_reset(struct nc_qdr *qdr);
static inline void           nc_qdr_test(struct nc_qdr *qdr);


/* ~~~~[ REGISTERS ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define QDR_REG_MODE        0x0000
#define QDR_REG_STATUS      0x0004
#define QDR_REG_CONTROL     0x0004
#define QDR_REG_DIFFLO      0x0008
#define QDR_REG_DIFFHI      0x000C

#define QDR_REG_STATUS_CALIB    0x0004
#define QDR_REG_STATUS_TEST     0x0100

enum nc_qdr_cmds {
    QDR_CMD_START = 0x00000001,
    QDR_CMD_RESET = 0x00000010,
    QDR_CMD_TEST  = 0x00000002,
};

#define COMP_NETCOPE_QDR "netcope,qdr"


/* ~~~~[ IMPLEMENTATION ]~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
static inline struct nc_qdr *nc_qdr_open(struct nfb_device *dev, int fdt_offset)
{
    struct nc_qdr *qdr;
    struct nfb_comp *comp;

    if (fdt_node_check_compatible(nfb_get_fdt(dev), fdt_offset, COMP_NETCOPE_QDR))
        return NULL;

    comp = nfb_comp_open_ext(dev, fdt_offset, sizeof(struct nc_qdr));
    if (!comp)
        return NULL;

    qdr = (struct nc_qdr *) nfb_comp_to_user(comp);

    return qdr;
}

static inline struct nc_qdr *nc_qdr_open_index(struct nfb_device *dev, unsigned index)
{
    int fdt_offset = nfb_comp_find(dev, COMP_NETCOPE_QDR, index);
    return nc_qdr_open(dev, fdt_offset);
}

static inline void nc_qdr_close(struct nc_qdr *qdr)
{
    nfb_comp_close(nfb_user_to_comp(qdr));
}

static inline void nc_qdr_start(struct nc_qdr *qdr)
{
    nfb_comp_write32(nfb_user_to_comp(qdr), QDR_REG_CONTROL, QDR_CMD_START);
}

static inline void nc_qdr_reset(struct nc_qdr *qdr)
{
    nfb_comp_write32(nfb_user_to_comp(qdr), QDR_REG_CONTROL, QDR_CMD_RESET);
}

static inline void nc_qdr_test(struct nc_qdr *qdr)
{
    nfb_comp_write32(nfb_user_to_comp(qdr), QDR_REG_CONTROL, QDR_CMD_TEST);
}

static inline int nc_qdr_get_calib(struct nc_qdr *qdr)
{
    uint32_t val = nfb_comp_read32(nfb_user_to_comp(qdr), QDR_REG_STATUS);
    return (val & QDR_REG_STATUS_CALIB) ? 1 : 0;
}

static inline int nc_qdr_get_test(struct nc_qdr *qdr)
{
    uint32_t val = nfb_comp_read32(nfb_user_to_comp(qdr), QDR_REG_STATUS);
    return !(val & QDR_REG_STATUS_TEST) ? 1 : 0;
}

static inline int nc_qdr_get_ready(struct nc_qdr *qdr)
{
    uint32_t val = nfb_comp_read32(nfb_user_to_comp(qdr), QDR_REG_STATUS);
    return (val & QDR_REG_STATUS_CALIB && !(val & QDR_REG_STATUS_TEST)) ? 1 : 0;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NETCOPE_QDR_H */
