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


Board name / card name
~~~~~~~~~~~~~~~~~~~~~~

- Board name is a label assembled by the software driver
- Card name is a label embedded in the firmware

Board name can vary with specific modifications/configurations of the base hardware, for example:
Card-name "100G2" has two variants with board-name "100G2Q", "100G2C".
The firmware should be safe-to-use between different board-names,
although it can have limited functionality.

On the other side, card-name label should distinguish between
hardware with non-compatible firmware, for example:
The "400G1" card has currently two revision, but the firmware is
incompatible between them. Therefore it is necessary to have two
different card-name labels.

:ref:`nfb-boot<nfb_boot>` compares card-name labels and ensures and refuses to flash
firmware with unequal card-name.
