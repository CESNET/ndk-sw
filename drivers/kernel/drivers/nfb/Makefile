# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
#
# NFB driver Makefile
#
# Copyright (C) 2017-2022 CESNET
#
# Author(s):
#   Martin Spinler <spinler@cesnet.cz>

include $(DRIVER_TOPDIR)/Makefile.conf

ccflags-y := $(CUSTOM_CFLAGS)

nfb-objs += ../fdt/fdt.o ../fdt/fdt_ro.o ../fdt/fdt_rw.o ../fdt/fdt_wip.o ../fdt/fdt_sw.o ../fdt/fdt_empty_tree.o
nfb-objs += ../cfi/cfi_cmdset_0001.o ../cfi/cfi_cmdset_0002.o ../cfi/cfi_cmdset_0020.o ../cfi/cfi_probe.o ../cfi/cfi_util.o ../cfi/chipreg.o ../cfi/gen_probe.o ../cfi/mtdcore.o
nfb-objs += ../spi-nor/spi-nor.o
nfb-objs += ../spi/spi-xilinx.o

nfb-objs += mi/mi.o
nfb-objs += ndp/ctrl.o ndp/ctrl_ndp.o ndp/channel.o ndp/subscription.o ndp/subscriber.o ndp/ring.o ndp/char.o ndp/driver.o ndp/kndp.o
nfb-objs += ndp_netdev/core.o
nfb-objs += boot/flash.o boot/reload.o boot/boot.o boot/gecko.o boot/sdm.o boot/bw-bmc.o
ifndef CONFIG_REGMAP_SPI_AVMM
nfb-objs += ../base/regmap/regmap-spi-avmm.o
endif
ifndef CONFIG_SPI_ALTERA_CORE
nfb-objs += ../spi/spi-altera-core.o
endif
nfb-objs += ../mfd/intel-m10-bmc-core.o ../fpga/fpga-image-load.o ../fpga/intel-m10-bmc-sec-update.o ../hwmon/intel-m10-bmc-hwmon.o ../mfd/intel-m10-bmc-log.o boot/nfb-pmci.o boot/nfb-spi.o
nfb-objs += net/driver.o net/device.o net/ethtool.o net/sysfs.o
nfb-objs += qdr/qdr.o
nfb-objs += misc.o lock.o bus.o char.o pci.o core.o
nfb-objs += hwmon/nfb_hwmon.o

ccflags-$(CONFIG_NFB_XDP) += -DCONFIG_NFB_ENABLE_XDP
nfb-$(CONFIG_NFB_XDP) += xdp/driver.o xdp/ethdev.o xdp/ctrl_xdp_common.o xdp/ctrl_xdp_pp.o xdp/ctrl_xdp_xsk.o xdp/channel.o xdp/sysfs.o

obj-m += nfb.o
