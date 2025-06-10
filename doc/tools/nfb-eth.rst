.. _nfb_eth:

nfb-eth
-------

Tool for configuring network interfaces (TX/RX MACs), displaying statistics and parameters of TX/RX lines.

It consists from three sections:

MAC section
~~~~~~~~~~~

As a default action, the packet statistics and link status are reported for both RX and TX MAC.
Specific direction (RX/TX) can be selected with the **-r** or **-t** argument.

MAC components must be enabled before use (**-e 1**), otherwise the network traffic doesn't pass through.
This is necessary especially for TX as the DMA/application core typically get stuck when the TX MAC doesn't accept the data stream (TX MAC doesn't drop them when disabled).

The tool can configure the maximum and minimum allowed packet length (**-L** and **-l** arguments)
and MAC address based filtration (**-M**: *add*, *remove*, *show*, *clear, *fill* commands operates on MAC address table, *normal* (unicast), *broadcast*, *multicast*, and *promiscuous* sets the required mode).

PMA/PCS section
~~~~~~~~~~~~~~~

The **-P** argument is intended to configure Ethernet modes.
If no command is entered, PMA and PCS status is printed, including PMA type, speed and link status.

Currently used PMA type can be configured with **-c** argument whereas the exact PMA identification string must be entered.
List of PMA types can vary with used firmware/hardware and can be obtained in verbose output (**-v** argument),
together with list of PMA and PCS features.

The PMA/PCS features that can be activated with **-c** argument as well.
However the feature can be activated and deactivated with *+* or *-* prefixes of its name.

For example:

- ``nfb-eth -Pv`` prints all supported PMA types/modes and features,
- ``nfb-eth -Pc 100GBASE-LR4`` sets the PMA to 100GBASE-LR4 type,
- ``nfb-eth -Pc "+PMA local loopback"`` enables local loopback feature on all PMAs.
- ``nfb-eth -Pc -i 0 "+PCS reverse loopback"`` enables remote loopback feature on the first PCS.

Transceiver section
~~~~~~~~~~~~~~~~~~~

Prints transceiver information (if plugged) obtained from port management interface (MDIO or I2C),
including vendor, compliance, temperature, signal strength and more.


JSON output
~~~~~~~~~~~

.. code-block:: python

    {
        "eth": {                                # list of selected/all Ethernet channels
            [
                "id": 1,                        # ID of this ethernet channel in firmware
               #"speed": 100000,                # speed (in Mbps)
                "speed_str": "100 Gb/s",        # speed (string)
                "transceiver_id": 0,            # ID of used transceiver (cage)
                "transceiver_lanes": [          # list of electrical lane numbers used in transceiver for this Ethernet channel
                    0, 1, 2, 3
                ],

                "rxmac": {                      # processing unit for incoming frames; only present for nfb-eth tool in RxMAC or MAC mode
                    "enabled": False,           # receive enablement
                    "link_up": False,           # link is estabilished for receive
                    "stats": {                  # base statistics
                        "total": 0,             # total number of frames received from Ethernet (including bad frames)
                        "total_octets": 0,      # total number of octets received from Ethernet (including bad frames)
                        "pass": 0,              # number of frames passed to firmware application
                        "pass_octets": 0,       # number of octets passed to firmware application
                        "drop": 0,              # number of dropped frames (drop == total - pass)
                        "drop_disabled": 0,     # number of frames dropped due to disabled RxMAC state
                        "drop_filtered": 0,     # ... due to MAC address filter
                        "drop_overflow": 0,     # ... due to backpressure from firmwarove application (incl. backpressure from DMA)
                        "drop_err": 0,          # ... due to one or more errors (drop_err <= drop_err_len + drop_err_crc + drop_err_mii)
                        "drop_err_len": 0,      # ... due to frame length out of configured range
                        "drop_err_crc": 0,      # ... due to cyclic redundancy check error
                        "drop_err_mii": 0,      # ... due to bad framing on MII
                    },
                    "etherstats": {             # extended stats based on RFC 1271; only present for nfb-eth tool in etherStats mode
                        "octets": 0,            # = rxmac.stats.total_octets
                        "pkts": 0,              # = rxmac.stats.total
                        "broadcast": 0,
                        "multicast": 0,
                        "crc_align_errors": 0,
                        "under64": 0,           # RFC 1271 name: "etherStatsUndersize"
                        "over1518": 0,          # RFC 1271 name: "etherStatsOversize"
                        "fragments": 0,
                        "jabbers": 0,
                        "pkts64octets": 0,
                        "pkts65to127octets": 0,
                        "pkts128to255octets": 0,
                        "pkts256to511octets": 0,
                        "pkts512to1023octets": 0,
                        "pkts1024to1518octets": 0,
                        "pkts1519to2047octets": 0,
                        "pkts2048to4095octets": 0,
                        "pkts4096to8191octets": 0,
                        "oversize": 0,          # number of frames with length over last bin resolution (not RFC 1271 name "oversize")
                        "conf_undersize": 0,    # number of frames shorter than minimal configured length (rxmac.config.min_length)
                        "conf_oversize": 0      # number of frames longer than maximal configured length (rxmac.config.max_length)
                    },
                    "config": {
                        "err_mask_frame_err": True,     # enable drop due to framing error
                        "err_mask_crc_check": True,     # enable drop due to cyclic redundancy check error
                        "err_mask_min_length": True,    # enable drop due to frame length shorter than minimal configured length
                        "err_mask_max_length": True,    # enable drop due to frame length shorter than maximal configured length
                        "err_mask_mac_addr_check": True,# enable drop due to MAC address filter
                        "err_mask_mac_addr_mode": "promiscuous", # MAC address filter mode (promiscuous, normal, broadcast, multicast)
                        "cfg_min_length": 64,           # configured minimal frame length
                        "cfg_max_length": 1526,         # configured maximal frame length
                        "max_length_capable": 4096,     # maximal frame length handled by this RxMAC (max_length_capable >= max_length)
                        "mac_addr_count": 16            # maximal number of configurable MAC addresses
                    }
                },
                "txmac": {                      # processing unit for outgoing frames; only present for nfb-eth tool in TxMAC or MAC mode
                    "enabled": False,           # transmit enablement
                    "stats": {                  # base statistics
                        "total": 0,             # total number of frames received from firmware application
                       #"total_octets": 0,      # total number of octets received from firmware application
                        "pass": 0,              # total number of sent frames on Ethernet
                        "pass_octets": 0,       # total number of sent octets on Ethernet
                        "drop": 0,              # total number of dropped frames
                        "drop_disabled": 0,     # total number of frames dropped due to disabled TxMAC state (txmac.disabled == True)
                        "drop_link_down": 0,    # ... due to not estabilished link
                       #"drop_err": 0,          # ... due to an error (currently: drop_err == drop_err_len)
                        "drop_err_len": 0       # ... due to frame lenght out of range
                    }
                },
                "pma": {                        # physical medium attachment; only present for nfb-eth tool in PCS/PMA mode
                    "link_status_latch": True,  # PMA link is estabilished all the time from previous status read
                    "link_status": True,        # PMA link is estabilished
                    "speed_str": "100 Gb/s",
                    "transmit_fault": False,
                    "receive_fault": True,
                    "type": "100GBASE-LR4",     # current link mode
                    "available_types": [        # list of all link modes supported in firmware
                        "100GBASE-SR4",
                        ...
                    ],
                    "available_features": [     # list of all features supported in firmware
                        {
                            "active": False,
                            "name": "PMA local loopback"
                        },
                        ...
                    ]
                },
                "rsfec": {                      # Reed-Solomon Forward Error Correction; only present for nfb-eth tool in PCS/PMA mode and for PMA type 100GBASE-SR4 or similar (clause 91 and 108)
                    "high_ser": False,
                    "lanes_aligned": False,
                    "pcs_lanes_aligned": True,
                    "corrected_cws": 0,
                    "uncorrected_cws": 0,
                    "symbol_errors": [
                        0,
                        ...
                    ],
                    "lane_map": [
                        0,
                        ...
                    ],
                    "am_lock": [
                        False,
                        ...
                    ]
                },
                "pcs": {                        # physical coding sublayer; only present for nfb-eth tool in PCS/PMA mode
                    "link_status_latch": False,
                    "link_status": False,
                    "speed_str": "100 Gb/s",
                    "transmit_fault": False,
                    "receive_fault": True,
                    "available_features": [
                        {
                            "active": False,
                            "name": "PCS reverse loopback"
                        }
                    ],
                    "global_block_lock_latch": False,
                    "global_block_lock": False,
                    "global_high_ber_latch": False,
                    "global_high_ber": False,
                    "ber_counter": 0,
                    "error_blocks": 0,
                    "lanes_aligned": False,
                    "block_lock": [
                        False,
                        ...
                    ],
                    "am_lock": [
                        False,
                        ...
                    ],
                    "lane_map": [
                        0,
                        ...
                    ],
                    "bip_error_counters": [
                        0,
                        ...
                    ],
                    "rsfec_cl119": {                  # Reed-Solomon Forward Error Correction; only present for nfb-eth tool in PCS/PMA mode and for PMA type 200/400G or similar (clause 119)
                        "high_ser": False,
                        "degraded_ser": False,
                        "remote_degraded_ser": False,
                        "local_degraded_ser": False,
                        "corrected_cws": 19063599,
                        "uncorrected_cws": 0,
                        "symbol_errors": [
                            121928,
                            ...
                        ]
                    }
                }
            ]
        }
    }
