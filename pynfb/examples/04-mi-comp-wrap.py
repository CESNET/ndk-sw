#!/usr/bin/python3

##########################################
# Python NFB API examples - MI comp wrap #
##########################################

import nfb


class IdComp(nfb.BaseComp):
    DT_COMPATIBLE = "netcope,idcomp"

    # MI registers addresses
    _REG_TEST = 0

    # _REG_TEST bits
    _BIT_TEST = 0

    def __init__(self, **kwargs):
        """**kwargs
        The keyword arguments are passed to `OfmComp.__init__() (it accepts: dev, node, index)`
        """
        super().__init__(**kwargs)

        # You can access device and component using:
        #   self._dev
        #   self._comp

    def print(self):
        print(self._comp.read32(self._REG_TEST))
        print(self._comp.get_bit(self._REG_TEST, self._BIT_TEST))


# Usage
if __name__ == "__main__":
    handle = IdComp()
    handle = IdComp(index=0)
    handle = IdComp(dev="/dev/nfb0")

    dev = nfb.open()
    handle = IdComp(dev=dev, node=dev.fdt_get_compatible(IdComp.DT_COMPATIBLE)[0])
    handle.print()
