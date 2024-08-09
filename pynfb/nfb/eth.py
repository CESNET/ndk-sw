from . import libnetcope


class EthManager:
    """Object which encapsulates all Ethernet interfaces into nfb"""

    def __init__(self, nfb):
        self._eth = [Eth(nfb, node) for i, node in enumerate(nfb.fdt_get_compatible("netcope,eth"))]
        nfb.eth = self

    def __iter__(self):
        return self._eth.__iter__()

    def __getitem__(self, index):
        return self._eth[index] #TODO: self._eth.__getitem(index)

    def _common_action(self, action, *args, **kwargs):
        eths = self
        if 'i' in kwargs:
            indexes = [kwargs['i']] if isinstance(kwargs['i'], int) else kwargs['i']
            eths = [e for i, e in enumerate(self) if i in indexes]
            del kwargs['i']

        return [getattr(eth, action)(*args, **kwargs) for eth in eths]

    def __getattr__(self, name):
        if name in ["enable", "disable", "stats_reset", "reset_stats", "is_link"]:
            return lambda *args, **kwargs: self._common_action(name, *args, **kwargs)
        else:
            raise AttributeError


class Eth:
    """Object representing one Ethernet interface

    :ivar libnetcope.RxMac rxmac: Input MAC object
    :ivar libnetcope.TxMac txmac: Ouput MAC object
    :ivar PcsPma pcspma: PCS/PMA object
    :ivar Transceiver pmd: Transceiver object
    """

    def __init__(self, nfb, node):
        self.rxmac = libnetcope.RxMac(nfb, nfb.fdt_get_phandle(node.get_property('rxmac').value))
        self.txmac = libnetcope.TxMac(nfb, nfb.fdt_get_phandle(node.get_property('txmac').value))
        self.pcspma = PcsPma(nfb, nfb.fdt_get_phandle(node.get_property('pcspma').value))
        self.pmd = libnetcope.Transceiver(nfb, nfb.fdt_get_phandle(node.get_property('pmd').value))

    def _common_action(self, action, *args, **kwargs):
        return [
            getattr(self.rxmac, action)(*args, **kwargs),
            getattr(self.txmac, action)(*args, **kwargs),
        ]

    def __getattr__(self, name):
        if name in ["link", "is_link"]:
            return self.rxmac.__getattribute__(name)
        elif name in ["enable", "disable", "stats_reset", "reset_stats"]:
            return lambda *args, **kwargs: self._common_action(name, *args, **kwargs)
        elif name in ["pma_local_loopback"]:
            return getattr(self.pcspma, name)
        else:
            raise AttributeError

    def __setattr__(self, name, value):
        if name in ["link"]:
            raise AttributeError(f"'Eth' object attribute '{name}' is read-only")
        elif name in ["pma_local_loopback"]:
            return setattr(self.pcspma, name, value)
        else:
            object.__setattr__(self, name, value)





class PcsPma:
    """
    Object representing a PCS/PMA configuration registers

    :ivar libnetcope.Mdio mdio: MDIO bus handle
    :ivar int mdio_portad: Port address of PCS/PMA on MDIO bus
    """

    def __init__(self, nfb, node):
        ctrl = nfb.fdt_get_phandle(node.get_property('control').value)
        ctrl_param = node.get_subnode('control-param')
        self.mdio = libnetcope.Mdio(nfb, ctrl, ctrl_param)
        # FIXME: Check and enable!!!
        self.mdio_portad = ctrl_param.get_property("dev").value if ctrl_param and ctrl_param.get_property("dev") else 0

    def _read(self, dev, reg): return self.mdio.read(dev, reg, self.mdio_portad)
    def _write(self, dev, reg, val): return self.mdio.write(dev, reg, val, self.mdio_portad)

    def _set_bit(self, dev, reg, bit, value=True):
        oldval = self._read(dev, reg)
        newval = (oldval | bit) if value else (oldval & ~bit)
        if newval != oldval:
            self._write(dev, reg, newval)

    def _clr_bit(self, dev, reg, bit):
        self._set_bit(dev, reg, bit, False)

    def _bit(self, dev, reg, bit):
        return True if (self._read(dev, reg) & bit) else False

    @property
    def pma_local_loopback(self):
        """Get or set loopback on local side of PMA module"""
        return self._bit(1, 0, 0x1)

    @pma_local_loopback.setter
    def pma_local_loopback(self, value): self._set_bit(1, 0, 0x1, value)
