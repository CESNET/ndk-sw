"Software for control of the CESNET NDK based FPGA acceleration cards"

__all__ = ["open", "eth", "BaseComp"]

from . import libnfb
from . import eth

import fdt
from typing import Optional

def open(path: str = '0'):
    """Open a handle to NFB device in system

    :param path: Path to device node, default leads to /dev/nfb0
    :return: The ``Nfb`` object, enhanced by EthManager
    """

    dev = libnfb.Nfb(path)
    eth.EthManager(dev)
    return dev


class BaseComp():
    """
    BaseComp represents common parent for all classes that manages HW components

    Derived class should set it's own DT_COMPATIBLE!
    """

    DT_COMPATIBLE = None
    
    def __init__(self, dev=libnfb.Nfb.default_device, node: Optional[fdt.Node]=None, index: int=0):
        assert self.DT_COMPATIBLE is not None, "DT_COMPATIBLE must be set in derived class"
        
        self._dev = dev if isinstance(dev, libnfb.Nfb) else open(dev)
        
        if node:
            assert self.DT_COMPATIBLE == node.get_property("compatible").value, "compatible string mismatch"
        else:
            node = self._dev.fdt_get_compatible(self.DT_COMPATIBLE)[index]
        
        self._comp = self._dev.comp_open(node)

