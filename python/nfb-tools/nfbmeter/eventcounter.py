import nfb
import time


class EventCounter(nfb.BaseComp):
    DT_COMPATIBLE       = "cesnet,ofm,event_counter"

    _REG_INTERVAL       = 0x00
    _REG_EVENTS         = 0x04
    _REG_CAPTURE_EN     = 0x08
    _REG_CAPTURE_FIFO   = 0x0C

    _REG_CAPTURE_FIFO_DATA_MASK = 0x7FFFFFFF
    _REG_CAPTURE_FIFO_VLD_MASK  = 0x80000000
    _REG_INTERVAL_MAX           = 0xFFFFFFFF

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.__interval_cycles = None
        self.__interval_max = None
        self.__freq = None

        self._cycles = None

    def calibrate(self, reference: "EventCounter"=None):
        if isinstance(reference, EventCounter):
            self.__interval_max = reference.__interval_max
            self.__freq = reference.__freq

        if self.__interval_max is None:
            INIT_INTERVAL = 1
            self._comp.write32(self._REG_INTERVAL, INIT_INTERVAL)
            data = 0
            while data != INIT_INTERVAL:
                data = self._comp.read32(self._REG_INTERVAL)

            self._comp.write32(self._REG_INTERVAL, self._REG_INTERVAL_MAX)

            rc = 0
            time0 = time.time()
            while data == INIT_INTERVAL:
                data = self._comp.read32(self._REG_INTERVAL)
                rc += 1
                #time.sleep(0.00001)

            time1 = time.time()

            self.__interval_max = data
            self.__freq = round(data / (time1 - time0), -4)

            #print(f"EventCounter calibrate: max capture cycles: {hex(data)}, time: {time1 - time0}, read count: {rc}, freq: {self.__freq}")

        if self._cycles is None:
            self._cycles = self.__interval_max

    @property
    def freq(self):
        """ the approximate frequency of the connected clock signal"""
        return self.__freq

    @property
    def capture_cycles(self):
        """the clock cycles needed for one capture"""
        return self._cycles

    @capture_cycles.setter
    def capture_cycles(self, cycles):
        assert 0 <= cycles <= self.__interval_max, "value of capture cycles is out of component limits"
        self._cycles = cycles

    @property
    def capture_interval(self):
        """the time needed for one capture"""
        # FIXME: _cycles
        return self._cycles / self.__freq

    @capture_interval.setter
    def capture_interval(self, interval):
        cycles = int(interval * self.__freq)
        assert 0 <= cycles <= self.__interval_max, "value of capture interval is out of component limits"
        self.capture_cycles = cycles

    def capture_enable(self, enable: bool = True):
        # TODO: assert is calibrated
        #if self._cycles is None:
        #    self.__interval_cycles = self._REG_INTERVAL_MAX

        #self._comp.write32(self._REG_INTERVAL, self._cycles)
        self._comp.write32(self._REG_CAPTURE_EN, 1 if enable else 0)

    def start(self, capture: bool = True):
        self._comp.write32(self._REG_INTERVAL, self._cycles)
        self._comp.write32(self._REG_CAPTURE_EN, 1 if capture else 0)

    def stats_flush_captured(self):
        valid = self._REG_CAPTURE_FIFO_VLD_MASK
        cnt = 0 
        while valid:
            valid = valid & self._comp.read32(self._REG_CAPTURE_FIFO)
            if valid:
                cnt += 1
                self._comp.write32(self._REG_CAPTURE_FIFO, 0)
        #print(f"FIFO size: {cnt}")

    #def stats_read_captured(self, count: int = 0, wait: bool = False):
    def stats_read_captured(self):
        ret = None
        ret = {"cycles": 0, "events": 0, "wraps": 0} # What's better to return?
        valid = True
        while valid:
            reg = self._comp.read32(self._REG_CAPTURE_FIFO) 
            self._comp.write32(self._REG_CAPTURE_FIFO, 0)
            valid = reg & self._REG_CAPTURE_FIFO_VLD_MASK
            value = reg & self._REG_CAPTURE_FIFO_DATA_MASK

            if not valid:
                break
            if ret is None:
                ret = {"cycles": 0, "events": 0, "wraps": 0}

            # TODO: Check if correct
            ret["cycles"] += self._cycles
            ret["events"] += value
            ret["wraps"] += 1
        return ret

    def stats_read(self):
        data = self._comp.read(self._REG_INTERVAL, self._REG_CAPTURE_EN)
        cntr = {}
        for i, n in enumerate(["cycles", "events"]):
            cntr[n] = int.from_bytes(data[i*4:i*4+4], byteorder='little')
        return cntr
