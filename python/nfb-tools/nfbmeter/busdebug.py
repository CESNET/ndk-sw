import nfb


def po(probe_id, reg):
    return probe_id << 6 | reg


class BusDebugCtl(nfb.BaseComp):
    DT_COMPATIBLE       = "netcope,streaming_debug_master"

    _WORD_CNT_LOW       = 0x00
    _WORD_CNT_HIGH      = 0x04
    _WAIT_CNT_LOW       = 0x08
    _WAIT_CNT_HIGH      = 0x0C
    _DST_HOLD_CNT_LOW   = 0x10
    _DST_HOLD_CNT_HIGH  = 0x14
    _SRC_HOLD_CNT_LOW   = 0x18
    _SRC_HOLD_CNT_HIGH  = 0x1C
    _SOP_CNT_LOW        = 0x20
    _SOP_CNT_HIGH       = 0x24
    _EOP_CNT_LOW        = 0x28
    _EOP_CNT_HIGH       = 0x2C
    _NAME               = 0x30
    _CONFIG             = 0x34
    _CNT_CTRLREG        = 0x38
    _BUS_CTRLREG        = 0x3C

    _SPACE_WORDS        = 0x10

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self._nr_probes = self._node.get_property("probes").value

        self.probe_name = []
        for i in range(self._nr_probes):
            self.probe_name.append(self._comp.read(po(i, self._NAME), 4).decode())

    @property
    def nr_probes(self):
        return self._nr_probes

    def enable(self, pid):
        self._comp.write32(po(pid, self._CNT_CTRLREG), 1)

    def disable(self, pid):
        self._comp.write32(po(pid, self._CNT_CTRLREG), 0)

    def bus_block(self, pid):
        self._comp.write32(po(pid, self._BUS_CTRLREG), 1)

    def bus_drop(self, pid):
        self._comp.write32(po(pid, self._BUS_CTRLREG), 2)

    def bus_enable(self, pid):
        self._comp.write32(po(pid, self._BUS_CTRLREG), 0)

    def read_stats(self, pid):
        # TODO Check: atomic read / strobe?
        data = self._comp.read(po(pid, self._WORD_CNT_LOW), self._NAME)
        cntr = {}
        for i, n in enumerate(["words", "wait", "dst_hold", "src_hold", "sop_cnt", "eop_cnt"]):
            cntr[n] = int.from_bytes(data[i*8:i*8+8], byteorder='little')

        cntr['cycles'] = sum([v for k, v in cntr.items() if k in ["words", "wait", "dst_hold", "src_hold"]])
        #cntr['block'] =  cntr['wait']
        #cntr['ready'] = cntr['cycles'] - cntr['dst_hold']
        #cntr['valid'] = cntr['cycles'] - cntr['src_hold']
        #cntr['transfer'] = cntr['cycles'] - cntr['src_hold']
        return cntr
