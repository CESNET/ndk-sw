"Software for control of the CESNET NDK based FPGA acceleration cards"

__all__ = ["open", "eth", "BaseComp", "default_dev_path"]

from . import libnfb
from . import eth

from .libnfb import open

import fdt
from typing import Optional

default_dev_path = libnfb.Nfb.default_dev_path


class BaseComp(libnfb.AbstractBaseComp):
    """
    BaseComp represents common parent for all classes that manages HW components

    Derived class should set it's own `DT_COMPATIBLE`!

    :ivar libnfb.Nfb _dev: NFB object
    :ivar libnfb.Comp _comp: Component object
    """

    def __init__(self, dev=default_dev_path, node: Optional[fdt.Node]=None, index: int=0):
        super().__init__(dev, node, index)
        self._comp = self._dev.comp_open(self._node)
