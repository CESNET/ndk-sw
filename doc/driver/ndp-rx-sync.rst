.. _ndp_rx_sync:

NDP RX synchronization
-------------------------------

When receiving data (RX DMA direction), the driver first tells the DMA Module, where in the computer's RAM the data can be stored.
After doing so, the DMA Module informs the driver about how much data has been received.
The driver passes the data to the user and passes more space to the DMA Module.

Function call map
^^^^^^^^^^^^^^^^^

This section contains a cheatsheet of how functions call each other when using DPDK, Libnfb and NDP driver.
Each of these parts uses different structure to contain a block of memory space for packet.
Between the DMA Module and the NDP driver "descriptors" are used.
Descriptors are optimized for minimal PCI overhead.
The NDP driver and the Libnfb pass these infromation using the "Header Buffer" and "Offset Buffer" (the two buffers share control and thus function as a single buffer).
The buffers and the pointers to these buffers are accessible both to the NDP driver and (through ``vmap``) to the Libnfb.
This is basically the only way these two sides communicate.
Otherwise they run independently in parallel.

The Libnfb comunicates with the user using the "ndp_packet" structure.
In DPDK memory blocks are managed using the structure called "Mbuf".

RX
==

.. code-block:: text
   :linenos:

    rte_eth_rx_burst (dpdk/lib/librte_ethdev/rte_ethdev.h)
        -+
         |
         V
         nfb_eth_ndp_rx (dpdk/drivers/net/nfb/nfb_rx.h)
             - is set as dev->rx_pkt_burst
             - main RX receive function
             ==============-+
                            |
                            V
                            (nc_)ndp_(v2_)rx_burst_get (swbase/libnfb/include/netcope/ndp_rx.h)
                                - takes values from headers in the Offset Buffer and copies them to an ndp_packet
                                - shifts rhp
             ==========-+
                        |
                        V
                        ndp_rx_fill_mbuf (dpdk/drivers/net/nfb/nfb_rx.h)
                            - copies information from ndp_packet to an Mbuf Buffer
                            - copies each Mbuf to output Mbuf array (from here it will be freed by the user)
             -+
              |
              V
              ndp_rx_fill_desc (dpdk/drivers/net/nfb/nfb_rx.h)
                  - allocates a requested amount of Mbufs in the Mbuf Buffer
                  - fills ndp_packets with pointers to the Mbufs' data
                  -+
                   |
                   V
                   (nc_)ndp_rx_burst_put_desc (swbase/libnfb/include/netcope/ndp_rx.h)
                       - checks the amount of free space in the Offset Buffer
                       - copiest info from ndp_packets to Header and Offset Buffer
                       - shifts sync.swptr

    ndp_channel_rxsync (drivers/kernel/drivers/nfb/ndp/channel.c)
        -+
         |
         V
         ndp_ctrl_rx_set_swptr (swbase/drivers/kernel/drivers/nfb/ndp/ctrl_ndp.c)
             - is set as ndp_ctrl_rx_ops.set_swptr
             - shifts shp
             - pro SIMPLE mod vytvori deskriptory do deskriptoroveho bufferu
             - in the *Simple* mode creates new descriptors in the Header and Offset Buffers
             - in the *User* mode:
             -+
              |
              V
              ndp_ctrl_user_fill_rx_descs (swbase/drivers/kernel/drivers/nfb/ndp/ctrl_ndp.c)
                  - checks the number of new descritpors in the Header and Offset Buffer
                  - creates new descriptors in the HW descirptor buffer
                  - shifts SDP and propagates it to the HW
         ndp_ctrl_rx_get_hwptr (swbase/drivers/kernel/drivers/nfb/ndp/ctrl_ndp.c)
             - is set as ndp_ctrl_rx_ops.get_hwptr
             - reads hhp
             - sets sync.hwptr accordingly propagatng it higher
             - if there are items in the Header and Offset Buffer waiting to be propagated as descriptors it does so

Header and Offset Buffer
^^^^^^^^^^^^^^^^^^^^^^^^

To better understand the function of individual pointer in the Header and Offset Buffer, here is a cheatsheet for that as well.

RX
==

.. code-block:: text
   :linenos:

     u.v2.rhp                     sync.hwptr                     sync.swptr
        |   items filled by HW, but   |   created ready items from   |  non-valid items
        V   not read by the SW yet    V   the SW but still empty     V  (freed by the SW)
    +==-+============================-+==-+======================+==-+======================+
                                          ^                      ^
                                          |                      |
                                      ctrl->php              ctrl->shp
                                          <- - - - - - - - - - - >
                                   items passed from Libnfb to the driver,
                                    but not yet from the driver to the HW
