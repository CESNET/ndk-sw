#!/usr/bin/env python3


import sys
from time import sleep
from argparse import ArgumentParser

import nfb

from  ofm.comp.mfb_tools.logic.speed_meter.speed_meter import SpeedMeter

def parseParams():
    parser = ArgumentParser(description =
        """Peer-to-Peer DMA Calypte speed measurement""",
    )

    access = parser.add_argument_group('card access arguments')
    access.add_argument('-r', '--recv', default=nfb.default_dev_path,
                        metavar='tx_device', help = "Index of a receiving NFB device")
    access.add_argument('-t', '--trans', default=nfb.default_dev_path,
                        metavar='rx_device', help = "Index of a transmitting NFB device")

    args = parser.parse_args()
    return args

if __name__ == "__main__":
    args = parseParams()
    l_recv_meter = SpeedMeter(True, dev=args.trans, index=3)
    l_trans_meter = SpeedMeter(True, dev=args.trans, index=0)
    r_recv_meter = SpeedMeter(True, dev=args.recv, index=3)
    r_trans_meter = SpeedMeter(True, dev=args.recv, index=0)

    try:
        while True:
            l_recv_bps, _ = l_recv_meter.measure(0.1, 250000000)
            l_recv_bps = l_recv_bps / 1e9
            l_trans_bps, _ = l_trans_meter.measure(0.1, 250000000)
            l_trans_bps = l_trans_bps / 1e9
            r_recv_bps, _ = r_recv_meter.measure(0.1, 250000000)
            r_recv_bps = r_recv_bps / 1e9
            r_trans_bps, _ = r_trans_meter.measure(0.1, 250000000)
            r_trans_bps = r_trans_bps / 1e9
            print(f"R_RECEIVING:    {r_recv_bps:.3f} Gbps  ({r_recv_bps/8:.3f} GBps)\n",
                  f"R_TRANSMITTING: {r_trans_bps:.3f} Gbps ({r_trans_bps/8:.3f} GBps)\n",
                  f"L_RECEIVING:    {l_recv_bps:.3f} Gbps  ({l_recv_bps/8:.3f} GBps)\n"
                  f"L_TRANSMITTING: {l_trans_bps:.3f} Gbps ({l_trans_bps/8:.3f} GBps)\n",
                  )
            l_recv_meter.clear_data()
            l_trans_meter.clear_data()
            r_recv_meter.clear_data()
            r_trans_meter.clear_data()
            print("===========================================")
            sleep(1)

    except KeyboardInterrupt:
        print("Interrupt caught, terminating...")
        l_recv_meter.clear_data()
        l_trans_meter.clear_data()
        r_recv_meter.clear_data()
        r_trans_meter.clear_data()
        sys.exit(0)
