#!/usr/bin/python3

###########################################
# Python NFB API examples - data transfer #
###########################################

# TOC
#####

# 2A Data transmission on specific queue
# 2B Data transmission on multiple queues
# 2C Queues statistics

import nfb

dev = nfb.open()
for e in dev.eth:
    e.enable()
    e.pma_local_loopback = True

# 2.A Data transmission on specific queue
#########################################

# NFB data plane representer
ndp = dev.ndp
rxq = ndp.rx[0]
txq = ndp.tx[0]

# start the queue; this step is optional
# for loopback test purposes helps to avoid discarding on RxQ
rxq.start()
txq.start()

# Raw frames are represented by bytes
pkt = bytes([0] * 64)
hdr = bytes([0] * 16)


# Transmitting frames
# -------------------

# send 64B frame
txq.send(pkt)
# write two 64B frames into buffer
txq.send([pkt] * 2, flush=False)
# send the pending frames
txq.flush()

# send two 64B frame with the same 16B header
txq.send([pkt] * 2, hdrs=hdr)
# send two 64B frame, one with 16B header, one with 32B header
txq.send([pkt] * 2, hdrs=[hdr, hdr + hdr], flags=0)

# send a frame from prepared tuple
msg = (pkt, hdr, 0)
txq.sendmsg([msg])


# Receiving frames
# ----------------

# receive all pending frames
pkts = rxq.recv()
assert len(pkts) == 0 or isinstance(pkts[0], bytes)

# receive all pending frames
# if no frame is available, wait for incoming frames maximally 0.5 sec
pkts = rxq.recv(timeout=0.5)

# try to receive specific count of frames
# returned list can be smaller, if there is not enough pending frames
pkts = rxq.recv(cnt=1)

# try to receive specific count of frames
# if not enough frames is available, wait for incoming frames maximally 2 secs
pkts = rxq.recv(cnt=1, timeout=2)

# receive specific count of frames, wait indefinitely
#rxq.recv(cnt=1, timeout=None)

# receive list of messages
# message is a tuple(pkt: bytes, hdr: bytes, flags: int)
msgs = rxq.recvmsg()
assert len(msgs) == 0 or (isinstance(msgs[0], tuple) and len(msgs[0]) == 3)


# 2.B Data transmission on multiple queues
##########################################

# Transmitting frames
# -------------------

# send 64B frame to all queues
ndp.send(pkt)
# send 64B frame to first three queues
ndp.send(pkt, i=[0, 1, 2])
# write 64B frame into buffer of all queues
ndp.send(pkt, flush=False)
# send pending frames on all queues
ndp.flush()

# send a frame from prepared tuple on all queues
ndp.sendmsg(msg)


# Receiving frames
# ----------------

# receive pending frames from all queues
# return value is list of tuple(pkt: bytes, queue_index: int)
pkts = ndp.recv()
assert len(pkts) == 0 or (
    isinstance(pkts[0], tuple) and len(pkts[0]) == 2 and isinstance(pkts[0][0], bytes)
)

# receive pending frames on specific queue
ndp.recv(i=1)
# receive pending frames on first three queues
ndp.recv(i=[0, 1, 2])
# receive specific count of frames from any queues, wait indefinitely
#ndp.recv(timeout=None)

# receive pending messages from all queues, return value is list of tuple(tuple(pkt: bytes, hdr: bytes, flags: int), queue_index: int)
msgs = rxq.recvmsg()
assert len(msgs) == 0 or (
    isinstance(msgs[0], tuple)
    and len(msgs[0]) == 2
    and isinstance(msgs[0][0][0], bytes)
)


# 2.C Queues statistics
#######################

try:
    # stats == {'packets': 4, 'bytes': 276}
    stats = txq.stats_read()

    # Reset Queue statistics
    txq.stats_reset()
except:
    pass
