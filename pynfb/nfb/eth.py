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
        if name in ["enable", "disable", "stats_reset"]:
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
        self.pmd = Transceiver(nfb, nfb.fdt_get_phandle(node.get_property('pmd').value))

    def _common_action(self, action, *args, **kwargs):
        return [
            getattr(self.rxmac, action)(*args, **kwargs),
            getattr(self.txmac, action)(*args, **kwargs),
        ]

    def __getattr__(self, name):
        if name in ["link"]:
            return self.rxmac.__getattribute__(name)
        elif name in ["enable", "disable", "stats_reset"]:
            return lambda *args, **kwargs: self._common_action(name, *args, **kwargs)
        elif name in ["pma_local_loopback"]:
            return getattr(self.pcspma, name)
        else:
            raise AttributeError

class Transceiver:
    """
    Object representing a transciever interface

    :ivar I2c i2c: I2c bus handle (only with QSFP+ modules)
    """
    def __init__(self, nfb, node):
        status = nfb.fdt_get_phandle(node.get_property('status-reg').value)
        self.comp_status = nfb.comp_open(status)

        ctrl = nfb.fdt_get_phandle(node.get_property('control').value)
        node_ctrl_param = node.get_subnode('control-param')

        # FIXME
        if "QSFPP":
            prop = node_ctrl_param.get_property("i2c-addr")
            i2c_addr = prop.value if prop else 0xA0
            self.i2c = libnetcope.I2c(nfb, ctrl, i2c_addr);

    # FIXME: only valid for specific QSFP+
    @property
    def vendor_name(self) -> str:
        "vendor of the transceiver"
        return self.i2c.read_reg(148, 16).decode()
    @property
    def vendor_pn(self) -> str:
        "product number of the transceiver"
        return self.i2c.read_reg(168, 16).decode()
    @property
    def vendor_sn(self) -> str:
        "serial number of the transceiver"
        return self.i2c.read_reg(196, 16).decode()


class PcsPma:
    """
    Object representing a PCS/PMA configuration registers

    :ivar libnetcope.Mdio mdio: MDIO bus handle
    :ivar int mdio_portad: Port address of PCS/PMA on MDIO bus
    """

    def __init__(self, nfb, node):
        ctrl = nfb.fdt_get_phandle(node.get_property('control').value)
        prop_ctrl_param = node.get_property('control-param')
        ctrl_param = nfb.fdt_get_phandle(prop_ctrl_param.value) if prop_ctrl_param else None
        self.mdio = libnetcope.Mdio(nfb, ctrl, ctrl_param)
        # FIXME: Check and enable!!!
        self.mdio_portad = ctrl_param.get_property("dev").value if ctrl_param and ctrl_param.get_property("dev") else 0

    def _read(self, dev, reg): return self.mdio.read(self.mdio_portad, dev, reg)
    def _write(self, dev, reg, val): return self.mdio.write(self.mdio_portad, dev, reg, val)

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
