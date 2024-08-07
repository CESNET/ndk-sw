#!/usr/bin/python3

#############################################
# Python NFB API examples - quick reference #
#############################################

# 1.A Basic device manipulation
import nfb

dev = nfb.open(path="0")

# 1.B Device Tree
node = dev.fdt_get_compatible("netcope,eth")[0]
node = dev.fdt_get_phandle(node.get_property("rxmac").value)

# 1.C Component manipulation
comp = dev.comp_open(node)
comp = dev.comp_open("netcope,rxmac", index=0)

comp.write16(0x04, 0xFFFF)
comp.write(0x12, bytes([0xDE, 0xAD, 0xBE]))

val = comp.read64(0x04)
val = comp.read(0x12, 3)

# 2.A Data transmission
ndp = dev.ndp
rxq = ndp.rx[0]
txq = ndp.tx[0]

rxq.start()
txq.start()

pkt = bytes([0] * 64)
hdr = bytes([0] * 16)
msg = (pkt, hdr, 0)

txq.send([pkt] * 2, hdrs=[hdr, hdr + hdr], flags=0)
txq.sendmsg(msg)

pkts = rxq.recv(cnt=1, timeout=1)
assert len(pkts) == 0 or (len(pkts) == 1 and isinstance(pkts[0], bytes))

msgs = rxq.recvmsg(cnt=1, timeout=0.5)
assert len(msgs) == 0 or (isinstance(msgs[0], tuple) and len(msgs[0]) == 3)

# 2.B Data transmission on multiple queues
ndp.send(pkt, i=[0, 1, 2])
ndp.sendmsg([msg], flush=False)
ndp.flush()

pkts_q = ndp.recv(cnt=1, i=0, timeout=0.5)
msgs_q = ndp.recvmsg(cnt=1, i=0, timeout=1)

# 2.C NDP queues statistics
txq.reset_stats()
assert txq.read_stats()["passed"] == 0

# 3.A Single Ethernet port manipulation
eth = dev.eth[0]
link_ready = eth.rxmac.link

for mac in [eth.rxmac, eth.txmac]:
    mac.enable(enable=True)
    mac.disable()
    mac.reset_stats()
    assert mac.read_stats()["total"] == 0

# 3.B PCS/PMA management and Transceiver manipulation
eth.pcspma.pma_local_loopback = True

val = eth.pmd.vendor_name
eth.pcspma.mdio.write(1, 0, 1)
if hasattr(eth.pmd, 'i2c'):
    val = eth.pmd.i2c.read_reg(1, 0)

# 3.C Aggregated Ethernet ports manipulation
assert eth.is_link() == eth.rxmac.is_link()

dev.eth.enable(enable=False, i=1)
dev.eth.disable(i=[0, 1])
dev.eth.stats_reset()
