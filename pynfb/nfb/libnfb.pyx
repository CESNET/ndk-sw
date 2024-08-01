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

from . import eth

if not hasattr(time, 'time_ns'):
    time_ns = lambda: int(time.time() * 1000000000)
else:
    time_ns = time.time_ns


def _batched(iterable, n):
    it = iter(iterable)
    batch = tuple(islice(it, n))
    while batch:
        yield batch
        batch = tuple(islice(it, n))


def open(path: str = '0') -> Nfb:
    """Open a handle to NFB device in system

    :param path: Path to device node, default leads to ``/dev/nfb0``
    :return: The :class:`libnfb.Nfb` object, enhanced by :class:`eth.EthManager`
    """

    dev = Nfb(path)
    eth.EthManager(dev)
    return dev

cdef class NfbDeviceHandle:
    def __cinit__(self, path: str):
        self._dev = nfb_open(path.encode())
        if self._dev is NULL:
            PyErr_SetFromErrno(OSError)

    def __dealloc__(self):
        if self._dev is not NULL:
            nfb_close(self._dev)


cdef class Nfb:
    """
    Nfb class instance represents handle to NFB device in system

    It allows to create :class:`Comp` object instance for access to the component registers inside the NFB device.
    It also allows to access to the object representation of the Flattened Device Tree by the ``fdt`` attribute and some helper functions.

    :ivar fdt.FDT fdt: FDT object
    :ivar QueueManager ndp: NDP Queue manager object
    :ivar EthManager eth: Ethernet manager object. Exist only when using :func:`nfb.open`
    """

    default_device = '/dev/nfb0'

    def __init__(self, path: str):
        cdef const char* _fdtc

        self._handle = NfbDeviceHandle(path)
        # Deprecated. Use _handle._dev
        self._dev = self._handle._dev

        self._fdt = nfb_get_fdt(self._handle._dev)
        fdtc = <const char*>self._fdt
        dtb = <bytes>fdtc[:c_fdt_totalsize(self._fdt)]
        self.fdt = fdt.parse_dtb(dtb)

        self.ndp = QueueManager(self)

    def __dealloc__(self):
        pass

    def comp_open(self, comp: Union[str, fdt.items.Node], index: cython.int = 0) -> Comp:
        """
        Create component handle from a component instance inside the NFB

        :param comp: Exact node representing the component from the Nfb.fdt or compatible string, which will be searched and matched in properties of Nfb.fdt nodes
        :param index: Index of component with matching compatible string
        :return: Component object handle
        """

        cdef int fdt_offset
        if isinstance(comp, str):
            fdt_offset = nfb_comp_find(self._handle._dev, comp.encode(), index)
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
    """
    Comp class instance represents component handle inside NFB device.
    """

    cdef nfb_comp* _comp
    cdef NfbDeviceHandle _nfb_dev

    def __cinit__(self, Nfb nfb_dev, int fdt_offset):
        self._nfb_dev = nfb_dev._handle
        self._comp = nfb_comp_open(self._nfb_dev._dev, fdt_offset)
        if self._comp is NULL:
            PyErr_SetFromErrno(OSError)

    def __dealloc__(self):
        if self._comp is not NULL:
            nfb_comp_close(self._comp)

    def read(self, addr: int, count: int):
        """
        Read data from component register

        :param addr: Address of the register in component space
        :param count: Number of bytes to read
        """

        cdef unsigned char *b = <unsigned char *> malloc(count)
        cdef ret = nfb_comp_read(self._comp, b, count, addr)
        assert ret == count
        return <bytes>b[:count]

    def write(self, addr: int, data: bytes):
        """
        Write data to component register

        :param addr: Address of the register in component space
        :param data: Data bytes to be written
        """
        cdef const char* b = data
        cdef ret = nfb_comp_write(self._comp, b, len(data), addr)
        assert ret == len(data)
        return None

    def write8 (self, addr: int, data: int):
        """
        Write integer value to component register, use 8 bit access (1 byte)

        :param addr: Address of the register in component space
        :param data: Unsigned integer value to be written
        """
        self.write(addr, data.to_bytes(1, sys.byteorder))

    def write16(self, addr: int, data: int): self.write(addr, data.to_bytes(2, sys.byteorder))
    def write32(self, addr: int, data: int): self.write(addr, data.to_bytes(4, sys.byteorder))
    def write64(self, addr: int, data: int): self.write(addr, data.to_bytes(8, sys.byteorder))

    def read8 (self, addr: int):
        """
        Read integer value from component register, use 8 bit access (1 byte)

        :param addr: Address of the register in component space
        :return: Readen unsigned integer value
        """
        return int.from_bytes(self.read(addr, 1), sys.byteorder)

    def read16(self, addr: int): return int.from_bytes(self.read(addr, 2), sys.byteorder)
    def read32(self, addr: int): return int.from_bytes(self.read(addr, 4), sys.byteorder)
    def read64(self, addr: int): return int.from_bytes(self.read(addr, 8), sys.byteorder)

    #write64.__doc__ = """
    #    Write integer value to component register, use {0} bit access ({1} byte)

    #    :param addr: Address of the register in component space
    #    :param data: Unsigned integer value to be written
    #    """

    #read64.__doc__ = """
    #    Read integer value from component register, use {0} bit access ({1} byte)

    #    :param addr: Address of the register in component space
    #    :return: Readen unsigned integer value
    #    """

    #write8.__doc__  = write64.__doc__.format(8, 1)
    #write16.__doc__ = write64.__doc__.format(16, 2)
    #write32.__doc__ = write64.__doc__.format(32, 4)
    #write64.__doc__ = write64.__doc__.format(64, 8)

    #read8.__doc__  = read64.__doc__.format(8, 1)
    #read16.__doc__ = read64.__doc__.format(16, 2)
    #read32.__doc__ = read64.__doc__.format(32, 4)
    #read64.__doc__ = read64.__doc__.format(64, 8)

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

    def wait_for_bit(self, addr, bit, timeout=5.0, delay=0.01, level=True, width=32):
        """
        Wait for bit in comenent register

        :param addr: Address of the register in component space
        :param bit: Bit number in register
        :param timeout: Timeout after which will be the waiting terminated
        :param delay: Delay between each ask (ask = read of a given register)
        :param level: For what bit level is function waiting
        :param width: The bit width of accessed register; supported values are 8, 16, 32, 64
        :return: True if bit was set in time, else False
        """
        t = 0
        while self.get_bit(addr, bit, width) != level:
            t += delay
            if t >= timeout:
                return False
            else:
                time.sleep(delay)
        return True


cdef class AbstractBaseComp:
    """
    AbstractBaseComp represents common parent for all classes that manages HW components

    Derived class should set it's own `DT_COMPATIBLE`!

    :ivar libnfb.Nfb _dev: NFB object
    :ivar libnfb.Comp _node: fdt.Node object
    """

    DT_COMPATIBLE = None

    def __init__(self, dev=Nfb.default_device, node: Optional[fdt.Node]=None, index: int=0):
        assert self.DT_COMPATIBLE is not None, "DT_COMPATIBLE must be set in derived class"

        self._dev = dev if isinstance(dev, Nfb) else open(dev)

        if node:
            assert self.DT_COMPATIBLE == node.get_property("compatible").value, "compatible string mismatch"
        else:
            node = self._dev.fdt_get_compatible(self.DT_COMPATIBLE)[index]

        self._node = node


cdef class QueueManager:
    """
    :ivar List[NdpQueueRx] rx: List of RX NDP queues
    :ivar List[NdpQueueTx] tx: List of TX NDP queues
    """

    cdef dict __dict__

    def __init__(self, nfb):
        compatibles = ([
            (NdpQueueRx, "cesnet,dma_ctrl_calypte_rx", 0),
            (NdpQueueRx, "netcope,dma_ctrl_ndp_rx", 0),
            (NdpQueueRx, "netcope,dma_ctrl_sze_rx", 0),
        ], [
            (NdpQueueTx, "cesnet,dma_ctrl_calypte_tx", 1),
            (NdpQueueTx, "netcope,dma_ctrl_ndp_tx", 1),
            (NdpQueueTx, "netcope,dma_ctrl_sze_tx", 1),
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

    def start(self, i: Optional[Union[int, List[int]]] = None):
        """
        Start all Rx and Tx queues

        See :func:`NdpQueue.start`

        :param i: list of queue indexes
        """
        for qi in self._get_q(i, False):
            self.rx[qi].start()

        for qi in self._get_q(i, True):
            self.tx[qi].start()

    def stop(self, i: Optional[Union[int, List[int]]] = None):
        """
        Stop all Rx and Tx queues

        See :func:`NdpQueue.stop`

        :param i: list of queue indexes
        """
        for qi in self._get_q(i, False):
            self.rx[qi].stop()

        for qi in self._get_q(i, True):
            self.tx[qi].stop()

    def send(self, pkts: Union[bytes, List[bytes]], hdrs: Optional[Union[bytes, List[bytes]]] = None, flags: Optional[Union[int, List[int]]] = None, flush: bool = True, i: Optional[Union[int, List[int]]] = None) -> None:
        """
        Send burst of packets to multiple queues

        See :func:`NdpQueueTx.send`

        :param i: list of queue indexes
        """
        for qi in self._get_q(i, True):
            self.tx[qi].send(pkts, hdrs, flags, flush)

    def sendmsg(self, pkts: List[Tuple[bytes, bytes, int]], flush: bool = True, i: Optional[Union[int, List[int]]] = None) -> None:
        """
        Send burst of messages to multiple queues

        See :func:`NdpQueueTx.sendmsg`

        :param i: list of queue indexes
        """
        for qi in self._get_q(i, True):
            self.tx[qi].sendmsg(pkts, flush)

    def flush(self, i = None):
        """
        Flush the prepared packets/messages on multiple queues

        See :func:`NdpQueueTx.flush`

        :param i: list of queue indexes
        """
        for qi in self._get_q(i, True):
            self.tx[qi].flush()

    def recv(self, cnt: int = -1, timeout = 0, i: Optional[Union[int, List[int]]] = None):
        """
        Receive packets from multiple queues

        See :func:`NdpQueueRx.recv`

        :param i: list of queue indexes
        """
        return [(pkt, qi) for (pkt, _, _), qi in self.recvmsg(cnt, timeout, i)]

    def recvmsg(self, cnt: int = -1, timeout = 0, i: Optional[Union[int, List[int]]] = None):
        """
        Receive messages from multiple queues

        See :func:`NdpQueueRx.recvmsg`

        :param i: list of queue indexes
        """
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


cdef class NdpQueue:
    """
    Object representing a NDP queue
    """

    cdef NfbDeviceHandle _handle
    cdef ndp_queue *_q
    cdef dict __dict__
    cdef int _index
    cdef bool _running

    def __init__(self, nfb: Nfb, node, index: int):
        self._handle = nfb._handle
        self._index = index
        self._q = NULL
        self._running = False
        self._node = node

    def __dealloc__(self):
        if self._q is not NULL:
            if self._dir:
                ndp_close_tx_queue(self._q)
            else:
                ndp_close_rx_queue(self._q)

    cdef _check_running(self):
        if not self._running:
            if self._q is NULL:
                p1, p2 = self._handle._dev, self._index
                self._q = ndp_open_tx_queue(p1, p2) if self._dir else ndp_open_rx_queue(p1, p2)
                if self._q is NULL:
                    PyErr_SetFromErrno(OSError)

            assert ndp_queue_start(self._q) == 0
            self._running = True

    def start(self):
        """ Start the queue and prepare for transceiving"""
        self._check_running()

    def stop(self):
        """ Stop the queue"""
        if self._running:
            assert ndp_queue_stop(self._q) == 0
            self._running = False

    def stats_read(self):
        raise NotImplementedError()

    def stats_reset(self):
        raise NotImplementedError()


cdef class NdpQueueRx(NdpQueue):
    """
    Object representing a queue for receiving data from NDP
    """

    cdef libnetcope.nc_rxqueue *_nc_queue

    def __init__(self, nfb: Nfb, node, index):
        self._dir = 0
        NdpQueue.__init__(self, nfb, node, index)
        self._nc_queue = libnetcope.nc_rxqueue_open(self._handle._dev, nfb._fdt_path_offset(node))

    def __dealloc__(self):
        if self._nc_queue is not NULL:
            libnetcope.nc_rxqueue_close(self._nc_queue)

    def _check_nc_queue(self):
        if not self.is_accessible():
            raise Exception("nc_queue is invalid")

    def is_accessible(self):
        """ Check if the control registers are accessible """
        return self._nc_queue is not NULL

    def is_available(self):
        """ Check if the queue can transfer data """
        raise NotImplementedError()

    def stats_reset(self):
        """Reset statistic counters"""
        self._check_nc_queue()
        libnetcope.nc_rxqueue_reset_counters(self._nc_queue)

    def stats_read(self) -> dict:
        """
        Read statistic counters

        :return: Dictionary with counters values: 'received', 'received_bytes', 'discarded', 'discarded_bytes'.
        """
        cdef libnetcope.nc_rxqueue_counters counters

        self._check_nc_queue()
        libnetcope.nc_rxqueue_read_counters(self._nc_queue, &counters)

        return {
                'received': counters.received,
                'received_bytes': counters.received_bytes,
                'discarded': counters.discarded,
                'discarded_bytes': counters.discarded_bytes,
        }

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

                    l_hdr = ndppkt[i].header_length
                    c_hdr = <unsigned char *> malloc(l_hdr)
                    if c_hdr == NULL:
                        raise MemoryError()
                    memcpy(c_hdr, ndppkt[i].header, l_hdr)

                    pkts.append((<bytes>c_pkt[:l_pkt], <bytes>c_hdr[:l_hdr], ndppkt[i].flags))

                if cnt != -1:
                    cnt -= icnt
                ndp_rx_burst_put(self._q)

        return pkts

    def recv(self, cnt: int = -1, timeout = 0) -> List[bytes]:
        """
        Receive packets

        Try to receive packets from the queue.

        With default parameter values receive all currently pending packets and return.

        :param cnt: Maximum number of packets to read. cnt == -1 means unlimited packet count (timeout must be used).
        :param timeout: Maximum time in secs to wait for packets. timeout == None means unlimited time (cnt must be used).
        :return: list of packets; packet is represented by bytes
        """
        return [pkt for pkt, _, _ in self._recvmsg(cnt, int(timeout * 1000000000) if timeout is not None else None)]

    def recvmsg(self, cnt: int = -1, timeout = 0) -> List[Tuple[bytes, bytes, int]]:
        """
        Receive messages

        Try to receive messages from the queue.

        With default parameter values receive all currently pending mesages and return.

        :param cnt: Maximum number of messages to read. cnt == -1 means unlimited message count (timeout must be used).
        :param timeout: Maximum time in secs to wait for messages. timeout == None means unlimited time (cnt must be used).
        :return: list of messages; message is represented by tuple of (packet, packet_header, flags)
        """
        return self._recvmsg(cnt, int(timeout * 1000000000) if timeout is not None else None)


cdef class NdpQueueTx(NdpQueue):
    """
    Object representing a queue for transmitting data over NDP
    """

    cdef libnetcope.nc_txqueue *_nc_queue

    def __init__(self, nfb: Nfb, node, index):
        self._dir = 1
        NdpQueue.__init__(self, nfb, node, index)
        self._nc_queue = libnetcope.nc_txqueue_open(self._handle._dev, nfb._fdt_path_offset(node))

    def __dealloc__(self):
        if self._nc_queue is not NULL:
            libnetcope.nc_txqueue_close(self._nc_queue)

    def _check_nc_queue(self):
        if self._nc_queue is NULL:
            raise Exception("nc_queue is invalid")

    def is_accessible(self):
        """ Check if the control registers are accessible """
        return self._nc_queue is not NULL

    def is_available(self):
        """ Check if the queue can transfer data """
        raise NotImplementedError()

    def stats_reset(self):
        """Reset statistic counters"""
        self._check_nc_queue()
        libnetcope.nc_txqueue_reset_counters(self._nc_queue)

    def stats_read(self):
        """
        Read statistic counters

        :return: Dictionary with counters values: 'sent', 'sent_bytes'.
        """
        cdef libnetcope.nc_txqueue_counters counters

        self._check_nc_queue()
        libnetcope.nc_txqueue_read_counters(self._nc_queue, &counters)

        return {
                'sent': counters.sent,
                'sent_bytes': counters.sent_bytes,
        }

    cdef _sendmsg(self, pkts: List[Tuple[bytes, bytes, int]]): # -> None:
        cdef int cnt
        cdef int icnt
        cdef ndp_packet ndppkt[64]
        cdef const char* c_pkt
        cdef const char* c_hdr

        for burst in _batched(pkts, 64):
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
