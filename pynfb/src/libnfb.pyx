import sys
import time
import cython

from itertools import islice
from typing import Union, Optional, List, Tuple

from libc.stdlib cimport malloc
from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from libcpp cimport bool

from cpython.ref cimport PyObject
from cpython.exc cimport PyErr_SetFromErrno

import fdt

cimport libnetcope

if not hasattr(time, 'time_ns'):
    time_ns = lambda: int(time.time() * 1000000000)
else:
    time_ns = time.time_ns


def __batched(iterable, n):
    it = iter(iterable)
    batch = tuple(islice(it, n))
    while batch:
        yield batch
        batch = tuple(islice(it, n))


cdef class Nfb:
    """
    Nfb class instance represents handle to NFB device in system

    It allows to create ``Comp`` object instance for access to the component registers inside the NFB device.
    It also allows to access to the object representation of the Flattened Device Tree by the ``fdt`` attribute and some helper functions.
    """

    def __cinit__(self, path: str):
        self._dev = nfb_open(path.encode())
        if self._dev is NULL:
            PyErr_SetFromErrno(OSError)

        self._fdt = nfb_get_fdt(self._dev)
        self._fdtc = <const char*>self._fdt
        self._dtb = <bytes>self._fdtc[:c_fdt_totalsize(self._fdt)]
        self.fdt = fdt.parse_dtb(self._dtb)

        self.ndp = QueueManager(self)

    def __dealloc__(self):
        if self._dev is not NULL:
            nfb_close(self._dev)

    def comp_open(self, comp: Union[str, fdt.items.Node], index: cython.int = 0) -> Comp:
        """
        Create component handle from a component instance inside the NFB

        :param comp: Exact node representing the component from the Nfb.fdt or compatible string, which will be searched and matched in properties of Nfb.fdt nodes
        :param index: Index of component with matching compatible string
        :return: Component object handle
        """

        cdef int fdt_offset
        if isinstance(comp, str):
            fdt_offset = nfb_comp_find(self._dev, comp.encode(), index)
        elif isinstance(comp, fdt.items.Node):
            fdt_offset = self._fdt_path_offset(comp)
        else:
            raise ValueError()
        return Comp(self, fdt_offset)

    def _fdt_path_offset(self, node):
        return fdt_path_offset(self._fdt, (node.path + '/' + node.name + '/').encode())

    def fdt_get_compatible(self, compatible: str) -> List[fdt.items.Node]:
        """
        Get list of all Nfb.fdt nodes for specific compatible string

        :param compatible: Compatible string, which will be searched and matched in properties of Nfb.fdt nodes
        :returns: List of matched fdt nodes
        """

        l = []
        walk = list(self.fdt.walk())
        for path, nodes, props in walk:
            for prop in props:
                if prop.name == 'compatible' and prop.value == compatible:
                    l.append(self.fdt.get_node(path))
        return list(reversed(l))

    def fdt_get_phandle(self, phandle: int) -> Optional[fdt.items.Node]:
        """
        Get the Nfb.fdt node with equal phandle property value as requested

        :param phandle: phandle number
        :return: Matched fdt node
        """

        walk = list(self.fdt.walk())
        for path, nodes, props in walk:
            for prop in props:
                if prop.name == 'phandle' and prop.value == phandle:
                    return self.fdt.get_node(path)
        return None


cdef class Comp:
    cdef nfb_comp* _comp
    cdef Nfb _nfb_dev

    def __cinit__(self, Nfb nfb_dev, int fdt_offset):
        self._nfb_dev = nfb_dev
        self._comp = nfb_comp_open(self._nfb_dev._dev, fdt_offset)
        if self._comp is NULL:
            PyErr_SetFromErrno(OSError)

    def __dealloc__(self):
        if self._comp is not NULL:
            nfb_comp_close(self._comp)

    def read(self, addr: int, count: int):
        cdef unsigned char *b = <unsigned char *> malloc(count)
        cdef ret = nfb_comp_read(self._comp, b, count, addr)
        assert ret == count
        return <bytes>b[:count]

    def write(self, addr: int, data: bytes):
        cdef const char* b = data
        cdef ret = nfb_comp_write(self._comp, b, len(data), addr)
        assert ret == len(data)
        return None

    def write8 (self, addr: int, data: int): self.write(addr, data.to_bytes(1, sys.byteorder))
    def write16(self, addr: int, data: int): self.write(addr, data.to_bytes(2, sys.byteorder))
    def write32(self, addr: int, data: int): self.write(addr, data.to_bytes(4, sys.byteorder))
    def write64(self, addr: int, data: int): self.write(addr, data.to_bytes(8, sys.byteorder))
    def read8 (self, addr: int): return int.from_bytes(self.read(addr, 1), sys.byteorder)
    def read16(self, addr: int): return int.from_bytes(self.read(addr, 2), sys.byteorder)
    def read32(self, addr: int): return int.from_bytes(self.read(addr, 4), sys.byteorder)
    def read64(self, addr: int): return int.from_bytes(self.read(addr, 8), sys.byteorder)

    def set_bit(self, addr, bit, value=True, width=32):
        """
        Set bit in component register

        The register is first readen and if the value of requested bit differs, the new value is written into register as a whole word of specified width.

        :param addr: Address of the register in component space
        :param bit: Bit number in register
        :param value: Set or clear bit
        :param width: The bit width of accessed register; supported values are 8, 16, 32, 64
        """

        reg = getattr(self, f"read{width}")(addr)
        val = (reg | (1 << bit)) if value else (reg & ~(1 << bit))
        if val != reg:
            getattr(self, f"write{width}")(addr, val)

    def clr_bit(self, addr, bit, width=32):
        """
        Clear bit in component register

        The register is first readen and if the value of requested bit differs, the new value is written into register as a whole word of specified width.

        :param addr: Address of the register in component space
        :param bit: Bit number in register
        :param width: The bit width of accessed register; supported values are 8, 16, 32, 64
        """
        self.set_bit(addr, bit, value=False, width=width)

    def get_bit(self, addr, bit, width=32):
        """
        Get value of bit in component register

        :param addr: Address of the register in component space
        :param bit: Bit number in register
        :param width: The bit width of accessed register; supported values are 8, 16, 32, 64
        """
        reg = getattr(self, f"read{width}")(addr)
        return True if (reg & (1 << bit)) else False


class QueueManager:
    #cdef dict __dict__

    def __init__(self, nfb):
        compatibles = ([
            (QueueNdpRx, "netcope,dma_ctrl_ndp_rx", 0),
            (QueueNdpRx, "netcope,dma_ctrl_sze_rx", 0),
        ], [
            (QueueNdpTx, "netcope,dma_ctrl_ndp_tx", 1),
            (QueueNdpTx, "netcope,dma_ctrl_sze_tx", 1),
        ])

        self.rx, self.tx = (
            [queue(nfb, node, i)
                for queue, compatible, buf_off in d
                    for i, node in enumerate(nfb.fdt_get_compatible(compatible))]
            for d in compatibles
        )

    def _get_q(self, q, tx = False):
        if q == None:
            q = [i for i in range(len(self.tx if tx else self.rx))]
        elif isinstance(q, int):
            q = [q]
        else:
            q = [i for i in q if i in range(len(self.tx if tx else self.rx))]
        return q

    def send(self, pkts: Union[bytes, List[bytes]], hdrs: Optional[Union[bytes, List[bytes]]] = None, flags: Optional[Union[int, List[int]]] = None, flush: bool = True, i: Optional[Union[int, List[int]]] = None) -> None:
        for qi in self._get_q(i, True):
            self.tx[qi].send(pkts, hdrs, flags, flush)

    def sendmsg(self, pkts: List[Tuple[bytes, bytes, int]], flush: bool = True, i: Optional[Union[int, List[int]]] = None) -> None:
        for qi in self._get_q(i, True):
            self.tx[qi].sendmsg(pkts, flush)

    def flush(self, i = None):
        for qi in self._get_q(i, True):
            self.tx[qi].flush()

    def recv(self, cnt: int = -1, timeout = 0, i: Optional[Union[int, List[int]]] = None):
        return [(pkt, qi) for (pkt, _, _), qi in self.recvmsg(cnt, timeout, i)]

    def recvmsg(self, cnt: int = -1, timeout = 0, i: Optional[Union[int, List[int]]] = None):
        cdef list p
        cdef list pkts
        cdef long long int to
        cdef int count

        if timeout is not None:
            timeout = int(timeout * 1000000000)
        qs = self._get_q(i)

        to = 0
        pkts = []

        while cnt > 0 or cnt == -1:
            xcnt = 0
            for i in qs:
                p = self.rx[i].recvmsg(cnt, 0)
                if p:
                    count = len(p)
                    pkts.extend(zip(p, [i]*count))
                    if cnt != -1:
                        cnt -= count
                    xcnt += count
            if timeout is not None:
                if xcnt == 0:
                    # FIXME: overflow?
                    if to == 0:
                        to = time_ns() + timeout
                    elif to < time_ns():
                        return pkts
                else:
                    to = 0
        return pkts


cdef class QueueNdp:
    cdef ndp_queue *_q
    cdef nfb_device *_dev
    cdef dict __dict__
    cdef Nfb _nfb
    cdef int _index
    cdef bool _running

    def __init__(self, nfb: Nfb, node, index: int):
        self._nfb = nfb
        self._dev = nfb._dev
        self._index = index
        self._q = NULL
        self._running = False
        self._node = node

    cdef _check_running(self):
        if not self._running:
            if self._q is NULL:
                p1, p2 = self._nfb._dev, self._index
                self._q = ndp_open_tx_queue(p1, p2) if self._dir else ndp_open_rx_queue(p1, p2)
                if self._q is NULL:
                    PyErr_SetFromErrno(OSError)

            assert ndp_queue_start(self._q) == 0
            self._running = True

    def start(self):
        self._check_running()

    def stop(self):
        if self._running:
            assert ndp_queue_stop(self._q) == 0
            self._running = False

    def stats_read(self):
        raise NotImplementedError()

    def stats_reset(self):
        raise NotImplementedError()


cdef class QueueNdpRx(QueueNdp):
    cdef libnetcope.nc_rxqueue *_nc_queue

    def __init__(self, nfb: Nfb, node, index):
        self._dir = 0
        QueueNdp.__init__(self, nfb, node, index)
        self._nc_queue = libnetcope.nc_rxqueue_open(nfb._dev, nfb._fdt_path_offset(node))

    def stats_reset(self):
        """Reset statistic counters"""
        libnetcope.nc_rxqueue_reset_counters(self._nc_queue)

    def stats_read(self):
        """
        Read statistic counters

        :return: Dictionary with counters values
        """
        cdef libnetcope.nc_rxqueue_counters counters

        stats = {}
        libnetcope.nc_rxqueue_read_counters(self._nc_queue, &counters)
        for i, name in enumerate(['received', 'received_bytes', 'discarded', 'discarded_bytes']):
            stats[name] = getattr(counters, name)
        return stats

    cdef _recvmsg(self, cnt: int = -1, timeout: int = 0):
        cdef int l_pkt
        cdef int l_hdr
        cdef int icnt
        cdef unsigned char* c_pkt
        cdef unsigned char* c_hdr
        cdef ndp_packet ndppkt[64]
        cdef list pkts
        cdef long long int to

        self._check_running()

        to = 0
        pkts = []

        while cnt > 0 or cnt == -1:
            burst_cnt = 64 if cnt > 64 or cnt == -1 else cnt
            icnt = ndp_rx_burst_get(self._q, ndppkt, burst_cnt)
            if icnt == 0:
                if timeout != None:
                    if to == 0:
                        to = time_ns() + timeout
                    # FIXME: overflow?
                    elif to < time_ns():
                        break
            else:
                to = 0

                for i in range(icnt):
                    # FIXME: check if zero-length malloc will be freed after conversion to bytes
                    l_pkt = ndppkt[i].data_length
                    c_pkt = <unsigned char *> malloc(l_pkt)
                    if c_pkt == NULL:
                        raise MemoryError()
                    memcpy(c_pkt, ndppkt[i].data, l_pkt)

                    l_hdr = ndppkt[i].data_length
                    c_hdr = <unsigned char *> malloc(l_hdr)
                    if c_hdr == NULL:
                        raise MemoryError()
                    memcpy(c_hdr, ndppkt[i].data, l_hdr)

                    pkts.append((<bytes>c_pkt[:l_pkt], <bytes>c_hdr[:l_hdr], 0))

                if cnt != -1:
                    cnt -= icnt
                ndp_rx_burst_put(self._q)

        return pkts

    def recv(self, cnt: int = -1, timeout = 0):
        return [pkt for pkt, _, _ in self._recvmsg(cnt, int(timeout * 1000000000) if timeout is not None else None)]

    def recvmsg(self, cnt: int = -1, timeout: int = 0) -> List[Tuple[bytes, bytes, int]]:
        return self._recvmsg(cnt, int(timeout * 1000000000) if timeout is not None else None)


cdef class QueueNdpTx(QueueNdp):
    cdef libnetcope.nc_txqueue *_nc_queue

    def __init__(self, nfb: Nfb, node, index):
        self._dir = 1
        QueueNdp.__init__(self, nfb, node, index)
        self._nc_queue = libnetcope.nc_txqueue_open(nfb._dev, nfb._fdt_path_offset(node))

    def stats_reset(self):
        """Reset statistic counters"""
        libnetcope.nc_txqueue_reset_counters(self._nc_queue)

    def stats_read(self):
        """
        Read statistic counters

        :return: Dictionary with counters values
        """
        cdef libnetcope.nc_txqueue_counters counters

        stats = {}
        libnetcope.nc_txqueue_read_counters(self._nc_queue, &counters)
        for i, name in enumerate(['sent', 'sent_bytes']):
            stats[name] = getattr(counters, name)

        return stats

    cdef _sendmsg(self, pkts: List[Tuple[bytes, bytes, int]]): # -> None:
        cdef int cnt
        cdef int icnt
        cdef ndp_packet ndppkt[64]
        cdef const char* c_pkt
        cdef const char* c_hdr

        for burst in __batched(pkts, 64):
            in_cnt = len(burst)
            for i in range(in_cnt):
                pkt, hdr, flags = burst[i]
                ndppkt[i].header_length = len(hdr)
                ndppkt[i].data_length = len(pkt)

            cnt = 0
            while cnt != in_cnt:
                cnt = ndp_tx_burst_get(self._q, ndppkt, in_cnt)

            for i in range(in_cnt):
                pkt, hdr, flags = burst[i]
                c_pkt, c_hdr = pkt, hdr
                memcpy(ndppkt[i].data, c_pkt, len(pkt))
                memcpy(ndppkt[i].header, c_hdr, len(hdr))
            ndp_tx_burst_put(self._q)

    def send(self, pkts: Union[bytes, List[bytes]], hdrs: Optional[Union[bytes, List[bytes]]] = None, flags: Optional[Union[int, List[int]]] = None, flush: bool = True) -> None:
        """
        Send a packet or burst of packets

        :param pkts: Packets represented by list of data bytes or a single packet
        :param hdrs: Packet headers represented by data bytes or a single packet header, which will be used for all packets
        :param flags: Metadata represented by list of integers or a single integer, which will be used for all packets; value meaning is TBD; specific for particular NDP type
        :param flush: Send data immediatelly (default is True)
        """

        if isinstance(pkts, bytes):
            pkts = [pkts]

        if isinstance(hdrs, bytes):
            hdrs = [hdrs] * len(pkts)
        elif hdrs is None:
            hdrs = [bytes()] * len(pkts)

        if isinstance(flags, int):
            flags = [flags] * len(pkts)
        elif flags is None:
            flags = [0] * len(pkts)

        self.sendmsg(zip(pkts, hdrs, flags), flush)

    def sendmsg(self, pkts: List[Tuple[bytes, bytes, int]], flush: bool = True) -> None:
        """
        Send burst of messages

        :param pkts: List of messages represented by tuple: (packet data, header data, flags)
        :param flush: Send data immediatelly
        """

        self._check_running()

        if isinstance(pkts, tuple):
            pkts = [pkts]

        self._sendmsg(pkts)

        if flush:
            ndp_tx_burst_flush(self._q)

    def flush(self):
        """Flush the prepared packet/messages"""
        ndp_tx_burst_flush(self._q)
