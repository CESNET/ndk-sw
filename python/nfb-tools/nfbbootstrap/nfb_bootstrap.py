#!/usr/bin/env python3
import argparse
import fdt

from .dfl import parse_dfl


def main():
    parser = argparse.ArgumentParser(prog='nfb-bootstrap',
                                     description='Bootstrap factory firmare to NDK platform')

    parser.add_argument('-d', '--device', help='Device specifier, e.g. 0000:03:00.0')
    parser.add_argument('-c', '--card', help='Card name, e.g. "N6010"')
    parser.add_argument('--dtb', action='store', default='nfb-bootstrap.dtb', help='Output DTB filename')
    parser.add_argument('-v', '--verbose', action='store_true')

    args = parser.parse_args()

    dt = fdt.FDT()
    n_fw = fdt.Node('firmware')
    dt.add_item(n_fw)

    if args.card:
        n_fw.append(fdt.PropStrings('card-name', args.card))

    n_mi = fdt.Node('mi_bus0')
    n_mi.append(fdt.PropStrings('compatible', "netcope,bus,mi"))
    n_mi.append(fdt.PropStrings('resource', "PCI0,BAR0"))
    n_fw.append(n_mi)

    if args.card in ['N6010']:
        features = parse_dfl(args.device, args.verbose)

        # Search for the 'PMCI Subsystem' feature
        if features.get(0x12):
            pmci_reg = features.get(0x12)

            n_pmci = fdt.Node('pmci')
            n_mi.append(n_pmci)
            n_pmci.append(fdt.PropWords('reg', *pmci_reg))
            n_pmci.append(fdt.PropStrings("compatible", "cesnet,pmci"))

    with open(args.dtb, "wb") as f:
        f.write(dt.to_dtb(version=17))

if __name__ == "__main__":
    main()
