"Software for control of the CESNET NDK based FPGA acceleration cards"

__all__ = ["open"]

from . import libnfb

def open(path: str = '0'):
    """Open a handle to NFB device in system

    :param path: Path to device node, default leads to /dev/nfb0
    :return: The ``Nfb`` object, enhanced by EthManager
    """

    dev = libnfb.Nfb(path)
    return dev
