#!/usr/bin/python3

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
for mac in [eth.rxmac, eth.txmac]:
    mac.enable()
    mac.enable(False)
    mac.disable()

# check for link status
link_ready = eth.rxmac.link

# read RxMAC / TxMAC statistic counters
# rx_stats == {'packets': 0, 'received': 0, 'discarded': 0, 'overflowed': 0, 'octets': 0}
rx_stats = eth.rxmac.stats_read()

# tx_stats == {'packets': 0, 'octets': 0, 'discarded': 0, 'sent': 0}
tx_stats = eth.txmac.stats_read()

# reset RxMAC / TxMAC statistic counters
for mac in [eth.rxmac, eth.txmac]:
    mac.stats_reset()


# 3.B PCS/PMA management and Transceiver manipulation
####################################################

# Enable local PMA loopback
eth.pcspma.pma_local_loopback = True

# disable loopback in register 1.7
eth.pcspma.mdio.write(1, 7, 0)
# read 16b value from register 1.7: val = 0
val = eth.pcspma.mdio.read(1, 7)

# None if transceiver isn't present, else an object
pmd = eth.pmd
# getters, e.g: val = "FINISAIR CORP."
val = pmd.vendor_name
val = pmd.vendor_pn
val = pmd.vendor_sn
if hasattr(pmd, "mdio"):
    val = pmd.mdio.read(3, 0)
elif hasattr(pmd, "i2c"):
    val = pmd.i2c.read_reg(148, 16)


# 3.C Shortcuts for Ethernet ports manipulation
##############################################

# shortcuts to both RxMAC + TxMAC functions
eth.enable(False)
eth.stats_reset()

# rxmac link status shortcut
assert eth.link == eth.rxmac.link


# all Ethernet ports
dev.eth.enable()
# selected Ethernet ports
dev.eth.enable(i=[0, 1])
dev.eth.enable(False, i=0)
dev.eth.stats_reset()
