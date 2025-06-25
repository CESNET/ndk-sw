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

nfb-y += ../fdt/fdt.o ../fdt/fdt_ro.o ../fdt/fdt_rw.o ../fdt/fdt_wip.o ../fdt/fdt_sw.o ../fdt/fdt_empty_tree.o
nfb-y += ../cfi/cfi_cmdset_0001.o ../cfi/cfi_cmdset_0002.o ../cfi/cfi_cmdset_0020.o ../cfi/cfi_probe.o ../cfi/cfi_util.o ../cfi/chipreg.o ../cfi/gen_probe.o ../cfi/mtdcore.o
nfb-y += ../spi-nor/spi-nor.o
nfb-y += ../spi/spi-xilinx.o

nfb-y += mi/mi.o
nfb-y += ndp/ctrl.o ndp/ctrl_ndp.o ndp/channel.o ndp/subscription.o ndp/subscriber.o ndp/ring.o ndp/char.o ndp/driver.o ndp/kndp.o
nfb-y += ndp_netdev/core.o
nfb-y += boot/flash.o boot/reload.o boot/boot.o boot/gecko.o boot/sdm.o boot/bw-bmc.o
ifndef CONFIG_REGMAP_SPI_AVMM
nfb-y += ../base/regmap/regmap-spi-avmm.o
endif
ifndef CONFIG_SPI_ALTERA_CORE
nfb-y += ../spi/spi-altera-core.o
endif
nfb-y += ../mfd/intel-m10-bmc-core.o ../fpga/fpga-image-load.o ../fpga/intel-m10-bmc-sec-update.o ../hwmon/intel-m10-bmc-hwmon.o ../mfd/intel-m10-bmc-log.o boot/nfb-pmci.o boot/nfb-spi.o
nfb-y += net/driver.o net/device.o net/ethtool.o net/sysfs.o
nfb-y += qdr/qdr.o
nfb-y += misc.o lock.o bus.o char.o pci.o core.o
nfb-y += hwmon/nfb_hwmon.o

ccflags-$(CONFIG_NFB_XDP) += -DCONFIG_NFB_ENABLE_XDP
nfb-$(CONFIG_NFB_XDP) += xdp/driver.o xdp/ethdev.o xdp/ctrl_xdp_common.o xdp/ctrl_xdp_pp.o xdp/ctrl_xdp_xsk.o xdp/channel.o xdp/sysfs.o

obj-m += nfb.o
