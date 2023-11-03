#!/usr/bin/python3

####################################
# Python NFB API examples - basics #
####################################

# TOC
#####

# 1A Basic device manipulation
# 1B Device Tree
# 1C Component manipulation


# 1.A Basic device manipulation
##############################

import nfb

# get main device handle
dev = nfb.open()
dev = nfb.open("0")
dev = nfb.open("/dev/nfb0")


# 1.B Device Tree
################

# for DT access is used external fdt module: https://pypi.org/project/fdt/
fdt = dev.fdt
print(fdt.info())

# get all nodes for specific compatible string
nodes = dev.fdt_get_compatible("netcope,eth")

# access to value of node property
phandle = nodes[0].get_property("rxmac").value
assert isinstance(phandle, int)

node = dev.fdt_get_phandle(phandle)

# show full path of node in DT
print(node.path + "/" + node.name)


# 1.C Component manipulation
###########################

# get component handle

# use fdt node directly
comp = dev.comp_open(node)
# use compatible string, default index of compatible component in DT is 0
comp = dev.comp_open("cesnet,ofm,mi_test_space")
# use specific index of compatible component in DT
comp = dev.comp_open("cesnet,ofm,mi_test_space", 0)


# write access
# ------------

# write 16b integer value into component register on offset 0
# variants: write8, write16, write32, write64
comp.write16(0, 0x1234)

# write 3 bytes into component register on offset 1
# on 32b MI bus it realizes one write cycle to
# address 0 with BE=int('1110', 2)
comp.write(1, bytes([0, 1, 2]))


# read access
# -----------

# read 32b integer value: ret = 0xdeadcafe
# variants: read8, read16, read32, read64
print(comp.read32(0))

# read 3 bytes on offset 18: ret = bytes([0xff, 0xff, 0xff])
# on 32b MI bus it realizes two consecutive read cycles to
# address 16 with BE=int('1100', 2) and
# address 20 with BE=int('0001', 2)
val = comp.read(0x12, 3)
