import sys
import time
import cython

from libc.stdlib cimport malloc
from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t, int32_t
from libcpp cimport bool

from cpython.exc cimport PyErr_SetFromErrno

import libnfb
cimport libnfb

from libnfb cimport NfbDeviceHandle, nfb_comp_open, nfb_comp_close


cdef class RxMac:
    cdef NfbDeviceHandle _handle
    cdef nc_rxmac *_mac

    def __init__(self, libnfb.Nfb nfb, node):
        self._handle = nfb._handle
        self._handle.check_handle()
        self._mac = NULL
        self._mac = nc_rxmac_open(self._handle._dev, nfb._fdt_path_offset(node))
        if self._mac is NULL:
            PyErr_SetFromErrno(OSError)

        self._handle.add_close_cb(self._close_handle)

    def __del__(self):
        self._close_handle()

    def _close_handle(self):
        if self._mac is not NULL:
            nc_rxmac_close(self._mac)
            self._mac = NULL

    def _check_handle(self):
        if self._mac is NULL:
            raise ReferenceError

    @property
    def link(self):
        self._check_handle()
        return False if nc_rxmac_get_link(self._mac) == 0 else True

    @property
    def enabled(self):
        cdef nc_rxmac_status status
        self._check_handle()
        assert nc_rxmac_read_status(self._mac, &status) == 0
        return True if status.enabled else False

    @enabled.setter
    def enabled(self, enable):
        enabled = self.enabled
        self._check_handle()
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

    def is_link(self):
        return self.link

    def stats_reset(self):
        self.reset_stats()

    def reset_stats(self):
        """Reset statistic counters"""
        self._check_handle()
        nc_rxmac_reset_counters(self._mac)

    def stats_read(self, etherstats: bool = False):
        return self.read_stats(etherstats)

    def read_stats(self, etherstats: bool = False):
        """
        Read statistic counters

        :param etherstats: Request statistic values defined by RFC 2819

        :return: A dictionary with counters values
        """

        cdef nc_rxmac_counters counters
        cdef nc_rxmac_etherstats es
        self._check_handle()
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
            'total': counters.cnt_total,
            #'total_bytes': counters.cnt_total_octets,
            'passed': counters.cnt_received,
            'passed_bytes': counters.cnt_octets,
            'dropped': (counters.cnt_erroneous + counters.cnt_overflowed),
                'overflowed': counters.cnt_overflowed,
                'errors': counters.cnt_erroneous,
                # 'crc_errors': counters.cnt_crc_errors,
                # 'length_errors': counters.cnt_len_errors,
                # 'filtered': counters.cnt_filtered,
        }


cdef class TxMac:
    cdef NfbDeviceHandle _handle
    cdef nc_txmac *_mac

    def __init__(self, libnfb.Nfb nfb, node):
        self._handle = nfb._handle
        self._handle.check_handle()
        self._mac = NULL
        self._mac = nc_txmac_open(self._handle._dev, nfb._fdt_path_offset(node))
        if self._mac is NULL:
            PyErr_SetFromErrno(OSError)

        self._handle.add_close_cb(self._close_handle)

    def __del__(self):
        self._close_handle()

    def _close_handle(self):
        if self._mac is not NULL:
            nc_txmac_close(self._mac)
            self._mac = NULL

    def _check_handle(self):
        if self._mac is NULL:
            raise ReferenceError

    @property
    def enabled(self):
        cdef nc_txmac_status status
        self._check_handle()
        assert nc_txmac_read_status(self._mac, &status) == 0
        return True if status.enabled else False

    @enabled.setter
    def enabled(self, enable):
        enabled = self.enabled
        self._check_handle()
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
        self.reset_stats()

    def reset_stats(self):
        """Reset statistic counters"""
        self._check_handle()
        nc_txmac_reset_counters(self._mac)

    def stats_read(self):
        return self.read_stats()

    def read_stats(self):
        """
        Read statistic counters

        :return: A dictionary with counters values
        """

        cdef nc_txmac_counters counters
        self._check_handle()
        ret = nc_txmac_read_counters(self._mac, &counters)
        assert ret == 0
        # INFO: this hides uninialized variables warning
        if ret: return {}

        return {
            'total': counters.cnt_total,
            'total_bytes': counters.cnt_octets,
            'passed': counters.cnt_sent,
            'dropped': counters.cnt_erroneous,
                'errors': counters.cnt_erroneous,
        }


cdef class Mdio:
    cdef NfbDeviceHandle _handle
    cdef nc_mdio *_mdio
    cdef bool _inited

    def __init__(self, libnfb.Nfb nfb, node, param_node = None):
        self._handle = nfb._handle
        self._handle.check_handle()
        self._mdio = NULL
        self._mdio = nc_mdio_open_no_init(self._handle._dev, nfb._fdt_path_offset(node), nfb._fdt_path_offset(param_node) if param_node else -1)
        self._inited = False
        if self._mdio is NULL:
            PyErr_SetFromErrno(OSError)

        self._handle.add_close_cb(self._close_handle)

    def __del__(self):
        self._close_handle()

    def _close_handle(self):
        if self._mdio is not NULL:
            nc_mdio_close(self._mdio)
            self._mdio = NULL

    def _check_handle(self):
        if self._mdio is NULL:
            raise ReferenceError

    def _init(self):
        self._check_handle()
        if not self._inited:
            self._inited = True
            nc_mdio_init(self._mdio)

    def read(self, devad: int, reg: int, prtad: int=0):
        self._init()
        return nc_mdio_read(self._mdio, prtad, devad, reg)

    def write(self, devad: int, reg: int, val: int, prtad: int=0):
        self._init()
        nc_mdio_write(self._mdio, prtad, devad, reg, val)


cdef class I2c:
    cdef NfbDeviceHandle _handle
    cdef nc_i2c_ctrl *_ctrl

    def __init__(self, libnfb.Nfb nfb, node, addr = 0xA0):
        self._handle = nfb._handle
        self._handle.check_handle()
        self._ctrl = NULL
        self._ctrl = nc_i2c_open(self._handle._dev, nfb._fdt_path_offset(node))
        if self._ctrl == NULL:
            PyErr_SetFromErrno(OSError)
        nc_i2c_set_addr(self._ctrl, addr)

        self._handle.add_close_cb(self._close_handle)

    def __del__(self):
        self._close_handle()

    def _close_handle(self):
        if self._ctrl is not NULL:
            nc_i2c_close(self._ctrl)
            self._ctrl = NULL

    def _check_handle(self):
        if self._ctrl is NULL:
            raise ReferenceError

    def read_reg(self, reg, size):
        cdef int cnt
        cdef unsigned char* c_data

        self._check_handle()

        cnt = size
        c_data = <unsigned char *> malloc(cnt)
        if c_data == NULL:
            raise MemoryError()

        ret = nc_i2c_read_reg(self._ctrl, reg, c_data, size)
        return <bytes>c_data[:cnt]

    def write_reg(self, reg: int, data: bytes):
        cdef const uint8_t* c_data8

        self._check_handle()

        c_data8 = data
        ret = nc_i2c_write_reg(self._ctrl, reg, c_data8, len(data))


cdef class DmaCtrlNdp:
    cdef NfbDeviceHandle _handle
    cdef nc_ndp_ctrl _ctrl
    cdef void *_ctrl_queue
    cdef nc_rxqueue *_ctrl_tx
    cdef const unsigned char[:] ub_view

    cdef dict __dict__

    def __init__(self, libnfb.Nfb nfb, node):
        cdef int ret
        cdef nfb_device *dev

        self._ctrl_queue = NULL

        self._handle = nfb._handle
        self._handle.check_handle()
        dev = self._handle._dev
        ret = nc_ndp_ctrl_open(dev, nfb._fdt_path_offset(node), &self._ctrl)
        if ret != 0:
            PyErr_SetFromErrno(OSError)

        if self._ctrl.dir == 0:
            self._ctrl_queue = nc_rxqueue_open(dev, nfb._fdt_path_offset(node))
        else: #if self._ctrl.dir == 1:
            self._ctrl_queue = nc_txqueue_open(dev, nfb._fdt_path_offset(node))

        if self._ctrl_queue == NULL:
            nc_ndp_ctrl_close(&self._ctrl)
            self._ctrl.comp = NULL
            PyErr_SetFromErrno(OSError)

        self._handle.add_close_cb(self._close_handle)

    def __del__(self):
        self._close_handle()

    def _close_handle(self):
        if self._ctrl.comp is not NULL:
            if self._ctrl_queue:
                if self._ctrl.dir == 0:
                    nc_rxqueue_close(<nc_rxqueue*> self._ctrl_queue)
                else:
                    nc_txqueue_close(<nc_txqueue*> self._ctrl_queue)
                self._ctrl_queue = NULL
            nc_ndp_ctrl_close(&self._ctrl)
            #self._ctrl.comp = NULL  # already done by nc_ndp_ctrl_close

    def _check_handle(self):
        if self._ctrl.comp is NULL:
            raise ReferenceError

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

        self._check_handle()
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

        sp.data_buffer = 0
        sp.desc_buffer = desc_buffer
        sp.hdr_buffer = hdr_buffer
        sp.update_buffer = update_buffer
        sp.update_buffer_virt = ub_ptr
        sp.nb_desc = nb_desc
        sp.nb_hdr = nb_hdr
        sp.nb_data = 0

        self._ctrl.shp = 0
        self._ctrl.sdp = 0

        self._check_handle()
        ret = nc_ndp_ctrl_start(&self._ctrl, &sp)
        if ret:
            PyErr_SetFromErrno(OSError)

    def flush_sp(self):
        self._check_handle()
        nc_ndp_ctrl_sp_flush(&self._ctrl)

    def flush_sdp(self):
        self._check_handle()
        nc_ndp_ctrl_sdp_flush(&self._ctrl)

    def stop(self, force = False):
        self._check_handle()
        if force:
            nc_ndp_ctrl_stop_force(&self._ctrl)
        else:
            nc_ndp_ctrl_stop(&self._ctrl)

    def update_hdp(self):
        self._check_handle()
        nc_ndp_ctrl_hdp_update(&self._ctrl)
        return self._ctrl.hdp

    def update_hhp(self):
        self._check_handle()
        if self._ctrl.dir != 0:
            raise NotImplementedError()
        nc_ndp_ctrl_hhp_update(&self._ctrl)
        return self._ctrl.hhp

    def read_stats(self):
        cdef nc_rxqueue_counters rxc
        cdef nc_txqueue_counters txc
        self._check_handle()
        ret = {}
        if self._ctrl.dir == 0:
            nc_rxqueue_read_counters(<nc_rxqueue*> self._ctrl_queue, &rxc)
            ret['passed'] = rxc.received
            ret['dropped'] = rxc.discarded
            if rxc.have_bytes:
                ret['passed_bytes'] = txc.received_bytes
                ret['dropped_bytes'] = txc.discarded_bytes
        else:
            nc_txqueue_read_counters(<nc_txqueue*> self._ctrl_queue, &txc)
            ret['passed'] = txc.sent
            if txc.have_bytes:
                ret['passed_bytes'] = txc.sent
        return ret


cdef class Transceiver:
    cdef NfbDeviceHandle _handle
    cdef nfb_comp *_comp_status
    cdef dict __dict__

    """
    Object representing a transceiver interface

    :ivar I2c i2c: I2c bus handle (only with QSFP+ modules)
    """
    def __init__(self, libnfb.Nfb nfb, node):
        self._comp_status = NULL

        self._nfb = nfb
        self._handle = nfb._handle
        self._handle.check_handle()
        status = nfb.fdt_get_phandle(node.get_property('status-reg').value)
        fdt_offset = self._nfb._fdt_path_offset(status)
        self._comp_status = nfb_comp_open(self._handle._dev, fdt_offset)
        if self._comp_status is NULL:
            PyErr_SetFromErrno(OSError)

        self._handle.add_close_cb(self._close_handle)

        ctrl = nfb.fdt_get_phandle(node.get_property('control').value)
        node_ctrl_param = node.get_subnode('control-param')

        # FIXME
        if "QSFPP":
            prop = node_ctrl_param.get_property("i2c-addr")
            i2c_addr = prop.value if prop else 0xA0
            self.i2c = I2c(nfb, ctrl, i2c_addr)

    def __del__(self):
        self._close_handle()

    def _close_handle(self):
        if self._comp_status is not NULL:
            nfb_comp_close(self._comp_status)
            self._comp_status = NULL

    def _check_handle(self):
        if self._comp_status is NULL:
            raise ReferenceError

    def is_present(self) -> bool:
        self._check_handle()
        return nc_transceiver_statusreg_is_present(self._comp_status) != 0

    # FIXME: only valid for specific QSFP+
    @property
    def vendor_name(self) -> str:
        "vendor of the transceiver"
        return self.i2c.read_reg(148, 16).decode().strip()
    @property
    def vendor_pn(self) -> str:
        "product number of the transceiver"
        return self.i2c.read_reg(168, 16).decode().strip()
    @property
    def vendor_sn(self) -> str:
        "serial number of the transceiver"
        return self.i2c.read_reg(196, 16).decode().strip()

    def read_vendor_name(self) -> str:
        "vendor of the transceiver"
        return self.vendor_name

    def read_vendor_pn(self) -> str:
        "product number of the transceiver"
        return self.vendor_pn

    def read_vendor_sn(self) -> str:
        "serial number of the transceiver"
        return self.vendor_sn
