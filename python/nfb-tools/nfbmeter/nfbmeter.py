#!/usr/bin/env python3
# Copyright (C) 2023 CESNET z. s. p. o.
# Author: Martin Spinler <spinler@cesnet.cz>
#
# SPDX-License-Identifier: BSD-3-Clause

import logging
import time
import datetime
import argparse
import json
import yaml
from types import SimpleNamespace

from os import linesep

import nfb
import nfb.libnfb as libnfb
#import ofm

#import busdebug
#import eventcounter

import curses

from . import version
from .migrations import migrate_lines
from .utils import nocurses_wrapper, NoCurses
from .probes import EventCounterHistogram, BusDebugProbe


class MeterManager():
    # Rename dict keys with this map> new_key: orig_key (nk: ok)
    remaps = {
        'rxmac': {'total': 'p', 'passed': 'r', 'passed_bytes': 'b', 'errors': 'd', 'overflowed': 'o'},
        'rxdma': {'passed': 'p', 'passed_bytes': 'b', 'dropped': 'd', 'dropped_bytes': 'db'},
        'eventcounter': {'cycles': 'c', 'events': 'e', 'wraps': 'w'},
        'busdebugprobe': {"words": 'w', "wait": 'i', "dst_hold": 'dh', "src_hold": 'sh', 'sop_cnt': 'sc', 'eop_cnt': 'ec'}
    }

    def __init__(self, dev, config):
        self._dev = dev
        self._config = config

        self._rxmac = [eth.rxmac for eth in self._dev.eth]
        self._rxdma = {i: dma for i, dma in enumerate(self._dev.ndp.rx) if dma.is_accessible()}

        self._bdc = {}
        self._ech = {}
        for p in self._config.get('probes', []):
            cfg = SimpleNamespace(**p)
            types = {
                "event_counter_histogram": (EventCounterHistogram, self._ech),
                "busdebug": (BusDebugProbe, self._bdc),
            }
            if cfg.type in types:
                cls, dic = types[cfg.type]
                probe = cls(dev, cfg)
                dic.update({cfg.id: probe})

    def init(self):
        for probe in self._ech.values():
            probe.calibrate()

        for probe in self._ech.values():
            probe.start(True)

        self.lock()
        self.strobe()
        self._prev_sample = self.read()
        self.unlock()

        self._items = {
            "rxmac": [f"rm{i}" for i, _ in enumerate(self._rxmac)],
            "rxdma": [f"rd{i}" for i in self._rxdma.keys()],
            "busdebugprobe": {
                f"{bd.id}": {
                    bd.probe_name[p]: f"{bd.id}_{bd.probe_name[p]}" for p in range(bd.nr_probes)
                } for bd in self._bdc.values()
            },
            "eventcounter_histogram": list(self._ech.keys()),
        }

    def read(self):
        sample = SimpleNamespace()
        sample.timestamp = time.time()
        data = sample.data = {}

        for i, mac in enumerate(self._rxmac):
            data[f"rm{i}"] = mac.read_stats()

        for i, dma in self._rxdma.items():
            data[f"rd{i}"] = dma.read_stats()

        for i, bd in enumerate(self._bdc.values()):
            for p in range(bd.nr_probes):
                data[f"{bd.id}_{bd.probe_name[p]}"] = bd.read_stats(p)

        for i, (id, ech) in enumerate(self._ech.items()):
            data[id] = ech.read_stats_captured()

        return sample

    def measure(self):
        self.lock()
        self.strobe()

        sc = self.read()
        sp = self._prev_sample

        rel = SimpleNamespace()
        rel.timestamp = sc.timestamp
        rel.interval  = sc.timestamp - sp.timestamp
        rel.data = {}

        # Compute difference of particular keys of all items with absolute value (no counter reset between two measurements)
        diffable_bdb = [name for bdp in self._items['busdebugprobe'].values() for name in bdp.values()]
        diffable_items = self._items['rxmac'] + self._items['rxdma'] + diffable_bdb
        for ic, ip, ik in [(sc.data[item], sp.data[item], item) for item in diffable_items]:
            rel.data[ik] = {key: (ic[key] - ip[key]) for key in ic.keys()}

        # EventCounter does reset every time, is already relative
        for ic, ik in [(sc.data[item], item) for item in self._items['eventcounter_histogram']]:
            rel.data[ik] = ic

        self.unlock()
        self._prev_sample = sc
        return rel

    def to_json(self, m: SimpleNamespace):
        """minimize measurement and convert to json"""

        ret = {}
        ret['tp'] = 'm' # type of the record

        ret.update(vars(m))

        if False and "remap":
            ret['data'].update({
                **{
                    unit_name: {
                        nk: m.data[unit_name][ok] for ok, nk in remap.items()
                    } for unit_type, remap in self.remaps.items()
                    for unit_name in self._items[unit_type]
                }
            })

        x = json.dumps(ret)
        return x

    def lock(self):
        pass

    def unlock(self):
        pass

    def strobe(self):
        pass


def get_basic_info(dev):
    info = SimpleNamespace()
    t = dev.fdt.get_node("/firmware").get_property("build-time").value
    info.build_time = datetime.datetime.fromtimestamp(t)
    for item in ["project-version", "build-revision", "card-name", "project-name", "project-variant"]:
        try:
            setattr(info, item.replace("-", "_"), dev.fdt.get_node("/firmware").get_property(item).value)
        except Exception:
            pass

    info.rxq_nr = len(dev.ndp.rx)
    info.txq_nr = len(dev.ndp.tx)
    #info.dts = dev.fdt.to_dts(tabsize=0)
    return info


def info_to_json(info):
    ret = vars(info)
    ret['tp'] = "info" # type of the record
    ret['ver'] = version.version
    ret['build_time'] = str(ret['build_time'])
    return json.dumps(vars(info))


class MeterDisplay():
    def __init__(self, items, config):
        self._items = items
        self._config = config

        if not self._config.get("display"):
            self._config["display"] = []

    def addstr(self, y, x, text):
        try:
            self._s.addstr(y, x,  text)
        except Exception:
            pass

    def _display_rxmac(self, o, m, items):
        self.addstr(o, 0, "RxMAC stats")
        for i, it in enumerate(items['rxmac']):
            item = m.data[it]
            values = {s.title(): item[s] for s in ['total', 'passed', 'errors', 'overflowed']}
            for w, (text, val) in enumerate(values.items()):
                self.addstr(o+w+1, 2, text)
                self.addstr(o+w+1, i*12+12, f"{val:>10}")
        return 6

    def _display_rxdma(self, o, m, items):
        dma_total_rcvd = sum(map(lambda x: m.data[x]['passed'], items['rxdma']))
        dma_total_disc = sum(map(lambda x: m.data[x]['dropped'], items['rxdma']))
        dma_total = dma_total_rcvd + dma_total_disc
        self.addstr(o+0, 0, f"Rx DMA | Total: {dma_total:>11} | Passed: {dma_total_rcvd:>11} | Dropped: {dma_total_disc:>11}")
        self.addstr(o+1, 2, "% Passed")
        self.addstr(o+2, 2, "% Dropped")

        for i, it in enumerate(items['rxdma']):
            item = m.data[it]

            pct_rcvd = int(item['passed']*1000 / dma_total)/10 if dma_total != 0 else '-'
            pct_disc = int(item['dropped']*1000 / dma_total)/10 if dma_total != 0 else '-'

            self.addstr(o+1, i*6+14, f"{pct_rcvd:>6}")
            self.addstr(o+2, i*6+14, f"{pct_disc:>6}")

        return 4

    def _display_busdebug(self, o, m, config):
        for i, it in enumerate(config.probes):
            if hasattr(config, "include"):
                include = {name: f"{it}_{name}" for name in config.include}
            else:
                include = self._items['busdebugprobe'][it]

            for j, (desc, pname) in enumerate(include.items()):
                item = m.data[pname]
                cycles, words, wait, dst_hold, src_hold = (item[x] for x in ['cycles', 'words', 'wait', 'dst_hold', 'src_hold'])

                words, wait, dst_hold, src_hold = \
                    (int(x * 1000 / cycles) / 10 for x in [words, wait, dst_hold, src_hold])

                self.addstr(o+2+j, 0, desc)
                self.addstr(o+1, 0, "%")
                values = {"Words": words, "Idle": wait, "DST H": dst_hold, "SRC H": src_hold}
                for w, (text, value) in enumerate(values.items()):
                    self.addstr(o+1, 5+w*6 + i*30, text)
                    self.addstr(o+2+j, 5+w*6 + i*30, f"{value:>5}")
        return j + 4

    def _display_histogram(self, o, m, config):
        prev_bins_names = None
        probes = config.probes

        for i, it in enumerate(probes):
            histograms = m.data[it]

            horiz = hasattr(config, 'style') and config.style == 'horizontal'

            #if prev_bins_names != histograms.keys():
            if prev_bins_names is None:
                prev_bins_names = histograms.keys()
                for j, bin_name in enumerate(prev_bins_names):
                    x, y = (2, j + 1) if horiz else (j*8+14, i)
                    self.addstr(o+y, x, f"{bin_name:>8}")

            x, y = (i*8+12, o) if horiz else (2, o+i+1)
            pb = [p for p in self._config['probes'] if p['id'] == it][0]
            self.addstr(y, x, pb['name'])

            vals = histograms.values()
            total = sum(vals)
            for j, bin_value in enumerate(vals):
                pct = int(bin_value*1000 / total) / 10 if total != 0 else '-'
                x, y = (i*8+12, j + 1) if horiz else (j*8+14, i + 1)
                self.addstr(o+y, x, f"{pct:>8}")

        return 2 + len(prev_bins_names) if horiz else 1 + len(probes)

    def display(self, stdscr, m):
        self._o = 0
        self._s = stdscr

        items = self._items
        o = 0
        self.addstr(o, 0, f"Time diff: {m.interval}")
        o += 1

        for config in self._config["display"]:
            config = SimpleNamespace(**config)
            if hasattr(config, "name"):
                self.addstr(o, 0, config.name)

            if config.type == "histogram":
                o += self._display_histogram(o, m, config)

            if config.type == "busdebug":
                o += self._display_busdebug(o, m, config)

            if config.type == "rxdma":
                o += self._display_rxdma(o, m, items)

            if config.type == "rxmac":
                o += self._display_rxmac(o, m, items)

        stdscr.refresh()
        if isinstance(stdscr, NoCurses):
            print("-" * 60)


def display_loop(stdscr, args):
    fixups = None
    config = None
    items = None
    display = None

    lines = open(args.load_logfile).readlines()

    for ln in lines:
        try:
            m = json.loads(ln)
        except Exception:
            continue
        m, fixups = migrate_lines(m, fixups)

        if m['tp'] == 'info':
            config = get_config(args, **m)

        if m['tp'] == 'units':
            items = m.copy()

        if display is None and items is not None and config is not None:
            display = MeterDisplay(items, config)

        if m['tp'] == 'm':
            if display is None:
                continue

            time.sleep(1)
            display.display(stdscr, SimpleNamespace(**m))


def meter_loop(stdscr, args, meter, logger, display):
    print_once = True
    t_toolstart = time.time()
    t_start = time.time()

    m = meter.measure()
    time.sleep(args.interval)

    while args.period is None or ((t_toolstart + args.period > t_start) if args.period is not None else True) or print_once:
        t_start = time.time()
        m = meter.measure()
        logging.debug(meter.to_json(m))
        logger.writelines(meter.to_json(m) + linesep)

        if print_once:
            if args.verbose:
                print("First measurement:\n" + meter.to_json(m) + "\n")
            print_once = False
        time.sleep(args.interval)

        display.display(stdscr, m)


def get_wrapper(args):
    return curses.wrapper if args.display else nocurses_wrapper


def get_config(args, **kwargs):
    defcfg_path = "/usr/share/ndk-fw/"
    defcfg_name = "nfb-meter-cfg"
    info = SimpleNamespace(**kwargs)

    if args.config is None:
        try:
            args.config = f"{defcfg_path}{info.card_name}-{info.project_name}-{info.project_variant}-{info.project_version}-{defcfg_name}.yaml"
        except Exception:
            raise Exception("Can't deduce default config filename, specify config file explicitly.")

    try:
        with open(args.config) as stream:
            config = yaml.safe_load(stream)
    except Exception:
        raise Exception("Can't load configuration file.")

    assert config['version'] == "0.1", "Incorrect version of config file"

    return config


def main():
    # Argument parsing
    arguments = argparse.ArgumentParser(description='NFB universal measurement tool')
    arguments.add_argument("-d", "--device", action="store", default=libnfb.Nfb.default_dev_path)
    arguments.add_argument("-l", "--logfile", action="store", default="/tmp/nfb-meter-stats_{pcislot}.txt")
    arguments.add_argument("-L", "--load_logfile", action="store")
    arguments.add_argument("-v", "--verbose", action="store_true")
    arguments.add_argument("-I", "--interval", action="store", default=1, type=float)
    arguments.add_argument("-D", "--display", action="store", default=None, help='display mode')
    arguments.add_argument("-P", "--period", action="store", default=None, type=float)
    arguments.add_argument("-c", "--config", action="store", default=None)
    args = arguments.parse_args()

    if args.load_logfile:
        get_wrapper(args)(display_loop, args)
        exit(0)

    dev = nfb.open(args.device)
    pcislot = dev.fdt.get_node("/system/device/endpoint0").get_property("pci-slot").value

    fn = args.logfile.format(pcislot=pcislot)
    print(f"Logging to {fn}")

    bi = get_basic_info(dev)
    logging.basicConfig(format='%(message)s', filename=fn, level=logging.DEBUG)  # encoding='utf-8')
    logging.debug(info_to_json(bi))

    info = SimpleNamespace()
    for item in ["project-version", "build-revision", "card-name", "project-name", "project-variant"]:
        try:
            setattr(info, item.replace("-", "_"), dev.fdt.get_node("/firmware").get_property(item).value)
        except Exception:
            pass

    # Load probe configuration
    config = get_config(args, **vars(info))

    # Init MeterManager and MeterDisplay
    meter = MeterManager(dev, config)
    meter.init()

    display = MeterDisplay(meter._items, config)

    # Log base informations
    logging.debug(json.dumps({'tp': 'units', **meter._items}))
    if args.verbose:
        print("Units:\n" + json.dumps({'tp': 'units', **meter._items}))
        print()

    # Run measurement loop
    with open(fn, mode="a") as logger:
        logger.write(info_to_json(get_basic_info(dev)) + linesep)
        meter.unlock()

        get_wrapper(args)(meter_loop, args, meter, logger, display)


if __name__ == '__main__':
    main()
