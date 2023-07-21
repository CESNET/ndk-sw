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
