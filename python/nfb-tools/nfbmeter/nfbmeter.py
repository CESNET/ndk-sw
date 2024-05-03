#!/usr/bin/env python3
# Copyright (C) 2023 CESNET z. s. p. o.
# Author: Martin Spinler <spinler@cesnet.cz>
#
# SPDX-License-Identifier: BSD-3-Clause

import sys
import logging
import subprocess
import time
import os
import csv
import signal
import datetime
import argparse
import json
import re
import types
import yaml
from types import SimpleNamespace

from os import linesep

import nfb
import nfb.libnfb as libnfb
#import ofm

#import busdebug
#import eventcounter

import re
import curses

from .utils import *
from .probes import *


class MeterManager():
    # Rename dict keys with this map> new_key: orig_key (nk: ok)
    remaps = {
        'rxmac': {'packets': 'p', 'received': 'r', 'octets': 'b', 'discarded': 'd', 'overflowed': 'o'},
        'rxdma': {'received': 'p', 'received_bytes': 'b', 'discarded': 'dp', 'discarded_bytes': 'db'},
        'eventcounter': {'cycles': 'c', 'events': 'e', 'wraps': 'w'},
        'busdebugprobe': {"words": 'w', "wait": 'i', "dst_hold": 'dh', "src_hold": 'sh',
            'sop_cnt': 'sc', 'eop_cnt': 'ec',
        }
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
            if cfg.type == "event_counter_histogram":
                probe = EventCounterHistogram(dev, cfg)
                self._ech.update({cfg.id: probe})
            if cfg.type == "busdebug":
                probe = BusDebugProbe(dev, cfg)
                self._bdc.update({cfg.id: probe})

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
            "busdebugprobe": [
                *[f"{bd.id}_{bd.probe_name[p]}" for i, bd in enumerate(self._bdc.values()) for p in range(bd.nr_probes)],
            ],
            "eventcounter_histogram": list(self._ech.keys()),
        }

    def read(self):
        sample = SimpleNamespace()
        sample.timestamp = time.time()
        data = sample.data = {}

        for i, mac in enumerate(self._rxmac):
            data[f"rm{i}"] = mac.stats_read()

        for i, dma in self._rxdma.items():
            data[f"rd{i}"] = dma.stats_read()

        for i, bd in enumerate(self._bdc.values()):
            for p in range(bd.nr_probes):
                data[f"{bd.id}_{bd.probe_name[p]}"] = bd.stats_read(p)

        for i, (id, ech) in enumerate(self._ech.items()):
            data[id] = ech.stats_read_captured()

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
        diffable_items = self._items['rxmac'] + self._items['rxdma'] + self._items['busdebugprobe']
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
                **{unit_name: {
                        nk: m.data[unit_name][ok] for ok, nk in remap.items()
    #                        if unit[ok] != 0    # Compress even more and do not include items with zero value
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
        except:
            pass

    info.rxq_nr = len(dev.ndp.rx)
    info.txq_nr = len(dev.ndp.tx)
    #info.dts = dev.fdt.to_dts(tabsize=0)
    return info

def info_to_json(info):
    ret = vars(info)
    ret['tp'] = "info" # type of the record
    ret['ver'] = "0.1.0"
    ret['build_time'] = str(ret['build_time'])
    return json.dumps(vars(info))

class MeterDisplay():
    def __init__(self, meter, config):
        self._meter = meter
        self._config = config

        if not self._config.get("display"):
            self._config["display"] = []

    def _display_rxmac(self, o, m, stdscr, items):
        stdscr.addstr(o, 0, "RxMAC stats")
        for i, it in enumerate(items['rxmac']):
            item = m.data[it]
            values = {s.title(): item[s] for s in ['packets', 'received', 'discarded', 'overflowed']}
            for w, (text, val) in enumerate(values.items()):
                stdscr.addstr(o+w+1, 2, text)
                stdscr.addstr(o+w+1, i*12+12, f"{val:>10}")
        return 6

    def _display_rxdma(self, o, m, stdscr, items):
        rows, cols = stdscr.getmaxyx()
        dma_total_rcvd = sum(map(lambda x: m.data[x]['received'], items['rxdma']))
        dma_total_disc = sum(map(lambda x: m.data[x]['discarded'], items['rxdma']))
        dma_total = dma_total_rcvd + dma_total_disc
        stdscr.addstr(o, 0, f"Rx DMA | Total: {dma_total:>11} | Received: {dma_total_rcvd:>11} | Discarded: {dma_total_disc:>11}")
        for i, it in enumerate(items['rxdma']):
            item = m.data[it]
            stdscr.addstr(o+1, 2, f"% Received")
            stdscr.addstr(o+2, 2, f"% Discarded")

            if (i+1)*12 + 12 >= cols:
                continue

            pct_rcvd = int(item['received']*1000 / dma_total)/10 if dma_total != 0 else '-'
            pct_disc = int(item['discarded']*1000 / dma_total)/10 if dma_total != 0 else '-'

            stdscr.addstr(o+1, i*6+14, f"{pct_rcvd:>6}")
            stdscr.addstr(o+2, i*6+14, f"{pct_disc:>6}")

        return 4

    def _display_busdebug(self, o, m, stdscr, config):
        for i, it in enumerate(config.probes):
            include = config.include if hasattr(config, "include") else self._meter._bdc[it].probe_name
            for j, pname in enumerate(include):
                item = m.data[f"{it}_{pname}"]
                cycles, words, wait, dst_hold, src_hold = (item[x] for x in ['cycles', 'words', 'wait', 'dst_hold', 'src_hold'])

                words, wait, dst_hold, src_hold = \
                    (int(x * 1000 / cycles) / 10 for x in [words, wait, dst_hold, src_hold])

                stdscr.addstr(o+2+j, 0 , pname)
                stdscr.addstr(o+1, 0, "%")
                values = {"Words": words, "Idle": wait, "DST H": dst_hold, "SRC H": src_hold}
                for w, (text, value) in enumerate(values.items()):
                    stdscr.addstr(o+1, 5+w*6 + i*30, text)
                    stdscr.addstr(o+2+j, 5+w*6 + i*30, f"{value:>5}")
        return j + 4

    def _display_histogram(self, o, m, stdscr, config):
        prev_bins_names = None
        probes = config.probes

        for i, it in enumerate(probes):
            histograms = m.data[it]
            ech = self._meter._ech[it]

            horiz = hasattr(config, 'style') and config.style == 'horizontal'

            #if prev_bins_names != histograms.keys():
            if prev_bins_names == None:
                prev_bins_names = histograms.keys()
                for j, bin_name in enumerate(prev_bins_names):
                    x, y = (2, j + 1) if horiz else (j*8+14, i)
                    stdscr.addstr(o+y, x, f"{bin_name:>8}")

            x, y = (i*8+12, o) if horiz else (2, o+i+1)
            stdscr.addstr(y, x, ech.name)

            vals = histograms.values()
            total = sum(vals)
            for j, bin_value in enumerate(vals):
                pct = int(bin_value*1000 / total) / 10 if total != 0 else '-'
                x, y = (i*8+12, j + 1) if horiz else (j*8+14, i + 1)
                stdscr.addstr(o+y, x, f"{pct:>8}")

        return 2 + len(prev_bins_names) if horiz else 1 + len(probes)

    def display(self, stdscr, m):
        self._o = 0
        self._s = stdscr

        rows, cols = stdscr.getmaxyx()
        items = self._meter._items
        o = 0
        stdscr.addstr(o, 0, f"Time diff: {m.interval}")
        o += 1

        for config in self._config["display"]:
            config = SimpleNamespace(**config)
            if hasattr(config, "name"):
                stdscr.addstr(o, 0, config.name)

            if config.type == "histogram":
                o += self._display_histogram(o, m, stdscr, config)

            if config.type == "busdebug":
                o += self._display_busdebug(o, m, stdscr, config)

            if config.type == "rxdma":
                o += self._display_rxdma(o, m, stdscr, items)

            if config.type == "rxmac":
                o += self._display_rxmac(o, m, stdscr, items)

        stdscr.refresh()


def meter_loop(stdscr, args, meter, logger, display):
    print_once = True
    t_toolstart = time.time()
    t_start = time.time()

    m = meter.measure()
    time.sleep(args.interval)

    while args.period == None or ((t_toolstart + args.period > t_start) if args.period is not None else True) or print_once:
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
        if isinstance(stdscr, NoCurses):
            print("-" * 60)


def main():
    # Argument parsing
    arguments = argparse.ArgumentParser(description='NFB universal measurement tool')
    arguments.add_argument("-d", "--device", action="store", default=libnfb.Nfb.default_device)
    arguments.add_argument("-l", "--logfile", action="store", default="/tmp/nfb-meter-stats_{pcislot}.txt")
    arguments.add_argument("-v", "--verbose", action="store_true")
    arguments.add_argument("-I", "--interval", action="store", default=1, type=float)
    arguments.add_argument("-D", "--display", action="store", default=None, help='display mode')
    arguments.add_argument("-P", "--period", action="store", default=None, type=float)
    arguments.add_argument("-c", "--config", action="store", default=None)
    args = arguments.parse_args()

    dev = nfb.open(args.device)
    pcislot = dev.fdt.get_node("/system/device/endpoint0").get_property("pci-slot").value

    fn = args.logfile.format(pcislot=pcislot)
    print(f"Logging to {fn}")

    bi = get_basic_info(dev)
    logging.basicConfig(format='%(message)s', filename=fn, level=logging.DEBUG)#, encoding='utf-8')
    logging.debug(info_to_json(bi))

    info = SimpleNamespace()
    t = dev.fdt.get_node("/firmware").get_property("build-time").value
    for item in ["project-version", "build-revision", "card-name", "project-name", "project-variant"]:
        try:
            setattr(info, item.replace("-", "_"), dev.fdt.get_node("/firmware").get_property(item).value)
        except:
            pass

    # Load probe configuration 
    defcfg_path = "/usr/share/ndk-fw/"
    defcfg_name = "nfb-meter-cfg"

    if args.config is None:
        try:
            args.config = f"{defcfg_path}{info.card_name}-{info.project_name}-{info.project_variant}-{info.project_version}-{defcfg_name}.yaml"
        except:
            raise Exception("Can't deduce default config filename, specify config file explicitly.")

    try:
        with open(args.config) as stream:
            config = yaml.safe_load(stream)
    except:
        raise Exception("Can't load configuration file.")

    assert config['version'] == "0.1", "Incorrect version of config file"

    # Init MeterManager and MeterDisplay
    meter = MeterManager(dev, config)
    meter.init()

    display = MeterDisplay(meter, config)

    # Log base informations
    logging.debug(json.dumps({'tp':'units', **meter._items}))
    if args.verbose:
        print("Units:\n" + json.dumps({'tp':'units', **meter._items}))
        print()

    # Run measurement loop
    with open(fn, mode="a") as logger:
        logger.write(info_to_json(get_basic_info(dev)) + linesep)
        meter.unlock()

        wrapper = curses.wrapper if args.display else nocurses_wrapper
        wrapper(meter_loop, args, meter, logger, display)

if __name__ == '__main__':
    main()
