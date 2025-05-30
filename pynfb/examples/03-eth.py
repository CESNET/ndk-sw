#!/usr/bin/env python3

############################################
# Python NFB API examples - Ethernet ports #
############################################

# TOC
#####

# 3A Single Ethernet port manipulation
# 3B PCS/PMA management and Transceiver manipulation
# 3C Shortcuts for Ethernet ports manipulation

import nfb

dev = nfb.open()

# 3.A Single Ethernet port manipulation
######################################

# Ethernet port representer
eth = dev.eth[0]

# enable or disable RxMAC / TxMAC
for mac in (eth.rxmac, eth.txmac):
    mac.enable()
    mac.enable(False)
    mac.disable()

# check for link status
link_ready = eth.rxmac.is_link()

# read RxMAC / TxMAC statistic counters
# rx_stats == {'total': 0, 'passed': 0, 'passed_bytes': 0, 'dropped': 0, 'overflowed': 0, 'errors': 0}
rx_stats = eth.rxmac.read_stats()

# tx_stats == {'total': 0, 'total_bytes': 0, 'passed': 0, 'dropped': 0, 'errors': 0}
tx_stats = eth.txmac.read_stats()

# reset RxMAC / TxMAC statistic counters
for mac in (eth.rxmac, eth.txmac):
    mac.reset_stats()


# 3.B PCS/PMA management and Transceiver manipulation
####################################################

# Enable local PMA loopback
eth.pcspma.pma_local_loopback = True

# disable loopback in register 1.7
eth.pcspma.mdio.write(1, 7, 0)
# read 16b value from register 1.7: val = 0
val_b = eth.pcspma.mdio.read(1, 7)

pmd = eth.pmd
pmd.is_present() in [True, False]

# val = "FINISAIR CORP."
val = pmd.read_vendor_name()
val = pmd.read_vendor_pn()
val = pmd.read_vendor_sn()
if hasattr(pmd, "mdio"):
    val_i = pmd.mdio.read(3, 0)
elif hasattr(pmd, "i2c"):
    val_i = pmd.i2c.read_reg(148, 16)


# 3.C Shortcuts for Ethernet ports manipulation
##############################################

# shortcuts to both RxMAC + TxMAC functions
eth.enable(False)
eth.reset_stats()

# rxmac link status shortcut
assert eth.is_link() == eth.rxmac.is_link()


# all Ethernet ports
dev.eth.enable()
# selected Ethernet ports
dev.eth.enable(i=[0, 1])
dev.eth.enable(False, i=0)
dev.eth.reset_stats()
