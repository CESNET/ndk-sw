Quick start
==================

These steps describes basic workflow for experiments.

Boot new firmware into card
---------------------------

.. note::

    The nfb-boot tool expects that some NDK firmware is already loaded in the FPGA. If this is not your case, follow the FPGA card manufacturer’s instructions for loading the FPGA firmware.

.. code-block:: console

    $ nfb-boot -f0 n6010-nic-230603.nfw

Check base board information
----------------------------

.. code-block:: console

    $ nfb-info -v
    --------------------------------------- Board info ----
    Board name                 : COMBO-GENERIC
    Serial number              : 0
    Network interfaces         : 2
     * Interface 0             : QSFP
     * Interface 1             : QSFP
    Temperature                : 56.2 C
    ------------------------------------ Firmware info ----
    Card name                  : N6010
    Project name               : NDK_NIC
    Project variant            : 100G2
    Project version            : 0.3.2
    Built at                   : 2023-06-03 00:30:22
    Build tool                 : Quartus Version 22.4.0 Build 94 12/07/2022 SC Pro Edition
    Build author               : no-reply@liberouter.org
    RX queues                  : 16
    TX queues                  : 16
    ETH channels               : 2
     * Channel 0               : 100G
     * Channel 1               : 100G
    -------------------------------------- System info ----
    PCIe Endpoint 0:
     * PCI slot                : 0000:03:00.0
     * PCI speed               : 8 GT/s
     * PCI link width          : x16
     * NUMA node               : -1

Write and read CSR (configuration and status registers)
-------------------------------------------------------

.. code-block:: console

    $ # write 32b value 0xdeadbeef to address 0x1c
    $ nfb-bus -c cesnet,ofm,mi_test_space 1c deadbeef
    $
    $ # read 32b value from address 0x1c
    $ nfb-bus -c cesnet,ofm,mi_test_space 1c
    deadbeef

Check ethernet modes and features
---------------------------------

.. code-block:: console

    $ nfb-eth -i0 -Pv |grep -A 7 "PMA types"
    Supported PMA types ->
     *                         : 100GBASE-SR4
     * [active]                : 100GBASE-LR4
     *                         : 100GBASE-ER4
    Supported PMA features ->
     *                         : PMA local loopback
     *                         : PMA remote loopback
     *                         : Low power

Enable input + output MAC, enable FEC and set internal loopback
--------------------------------------------------------------------

.. code-block:: console

    $ nfb-eth -e1
    $ nfb-eth -Pc "100GBASE-SR4"
    $ nfb-eth -Pc "+PMA local loopback"

Send 2x10 random frames (without payload) on first two DMA queues
-----------------------------------------------------------------

.. code-block:: console

    $ ndp-generate -s 64-128 -i0,1 -p 10
    ------------------------------- NDP generate stats ----
    Packets                    :                   20
    Bytes                      :                 2112
    Avg speed [Mpps]           :                    0.001
    Avg speed L1 [Mb/s]        :                    0.747
    Avg speed L2 [Mb/s]        :                    0.631
    Time                       :                    0.028

Check statistics on DMA queues and MAC
--------------------------------------

.. code-block:: console

    $ nfb-dma -i0,1
    ------------------------------ RX00 NDP controller ----
    Received                   : 0
    Received bytes             : 0
    Discarded                  : 20
    Discarded bytes            : 2752

    ------------------------------ RX01 NDP controller ----
    Received                   : 0
    Received bytes             : 0
    Discarded                  : 0
    Discarded bytes            : 0

    ------------------------------ TX00 NDP controller ----
    Sent                       : 10
    Sent bytes                 : 1056

    ------------------------------ TX01 NDP controller ----
    Sent                       : 10
    Sent bytes                 : 1056

.. code-block:: console

    $ nfb-eth -i0
    ----------------------------- Ethernet interface 0 ----
    Speed                      : 100 Gb/s
    Transceiver status         : Not plugged
    Transceiver cage           : QSFP-0
    ------------------------------------- RXMAC Status ----
    RXMAC status               : ENABLED
    Link status                : UP
    HFIFO overflow occurred    : False
    Received octets            :                 2192
    Processed                  :                   20
    Received                   :                   20
    Erroneous                  :                    0
    Overflowed                 :                    0
    ------------------------------------- TXMAC Status ----
    TXMAC status               : ENABLED
    Transmitted octets         :                 2192
    Processed                  :                   20
    Transmitted                :                   20
    Erroneous                  :                    0
    Repeater status            : Unknown
