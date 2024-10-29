# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
#
# NFB driver Makefile
#
# Copyright (C) 2017-2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz>

nfb-objs += ../fdt/fdt.o ../fdt/fdt_ro.o ../fdt/fdt_rw.o ../fdt/fdt_wip.o ../fdt/fdt_sw.o ../fdt/fdt_empty_tree.o
nfb-objs += ../cfi/cfi_cmdset_0001.o ../cfi/cfi_cmdset_0002.o ../cfi/cfi_cmdset_0020.o ../cfi/cfi_probe.o ../cfi/cfi_util.o ../cfi/chipreg.o ../cfi/gen_probe.o ../cfi/mtdcore.o
nfb-objs += ../spi-nor/spi-nor.o
nfb-objs += ../spi/spi-xilinx.o

nfb-objs += mi/mi.o
nfb-objs += ndp/ctrl.o ndp/ctrl_ndp.o ndp/channel.o ndp/subscription.o ndp/subscriber.o ndp/ring.o ndp/char.o ndp/driver.o ndp/kndp.o
nfb-objs += ndp_netdev/core.o
nfb-objs += boot/flash.o boot/reload.o boot/boot.o boot/gecko.o boot/sdm.o
nfb-objs += ../mfd/intel-m10-bmc-core.o ../fpga/fpga-image-load.o ../fpga/intel-m10-bmc-sec-update.o ../hwmon/intel-m10-bmc-hwmon.o ../mfd/intel-m10-bmc-log.o boot/nfb-pmci.o
nfb-objs += net/driver.o net/device.o net/ethtool.o net/sysfs.o
nfb-objs += qdr/qdr.o
nfb-objs += misc.o lock.o bus.o char.o pci.o core.o
nfb-objs += hwmon/nfb_hwmon.o

obj-m += nfb.o
