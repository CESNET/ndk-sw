import sys
import time
import cython

from libc.stdlib cimport malloc
from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libcpp cimport bool

from cpython.exc cimport PyErr_SetFromErrno

import libnfb
cimport libnfb

cdef class RxMac:
    cdef nc_rxmac *_mac

    def __init__(self, libnfb.Nfb nfb, node):
        self._mac = NULL
        self._mac = nc_rxmac_open(nfb._dev, nfb._fdt_path_offset(node))
        if self._mac is NULL:
            PyErr_SetFromErrno(OSError)

    def __del__(self):
        if self._mac is not NULL:
            nc_rxmac_close(self._mac)

    @property
    def link(self):
        cdef nc_rxmac_status status
        assert nc_rxmac_read_status(self._mac, &status) == 0
        return True if status.link_up else False

    @property
    def enabled(self):
        cdef nc_rxmac_status status
        assert nc_rxmac_read_status(self._mac, &status) == 0
        return True if status.enabled else False

    @enabled.setter
    def enabled(self, enable):
        enabled = self.enabled
        if not enabled and enable:
            nc_rxmac_enable(self._mac)
        elif enabled and not enable:
            nc_rxmac_disable(self._mac)

    def enable(self, enable: bool = True):
        self.enabled = enable

    def disable(self):
        self.enabled = False

    def is_enabled(self):
        return self.enabled

    def stats_reset(self):
        """Reset statistic counters"""
        nc_rxmac_reset_counters(self._mac)

    def stats_read(self, etherstats: bool = False):
        """
        Read statistic counters

        :param etherstats: Request statistic values defined by RFC 2819

        :return: A dictionary with counters values
        """

        cdef nc_rxmac_counters counters
        cdef nc_rxmac_etherstats es
        nc_rxmac_counters_initialize(&counters, &es)
        cdef int ret = nc_rxmac_read_counters(self._mac, &counters, &es)
        assert ret == 0

        return {
            'octets': es.octets,
            'pkts': es.pkts,
            'broadcastPkts': es.broadcastPkts,
            'multicastPkts': es.multicastPkts,
            'CRCAlignErrors': es.CRCAlignErrors,
            'undersizePkts': es.undersizePkts,
            'oversizePkts': es.oversizePkts,
            'fragments': es.fragments,
            'jabbers': es.jabbers,
            'pkts64Octets': es.pkts64Octets,
            'pkts65to127Octets': es.pkts65to127Octets,
            'pkts128to255Octets': es.pkts128to255Octets,
            'pkts256to511Octets': es.pkts256to511Octets,
            'pkts512to1023Octets': es.pkts512to1023Octets,
            'pkts1024to1518Octets': es.pkts1024to1518Octets,
        } if etherstats else {
            'packets': counters.cnt_total,
            'received': counters.cnt_received,
            'octets': counters.cnt_octets,
            'discarded': counters.cnt_erroneous,
            'overflowed': counters.cnt_overflowed,
        }

cdef class TxMac:
    cdef nc_txmac *_mac

    def __init__(self, libnfb.Nfb nfb, node):
        self._mac = NULL
        self._mac = nc_txmac_open(nfb._dev, nfb._fdt_path_offset(node))
        if self._mac is NULL:
            PyErr_SetFromErrno(OSError)

    def __del__(self):
        if self._mac is not NULL:
            nc_txmac_close(self._mac)

    @property
    def enabled(self):
        cdef nc_txmac_status status
        assert nc_txmac_read_status(self._mac, &status) == 0
        return True if status.enabled else False

    @enabled.setter
    def enabled(self, enable):
        enabled = self.enabled
        if not enabled and enable:
            nc_txmac_enable(self._mac)
        elif enabled and not enable:
            nc_txmac_disable(self._mac)

    def enable(self, enable: bool = True):
        self.enabled = enable

    def disable(self):
        self.enabled = False

    def is_enabled(self):
        return self.enabled

    def stats_reset(self):
        """Reset statistic counters"""
        nc_txmac_reset_counters(self._mac)

    def stats_read(self):
        """
        Read statistic counters

        :return: A dictionary with counters values
        """

        cdef nc_txmac_counters counters
        ret = nc_txmac_read_counters(self._mac, &counters)
        assert ret == 0
        # INFO: this hides uninialized variables warning
        if ret: return {}

        return {
            'packets': counters.cnt_total,
            'octets': counters.cnt_octets,
            'discarded': counters.cnt_erroneous,
            'sent': counters.cnt_sent,
        }

cdef class Mdio:
    cdef nc_mdio *_mdio

    def __init__(self, libnfb.Nfb nfb, node, param_node = None):
        self._mdio = NULL
        self._mdio = nc_mdio_open(nfb._dev, nfb._fdt_path_offset(node), nfb._fdt_path_offset(param_node) if param_node else -1)
        if self._mdio is NULL:
            PyErr_SetFromErrno(OSError)

    def __del__(self):
        if self._mdio is not NULL:
            nc_mdio_close(self._mdio)

    def read(self, prtad: int, devad: int, reg: int):
        return nc_mdio_read(self._mdio, prtad, devad, reg)

    def write(self, prtad: int, devad: int, reg: int, val: int):
        nc_mdio_write(self._mdio, prtad, devad, reg, val)

cdef class I2c:
    cdef nc_i2c_ctrl *_ctrl

    def __init__(self, libnfb.Nfb nfb, node, addr = 0xA0):
        self._ctrl = NULL
        self._ctrl = nc_i2c_open(nfb._dev, nfb._fdt_path_offset(node))
        nc_i2c_set_addr(self._ctrl, addr)

    def __del__(self):
        if self._ctrl is not NULL:
            nc_i2c_close(self._ctrl)

    def read_reg(self, reg, size):
        cdef int cnt
        cdef unsigned char* c_data

        cnt = size
        c_data = <unsigned char *> malloc(cnt)
        if c_data == NULL:
            raise MemoryError()

        ret = nc_i2c_read_reg(self._ctrl, reg, c_data, size)
        return <bytes>c_data[:cnt]

    def write_reg(self, reg: int, data: bytes):
        cdef const uint8_t* c_data8

        c_data8 = data
        ret = nc_i2c_write_reg(self._ctrl, reg, c_data8, len(data))

cdef class DmaCtrlNdp:
    cdef nc_ndp_ctrl _ctrl
    cdef void *_ctrl_queue
    cdef nc_rxqueue *_ctrl_tx
    cdef const unsigned char[:] ub_view

    cdef dict __dict__

    def __init__(self, libnfb.Nfb nfb, node):
        cdef int ret
        self._ctrl_queue = NULL

        self.__nfb = nfb
        ret = nc_ndp_ctrl_open(nfb._dev, nfb._fdt_path_offset(node), &self._ctrl)
        if ret != 0:
            PyErr_SetFromErrno(OSError)

        if self._ctrl.dir == 0:
            self._ctrl_queue = nc_rxqueue_open(nfb._dev, nfb._fdt_path_offset(node))
        else: #if self._ctrl.dir == 1:
            self._ctrl_queue = nc_txqueue_open(nfb._dev, nfb._fdt_path_offset(node))

        if self._ctrl_queue == NULL != 0:
            nc_ndp_ctrl_close(&self._ctrl)
            self._ctrl.comp = NULL
            PyErr_SetFromErrno(OSError)

    def __del__(self):
        if self._ctrl.comp is not NULL:
            if self._ctrl_queue:
                if self._ctrl.dir == 0:
                    nc_rxqueue_close(<nc_rxqueue*> self._ctrl_queue)
                else:
                    nc_txqueue_close(<nc_txqueue*> self._ctrl_queue)
                self._ctrl_queue = NULL
            nc_ndp_ctrl_close(&self._ctrl)

    @property
    def mdp(self):
        return self._ctrl.mdp

    @property
    def mhp(self):
        return self._ctrl.mhp

    @property
    def last_upper_addr(self):
        return self._ctrl.last_upper_addr

    @last_upper_addr.setter
    def last_upper_addr(self, val):
        self._ctrl.last_upper_addr = val

    @property
    def sdp(self):
        return self._ctrl.sdp

    @sdp.setter
    def sdp(self, val):
        self._ctrl.sdp = val & self._ctrl.mdp

    @property
    def shp(self):
        return self._ctrl.shp

    @shp.setter
    def shp(self, val):
        self._ctrl.shp = val & self._ctrl.mhp

    @property
    def hdp(self):
        return self._ctrl.hdp

    @property
    def hhp(self):
        return self._ctrl.hhp

    @property
    def mtu(self):
        cdef uint32_t imin
        cdef uint32_t imax

        ret = nc_ndp_ctrl_get_mtu(&self._ctrl, &imin, &imax)
        if ret:
            raise ValueError()
        return (imin, imax)

    cdef __desc2uint(self, nc_ndp_desc desc):
        cdef uint64_t i
        cdef nc_ndp_desc *d = (<nc_ndp_desc*> &i)
        d[0] = desc

        return int(i)

    def desc0(self, phys: dma_addr_t) -> int:
        return self.__desc2uint(nc_ndp_rx_desc0(phys))

    def desc2(self, phys: dma_addr_t, length: int, meta: int=0, next: bool=False, hdr_length: int=0) -> int:
        if self._ctrl.dir == 0:
            return self.__desc2uint(nc_ndp_rx_desc2(phys, length, 1 if next else 0))
        else:
            return self.__desc2uint(nc_ndp_tx_desc2(phys, length, ((meta & 0xF) << 8) | (hdr_length & 0xFF), 1 if next else 0))

    def start(self, desc_buffer: dma_addr_t, hdr_buffer: dma_addr_t, update_buffer: dma_addr_t,
              update_buffer_p: memoryview,
              nb_desc: uint32_t, nb_hdr: uint32_t):
        cdef nc_ndp_ctrl_start_params sp
        cdef uint32_t* ub_ptr
        self.ub_view = update_buffer_p
        ub_ptr = <uint32_t*>&self.ub_view[0]

        sp.desc_buffer = desc_buffer
        sp.hdr_buffer = hdr_buffer
        sp.update_buffer = update_buffer
        sp.update_buffer_virt = ub_ptr
        sp.nb_desc = nb_desc
        sp.nb_hdr = nb_hdr

        self._ctrl.shp = 0
        self._ctrl.sdp = 0

        ret = nc_ndp_ctrl_start(&self._ctrl, &sp)
        if ret:
            PyErr_SetFromErrno(OSError)

    def flush_sp(self):
        nc_ndp_ctrl_sp_flush(&self._ctrl)

    def flush_sdp(self):
        nc_ndp_ctrl_sdp_flush(&self._ctrl)

    def stop(self, force = False):
        if force:
            nc_ndp_ctrl_stop_force(&self._ctrl)
        else:
            nc_ndp_ctrl_stop(&self._ctrl)

    def update_hdp(self):
        nc_ndp_ctrl_hdp_update(&self._ctrl)
        return self._ctrl.hdp

    def update_hhp(self):
        if self._ctrl.dir != 0:
            raise NotImplementedError()
        nc_ndp_ctrl_hhp_update(&self._ctrl)
        return self._ctrl.hhp

    def read_stats(self):
        cdef nc_rxqueue_counters rxc
        cdef nc_txqueue_counters txc
        ret = {}
        if self._ctrl.dir == 0:
            nc_rxqueue_read_counters(<nc_rxqueue*> self._ctrl_queue, &rxc)
            ret['received'] = rxc.received
            ret['discarded'] = rxc.discarded
            if rxc.have_bytes:
                ret['received_bytes'] = txc.received_bytes
                ret['discarded_bytes'] = txc.discarded_bytes
        else:
            nc_txqueue_read_counters(<nc_txqueue*> self._ctrl_queue, &txc)
            ret['sent'] = txc.sent
            if txc.have_bytes:
                ret['sent_bytes'] = txc.sent
        return ret
