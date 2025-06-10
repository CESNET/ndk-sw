.. _nfb_info:

nfb-info
=========

Tool for obtaining basic informations about the card and firmware that is currently booted in the FPGA.

It shows board name and its serial number,
firmware project name and build details, number of RX/TX DMA queues and number of Ethernet channels.
It also shows PCIe endpoint configuration (slot number, speed and link width) and their NUMA node.

.. tip::
   Verbose mode shows also temperature of primary FPGA.

Supported queries are:

- *project*:  Project name
- *project-version*:  Project version
- *build*:    Firmware compilation time
- *rx*:       Number of receive DMA queues
- *tx*:       Number of transmit DMA queues
- *ethernet*: Number of ethernet channels
- *port*:     Number of ethernet ports
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


JSON output
~~~~~~~~~~~

.. code-block:: python

    {                                   # only present in default mode
        {
         "board": {
            "board_name": "FB1CGG",
            "serial_number": "317",
            "network_interfaces": 2,
            "interfaces": [             # only present in verbose mode
                {
                    "id": 0,
                    "type": "QSFP"
                },
                {
                    "id": 1,
                    "type": "QSFP"
                }
            ],
            "temperature": 54.2         # only present in verbose mode
        },
        "firmware": {
            "card_name": "FB2CGG3",
            "project_name": "NDK_HFT",
            "project_variant": "10G2",
            "project_version": "0.2.1",
            "build_time": "2024-04-04 15:38:44",
            "build_tool": "Vivado v2022.2 (64-bit)",
            "build_author": "cabal@cesnet.cz",
            "rx_queues": 16,            # all RX queues in firmware
            "rx_queues_available": 16,  # only usable RX queues (e.g. corresponding PCI endpoint is connected)
            "tx_queues": 16,
            "tx_queues_available": 16,
            "eth_channels": [           # only present in verbose mode
                {
                    "id": 0,
                    "type": "10G"
                }
            ]
        },
        "system": {
            "pci": [                    # list of PCI endpoints
                {
                    "id": 0,
                    "pci_bdf": "0000:03:00.0",
                    "pci_link_speed_str": "8 GT/s",
                    "pci_link_width": 8,
                    "numa": 0,
                    "bar": [
                        {
                            "id": 0,
                            "size": 67108864
                        },
                        {
                            "id": 2,
                            "size": 16777216
                        }
                    ]
                }
            ]
        }
    }

    [                                   # only present in card list mode
        {
            "id": 2,
            "path": "/dev/nfb2",
            "pci_bdf": "0000:81:00.0",
            "card_name": "FB2CGHH",
            "serial_number": "144",
            "project_name": "NDK_MINIMAL",
            "project_variant": "100G2",
            "project_version": "0.5.6"
        },
        ...
    ]
