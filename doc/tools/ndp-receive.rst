.. _ndp_receive:

ndp-receive
------------

Read packets from RX queue and write them to PCAP file.

The PCAP file must be set with argument **-f**.
In case of multi queue run, separate PCAP file is used for each queue (queue index is appended to filename).

There is three variants for capturing timestamp:

  - Ignore it (use zero for timestamp value)
  - Use system time sampled at moment of receiving packet in the tool (**-t system**).
  - Use value from packet header typically inserted by the TSU in firmware. This is most accurate method.
    Bit position of the 64b timestamp value in packet header must be entered, the position is typically zero (**-t header:0**).
    It is also necessary to run the :ref:`nfb-tsu<nfb_tsu>` tool.

Content of stored packet can be trimmed to a maximum size specified with **-r** argument.
