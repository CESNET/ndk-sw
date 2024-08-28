# Copyright (C) 2023 CESNET z. s. p. o.
# Author: Martin Spinler <spinler@cesnet.cz>
#
# SPDX-License-Identifier: BSD-3-Clause
from . import version


def v010_to_v020_units(items, state):
    items = items.copy()
    if 'busdebugprobe' in items:
        bdps_orig = items['busdebugprobe']
        bdps = items['busdebugprobe'] = {}
        for bd in bdps_orig:
            name, pname = bd.rsplit("_", 1)
            if name not in bdps:
                bdps.update({name: {}})
            bdps[name].update({pname: f"{name}_{pname}"})

    state['units'] = items
    return items


def v010_to_v020_m(m, state):
    units = state['units']
    for k, v in m['data'].items():
        if k in units['rxmac']:
            kmap = {'packets': 'total', 'received': 'passed', 'octets': 'passed_bytes', 'discarded': 'errors'}
            for k_old, k_new in kmap.items():
                v[k_new] = v.pop(k_old)
        if k in units['rxdma']:
            kmap = {'received': 'passed', 'discarded': 'dropped', 'received_bytes': 'passed_bytes', 'discarded_bytes': 'dropped_bytes'}
            for k_old, k_new in kmap.items():
                v[k_new] = v.pop(k_old)
    return m


def migrate_lines(m, fixups):
    if m['tp'] == 'info':
        if fixups is None or m['ver'] == version.version:
            fixups = {
                '__state__': {},
                'units': lambda m, s: m,
                'm': lambda m, s: m,
            }

        if m['ver'] == "0.1.0":
            fixups['units'] = v010_to_v020_units
            fixups['m'] = v010_to_v020_m
        else:
            raise NotImplementedError

    elif m['tp'] == 'units':
        assert fixups is not None
        m = fixups['units'](m, fixups['__state__'])
    elif m['tp'] == 'm':
        assert fixups is not None
        m = fixups['m'](m, fixups['__state__'])

    return m, fixups
