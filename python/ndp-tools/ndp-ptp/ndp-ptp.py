#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
#
# Experimental tool connecting pairs of (RX,TX) channels in a Peer-to-Peer manner
#
# Copyright (C) 2025-2025 Universitaet Heidelberg, Institut fuer Technische Informatik (ZITI)
# Author(s):
#     Vladislav Valek <vladislav.valek@stud.uni-heidelberg.de>

# TODO: Currently, this tool supports only one-endpoint devices. A support
# for multi-endpoint devices (like bifurcated ones) is a subject of future development.

import sys
import re
import argparse

from time import sleep

import nfb

from ofm.comp.mfb_tools.debug.gen_loop_switch import GenLoopSwitch
from ofm.utils import convert_units


class P2PChain:

    _REG_CONTROL     = 0x00
    _REG_STATUS      = 0x04
    _REG_EXPER       = 0x08
    _REG_SDP         = 0x10
    _REG_SHP         = 0x14
    _REG_TIMEOUT     = 0x20
    _REG_DESC_BASE   = 0x40
    _REG_HDR_BASE    = 0x48
    _REG_UPDATE_BASE = 0x50
    _REG_MDP         = 0x58
    _REG_MHP         = 0x5C

    def __init__(self, trans_dev, recv_dev, qcount, full_dpx_en=False):
        self.trans_dev = trans_dev
        self.recv_dev = recv_dev
        self.trans_bars = self._findDevBarAddrs(trans_dev)
        self.recv_bars = self._findDevBarAddrs(recv_dev)
        self.trans_qs = trans_dev.fdt_get_compatible("cesnet,dma_ctrl_calypte_rx")
        self.recv_qs = recv_dev.fdt_get_compatible("cesnet,dma_ctrl_calypte_tx")

        if full_dpx_en:
            self.dpx_trans_qs = recv_dev.fdt_get_compatible("cesnet,dma_ctrl_calypte_rx")
            self.dpx_recv_qs = trans_dev.fdt_get_compatible("cesnet,dma_ctrl_calypte_tx")

        self.qcount = qcount
        self.lowest_qcount = min(len(self.trans_qs), len(self.recv_qs))

    def _getNodeOffset(self, node):
        # NOTE: assumes properties #address-cells = 1 and #size-cells = 1
        prop = node.get_property("reg")
        return prop.value

    def _findDevBarAddrs(self, dev):
        bdf_str = dev.fdt.get_property("pci-slot", "/system/device/endpoint0")[0]
        pattern = re.compile(r"^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]$")

        if pattern.match(bdf_str) is None:
            raise ValueError(f"Invalid BDF identifier: {bdf_str}")

        path = f"/sys/bus/pci/devices/{bdf_str}/resource"
        bars = []
        try:
            with open(path, "r") as f:
                for line in f:
                    start, _, _ = line.strip().split()
                    bars.append(int(start, 16))
        except FileNotFoundError:
            raise RuntimeError(f"Device {bdf_str} not found in sysfs")

        return bars

    def _chanStart(self, comp):
        comp.set_bit(self._REG_CONTROL, 0)
        return comp.wait_for_bit(self._REG_STATUS, 0)

    def _setupReceiver(self, trans_bar0, recv_q, trans_q, recv_dev):
        recv_q_comp = recv_dev.comp_open(recv_q)

        ptr_offs = trans_bar0 + self._getNodeOffset(trans_q) + self._REG_SDP
        print(f"Writing RX UPD Buff addr: {ptr_offs:x} to remote {recv_q.name}")
        recv_q_comp.write64(self._REG_UPDATE_BASE, ptr_offs)
        recv_q_comp.set_bit(self._REG_EXPER, 0)
        recv_q_comp.write32(self._REG_TIMEOUT, 0x4000)
        recv_q_comp.write64(self._REG_SDP, 0)

        return self._chanStart(recv_q_comp)

    def _setupTransmitter(self, recv_bar0, recv_bar2, trans_q, recv_q, trans_dev, recv_dev):
        trans_q_comp = trans_dev.comp_open(trans_q)
        recv_q_comp = recv_dev.comp_open(recv_q)

        ptr_offs = recv_bar0 + self._getNodeOffset(recv_q) + self._REG_SDP
        print(f"Writing TX UPD Buff addr: {ptr_offs:x} to local {trans_q.name}")
        trans_q_comp.write64(self._REG_UPDATE_BASE, ptr_offs)

        dbuff_phandle = recv_q.get_property("data_buff").value
        dbuff_node = recv_dev.fdt_get_phandle(dbuff_phandle)
        dbuff_offs = recv_bar2 + self._getNodeOffset(dbuff_node)
        print(f"Writing TX DATA Buff addr: {dbuff_offs:x} to local {trans_q.name}")
        trans_q_comp.write64(self._REG_DESC_BASE, dbuff_offs)

        hbuff_phandle = recv_q.get_property("hdr_buff").value
        hbuff_node = recv_dev.fdt_get_phandle(hbuff_phandle)
        hbuff_offs = recv_bar2 + self._getNodeOffset(hbuff_node)
        print(f"Writing TX DATA Buff addr: {hbuff_offs:x} to local {trans_q.name}")
        trans_q_comp.write64(self._REG_HDR_BASE, hbuff_offs)

        mhp = recv_q_comp.read16(self._REG_MHP)
        mdp = recv_q_comp.read16(self._REG_MDP)

        trans_q_comp.write16(self._REG_MHP, mhp)
        # Data pointer for TX controller is in bytes whereas RX controller
        # advances in 128-byte blocks
        trans_q_comp.write16(self._REG_MDP, mdp // 128)
        trans_q_comp.set_bit(self._REG_EXPER, 0)
        trans_q_comp.write32(self._REG_TIMEOUT, 0x4000)
        trans_q_comp.write64(self._REG_SDP, 0)

        return self._chanStart(trans_q_comp)

    def _stopQPair(self, qidx, egress=True):
        ret = True
        recv_queue = self.recv_qs[qidx] if egress else self.dpx_recv_qs[qidx]
        trans_queue = self.trans_qs[qidx] if egress else self.dpx_trans_qs[qidx]
        trans_dev = self.trans_dev if egress else self.recv_dev
        recv_dev = self.recv_dev if egress else self.trans_dev

        trans_q_comp = trans_dev.comp_open(trans_queue)
        recv_q_comp = recv_dev.comp_open(recv_queue)

        trans_q_comp.clr_bit(self._REG_CONTROL, 0)
        recv_q_comp.clr_bit(self._REG_CONTROL, 0)

        ret &= recv_q_comp.wait_for_bit(self._REG_STATUS, 0, level=False)
        if not ret:
            print("Failed to stop {} receiver {}".format("remote" if egress else "local", qidx))

        ret &= trans_q_comp.wait_for_bit(self._REG_STATUS, 0, level=False)
        if not ret:
            print("Failed to stop {} transmitter {}".format("local" if egress else "remote", qidx))

        return ret

    def _startQPair(self, qidx, egress=True):
        recv_queue = self.recv_qs[qidx] if egress else self.dpx_recv_qs[qidx]
        trans_queue = self.trans_qs[qidx] if egress else self.dpx_trans_qs[qidx]
        trans_bars = self.trans_bars if egress else self.recv_bars
        recv_bars = self.recv_bars if egress else self.trans_bars
        trans_dev = self.trans_dev if egress else self.recv_dev
        recv_dev = self.recv_dev if egress else self.trans_dev

        ret = self._setupReceiver(trans_bars[0], recv_queue, trans_queue, recv_dev)
        if not ret:
            print("Failed to start {} receiver {}".format("remote" if egress else "local", qidx))
            return ret

        ret = self._setupTransmitter(recv_bars[0], recv_bars[2], trans_queue, recv_queue, trans_dev, recv_dev)
        if not ret:
            print("Failed to start {} transmitter {}".format("local" if egress else "remote", qidx))
            return ret

        return ret

    def stopChans(self, full_dpx_en=False):
        """Stops all channels in active qpairs

        All of the channels get stopped regardless if the stop attempt succeded or not. A warning
        is issued if the attempt has ended in unsuccess

        Args:
            full_dpx_en: disable qpairs in the oposite direction too
        """

        for qidx in range(self.qcount):
            ret = self._stopQPair(qidx)
            if not ret:
                print(f"Failed to stop egress qpair {qidx}")

            if full_dpx_en:
                ret = self._stopQPair(qidx, False)
                if not ret:
                    print(f"Failed to stop ingress qpair {qidx}")

    def startChans(self, full_dpx_en=False):
        """Starts all selected channels

        A start attempt is done on every channel. If an attempt ends up
        unsuccessful, all previously enabled channels get shutdown and an error
        is returned.

        Args:
            full_dpx_en: disable qpairs in the oposite direction too

        Returns:
            True if all channels were successfully enabled, otherwise False
        """

        ret = False

        for qidx in range(self.qcount):
            ret = self._startQPair(qidx)
            if not ret:
                print(f"Failed to start egress qpair {qidx}")
                self.stopChans()
                return ret

            if full_dpx_en:
                ret = self._startQPair(qidx, False)
                if not ret:
                    print(f"Failed to ingress qpair {qidx}")
                    return ret

        return ret


def parseParams():
    help_dict = {
        "transmitter" : "transmitting (local) device",
        "receiver"    : "receiving (remote) device",
        "channels"    : "specify the amount of communicating channels",
        "loopback"    : "enables loopback in the remote device which sends data back over the same"
                        " amount of channels",
        "local_gen"   : "enables local MFB generator generating packets of the specified size",
        "remote_gen"  : "enables remote MFB generator generating packets of the specified size",
        "measure"     : "continuously measures throughput on both sides of the P2P chain",
        "freq"        : "clock frequency of the domain where speed meters are located",
    }

    ptp_desc = """
        This script allows to connect two devices with DMA Calypte modules in a
        Peer-to-Peer (P2P) manner, meaning without the CPU involved in
        facilitating the data copy.
    """

    examples = """
        Examples:
            1) Unidirectional connect from device 1 to 2
                ndp-ptp.py -t1 -r2

            2) Bidirectional connect from device 1 to 2 with packet generation and loopback
               of data back from device 2
                ndp-ptp.py -t1 -r2 -g1024 -l

            3) Bidirectional connect from device 1 to 2 with packet generation in both directions.
               Enable measurement over 4 channels
                ndp-ptp.py -t1 -r2 -g1024 -a512 -m -c4
    """

    arg_parser = argparse.ArgumentParser(
        prog="ndp-ptp",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=ptp_desc,
        epilog=examples
    )

    arg_parser.add_argument("-t", "--transmitter", default=nfb.default_dev_path, help=help_dict["transmitter"])
    arg_parser.add_argument("-r", "--receiver", default=nfb.default_dev_path, help=help_dict["receiver"])
    arg_parser.add_argument("-c", "--channels", type=int, default=1, help=help_dict["channels"])
    arg_parser.add_argument("-l", "--lbk", action="store_true", help=help_dict["loopback"])
    arg_parser.add_argument("-g", "--local-gen", type=int, default=None, help=help_dict["local_gen"])
    arg_parser.add_argument("-a", "--remote-gen", type=int, default=None, help=help_dict["remote_gen"])
    arg_parser.add_argument("-m", "--measure", action="store_true", help=help_dict["measure"])
    arg_parser.add_argument("-f", "--freq", type=int, default=200000000, help=help_dict["freq"])
    args = arg_parser.parse_args()
    print(args)

    assert args.channels > 0, f"Invalid amount of channels: {args.channels}!"

    if args.local_gen is not None:
        assert args.local_gen >= 60 and args.local_gen <= 4096, \
            f"Invalid packet size for local packet generator: {args.local_gen}!"

    if args.remote_gen is not None:
        assert args.remote_gen >= 60 and args.remote_gen <= 4096, \
            f"Invalid packet size for remote packet generator: {args.remote_gen}!"
        assert args.local_gen is not None, \
            "Remote generator can be activated only if the local generator is active!"

    return args


def main():
    args = parseParams()

    single_dev = False
    full_dpx_en = args.lbk or (args.remote_gen is not None)

    # A device where RX DMA is activated
    trans_dev = nfb.open(path=args.transmitter)
    if args.transmitter == args.receiver:
        recv_dev = trans_dev
        single_dev = True
        full_dpx_en = False
    else:
        # A device where TX DMA is activated
        recv_dev = nfb.open(path=args.receiver)

    ptp_chain = P2PChain(trans_dev, recv_dev, args.channels, full_dpx_en)

    print(f"Transmitter -> BAR0: {ptp_chain.trans_bars[0]:x}, BAR2: {ptp_chain.trans_bars[2]:x}")
    print(f"Receiver    -> BAR0: {ptp_chain.recv_bars[0]:x}, BAR2: {ptp_chain.recv_bars[2]:x}")

    if not ptp_chain.startChans(full_dpx_en):
        print("Exiting...")
        sys.exit(-1)

    trans_gls = GenLoopSwitch(dev=args.transmitter, index=0)
    recv_gls = GenLoopSwitch(dev=args.receiver, index=0)

    if args.lbk and not single_dev:
        recv_gls.l2r.input = 3

    if args.local_gen is not None:
        trans_gls.l2r.gen_start(True, args.local_gen, 0, args.channels-1)

    if not args.lbk and args.remote_gen is not None and not single_dev:
        recv_gls.l2r.gen_start(True, args.remote_gen, 0, args.channels-1)

    if args.measure and args.local_gen is not None:
        sms = [trans_gls.l2r.tx_sm, recv_gls.r2l.rx_sm]
        if full_dpx_en:
            sms += [recv_gls.l2r.tx_sm, trans_gls.r2l.rx_sm]

        try:
            while True:
                sm_speeds = []

                for sm_comp in sms:
                    sm_comp.clear_data()
                    bps, _ = GenLoopSwitch.sm_measure(sm_comp, freq=args.freq)
                    b_speed, b_unit = convert_units(bps)
                    sm_speeds.append(f"{b_speed:>6.02f} {b_unit}bps")

                diagram = f"""\
   ===================================================================================
                 Local device                      Remote device
    +-----+
    | GEN | ---> {sm_speeds[0]} ---------------------> {sm_speeds[1]} ----> X
    +-----+\
"""
                if full_dpx_en:
                    mode = "LBK" if args.lbk else "GEN"
                    diagram += f"""
                                                                      +-----+
          X <--- {sm_speeds[3]} <--------------------- {sm_speeds[2]} <---- | {mode} |
                                                                      +-----+
"""
                print(diagram)
                sleep(1)

        except KeyboardInterrupt:
            print("Interrupt caught, stopping...")
    else:
        try:
            while True:
                sleep(1)
        except KeyboardInterrupt:
            print("Interrupt caught, stopping..")

    if not args.lbk and args.remote_gen is not None and not single_dev:
        recv_gls.l2r.gen_stop()

    if args.local_gen is not None:
        trans_gls.l2r.gen_stop()

    if args.lbk and not single_dev:
        recv_gls.l2r.input = 0

    ptp_chain.stopChans(full_dpx_en)


if __name__ == "__main__":
    main()
