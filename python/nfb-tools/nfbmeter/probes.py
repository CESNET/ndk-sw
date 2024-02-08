from .eventcounter import EventCounter as EvCntr
from .busdebug import BusDebugCtl

from types import SimpleNamespace

class Probe:
    def __init__(self, dev, cfg):
        self._dev = dev
        self._cfg = cfg
        self.name = self._cfg.name
        self.id = self._cfg.id


class BusDebugProbe(BusDebugCtl, Probe):
    def __init__(self, dev, cfg):
        self._cfg_data = data = SimpleNamespace(**cfg.data)
        super().__init__(dev=dev, node=dev.fdt.get_node(data.node))
        Probe.__init__(self, dev, cfg)


class EventCounterHistogram(Probe):
    _calibration_groups = {}

    def __init__(self, dev, cfg):
        super().__init__(dev, cfg)

        self._ec = []
        self._names = []
        cfg = self._cfg
        self._cfg_data = data = SimpleNamespace(**cfg.data)
        for bin in data.bins:
            bin = SimpleNamespace(**bin)
            ec = EvCntr(dev=dev, node=dev.fdt.get_node(bin.node))

            cg = EventCounterHistogram._calibration_groups
            if data.calibration_group not in cg:
                cg.update({data.calibration_group: []})
            cg[data.calibration_group].append(ec)
            
            self._ec.append(ec)
            self._names.append(bin.name)

    def calibrate(self):
        ref = EventCounterHistogram._calibration_groups[self._cfg_data.calibration_group][0]
        for ec in self._ec:
            ec.calibrate(ref)

    def start(self, capture: bool = True):
        for ec in self._ec:
            ec.start(capture)

    def stats_read_captured(self):
        s = []
        for ec in self._ec:
            s.append(ec.stats_read_captured())
        return {self._names[i]: s[i]['events'] for i, ec in enumerate(self._ec)}

