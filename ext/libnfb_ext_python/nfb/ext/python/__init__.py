"libnfb-ext for use in Python"

__all__ = ["AbstractNfbShim"]

import importlib
import fdt

class AbstractNfbShim:
    def __init__(self):
        self.__libnfb_ext_python = importlib.import_module(
            "libnfb-ext-python.libnfb_ext_python"
        )
        self.fdt = fdt.parse_dtb(self.dtb)

    def read(self, bus_node, comp_node, offset, size):
        return bytes([])

    def write(self, bus_node, comp_node, offset, data):
        return 0

    def path(self):
        return self.__libnfb_ext_python.__file__ + ":pynfb:" + str(id(self))
