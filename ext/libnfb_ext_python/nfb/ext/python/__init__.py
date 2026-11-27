"libnfb-ext for use in Python"

from abc import ABC, abstractmethod
import fdt

from . import shim
from .exception_bridge import ExceptionBridgeBase
from .lock import LockTable
from .shim import get_exception_bridge, get_libnfb_ext_path, set_exception_bridge

__all__ = [
    "AbstractNdpQueue",
    "AbstractNdpQueueRx",
    "AbstractNdpQueueTx",
    "AbstractNfb",
    "ExceptionBridgeBase",
    "LockTable",
    "get_exception_bridge",
    "get_libnfb_ext_path",
    "set_exception_bridge",
    "shim",
]


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

    # Component locks (nfb_comp_lock): the owner is one opened device (one nfb_open)
    # and its locks are released when it is closed. Override for a custom policy.

    @property
    def _nfb_ext_python_locks(self):
        # created on first use: subclasses don't have to call AbstractNfb.__init__
        try:
            return self.__dict__["_nfb_ext_python_lock_table"]
        except KeyError:
            return self.__dict__.setdefault("_nfb_ext_python_lock_table", LockTable())

    def comp_lock(self, owner, path, features):
        """Try to lock the features of the component, return True on success, False when busy."""
        return self._nfb_ext_python_locks.lock(owner, path, features)

    def comp_unlock(self, owner, path, features):
        self._nfb_ext_python_locks.unlock(owner, path, features)

    def comp_release(self, owner):
        """Release all locks of the owner (its device has been closed)."""
        self._nfb_ext_python_locks.release(owner)
