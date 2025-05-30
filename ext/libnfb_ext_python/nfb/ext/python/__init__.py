"libnfb-ext for use in Python"

from abc import ABC, abstractmethod
import fdt

from . import shim


class AbstractNdpQueue(ABC):
    def start(self):
        pass

    def stop(self):
        pass


class AbstractNdpQueueRx(AbstractNdpQueue):
    @abstractmethod
    def burst_get(self, count):
        pass

    def burst_put(self):
        pass


class AbstractNdpQueueTx(AbstractNdpQueue):
    @abstractmethod
    def burst_get(self, pkts):
        pass

    def burst_put(self):
        pass

    def burst_flush(self):
        pass


class AbstractNfb(ABC):
    def __init__(self, dtb):
        self._nfb_ext_python_fdt = fdt.parse_dtb(dtb)
        self._nfb_ext_python_dtb = dtb

    def queue_open(self, index, dir, flags):
        pass

    def close(self):
        pass

    @abstractmethod
    def read(self, bus_node, comp_node, offset, size):
        return bytes([])

    @abstractmethod
    def write(self, bus_node, comp_node, offset, data):
        return 0

    def path(self):
        return shim.get_libnfb_ext_path(self)
