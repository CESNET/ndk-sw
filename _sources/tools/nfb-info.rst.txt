.. _nfb_info:

nfb-info
---------

Tool for obtaining basic informations about the card and firmware that is currently booted in the FPGA.

It shows board name and its serial number,
firmware project name and build details, number of RX/TX DMA queues and number of Ethernet channels.
It also shows PCIe endpoint configuration (slot number, speed and link width) and their NUMA node.

.. tip::
   Verbose mode shows also temperature of primary FPGA.

Supported queries are:

- *project*:  Project name
- *build*:    Firmware compilation time
- *rx*:       Number of receive DMA queues
- *tx*:       Number of transmit DMA queues
- *ethernet*: Number of ethernet channels and their types
- *card*:     Card name
- *pci*:      PCI slot number of the primary endpoint
- *numa*:     Numa node of the primary endpoint

