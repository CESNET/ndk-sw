.. _ndp_tx_sync:

NDP TX synchronization
-------------------------------

When transmitting data from the SW to the device (TX DMA direction), the driver receives data stored in RAM from the user.
Then tells the DMA Module where they are and how big.
Once the DMA Module has finished transmitting the data from the RAM, it signals the driver, which frees the data from the RAM.
It tells the user, that the transmition is completed and waits for more data to be transmitted.

Pointers description
====================

lib-drv sync::

                ___C__   ___B___   ___A___
               /      \ /       \ /       \
      lib:           HWPTR     RHP     SWPTR
    older >----|-------|---------|--------|> newer
      drv:   HWPTR   SWPTR

A) empty space for new data
B) data in NDP buffer, not yet synced with driver
C) hardware is aware this data, but not transfered yet.

Example of TX synchronization run
=================================

1. start: actual position HWPTR + SWPTR is undefined: ::

     lib:   ? ? ?
     older >···································> newer

2. first lock: libnfb requests the driver to lock part of the NDP buffer. Typically requests maximum space (buffer_size - 1). The position of HWPTR doesn't matter, decisive is only described length: ::

     lib: HWPTR                             SWPTR
     older >|---------------------------------|> newer

3. pointer sync: the driver:

    a) checks if there is no actual lock
    b) checks the boundaries of request
    c) sets sync.hwptr by last position assigned to hardware
    d) clamps sync.swptr according to free space:

   ::

     older >··|-----------------------------|··> newer
     drv:   HWPTR                         SWPTR

4. fill & wait: Software fills the data placeholders and calls ndp_tx_burst_put. This doesn't flush the data and the lock is not given in. ::

     lib:   HWPTR     RHP                 SWPTR
     older >··|————————|--------------------|··> newer

5. fill & flush: Software fills the further data placeholders and calls ndp_tx_burst_put & ndp_tx_burst_flush. This flushes the data, but software still have the lock: ::

     lib:               RHP+HWPTR         SWPTR
     older >················|---------------|··> newer

6. desc fill: the driver creates the descriptors from packet headers and passes the SDP into hardware: ::

     older >··|—————————————|---------------|··> newer
     drv:    HDP           SDP

7. try lock: libnfb requests to lock additional data space: ::

     lib:      SWPTR      RHP+HWPTR
     older >-----|··········|------------------> newer

8. clamp: driver partially rejects and clamp this request, because the requested space in ring buffer is not yet freed: ::

     lib:   SWPTR       RHP+HWPTR
     older >--|·············|------------------> newer

9. HDP update: hardware transmits some data and updates the HDP: ::

     older >-----|——————————|------------------> newer
     drv:       HDP        SDP

10. try lock: same as in step 7: ::

     lib:      SWPTR    RHP+HWPTR
     older >-----|··········|------------------> newer

11. sync: drivers detect the HDP update and  sets the SWPTR according to maximal free space: ::

     lib:      SWPTR     HP+HWPTR
     older >-----|··········|------------------> newer

Example of TX multiple writers
==============================


1. start condition: app0 have the lock, app1 just started

2. app1 try lock: app1 request for the lock: ::

     app1:HWPTR                             SWPTR
     older >|---------------------------------|> newer

3. app1 sync: driver checks, that the lock exists and is not held by app1 and refuses to lock (swptr == hwptr): ::

     app1:      SWPTR+HWPTR
     older >---------|-------------------------> newer

4. app0 fill & wait: Software fills the data placeholders and calls ndp_tx_burst_put. This doesn't flush the data and the lock is not given in. ::

     lib:           HWPTR     RHP                 SWPTR
     older >·········|————————|--------------------|··> newer

5. app0 fill & flush & unlock: Software fills the further data placeholders and calls ndp_tx_burst_put & ndp_tx_burst_flush. This flushes the data, and give in the lock: ::

     lib:                    HWPTR+SWPTR
     older >······················|···················> newer

6. desc fill: the driver creates the descriptors from packet headers and passes the SDP into hardware: ::

     drv:         HWPTR         SWPTR
     older >········|—————————————|---------------|··> newer
     drv:          HDP           SDP

7. app1 try lock: same as in step 2: ::

     app1:HWPTR                                   SWPTR
     older >|---------------------------------------|> newer

8. app1 sync: driver checks, that the lock doesn't exists and let the request of app1 go through: ::

     app1:      SWPTR          HWPTR
     older >-------|··············|------------------> newer

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

TX
==

.. code-block:: text
   :linenos:

    rte_eth_tx_burst (dpdk/lib/librte_ethdev/rte_ethdev.h)
        -+
         |
         V
         nfb_eth_ndp_tx (dpdk/drivers/net/nfb/nfb_tx.h)
             - set as dev->tx_pkt_burst
             - main TX transmition function
             - finds out the number of new succesfully sent packets
             - frees the corresponding number of Mbufs (sent packets are freed from the memory)
             - copies info from input array of Mbufs to its Mbuf Buffer (to be able to free them later) and to an array of ndp_packets
             ==============-+
                            |
                            V
                            (nc_)ndp_(v2_)tx_burst_get (swbase/libnfb/include/netcope/ndp_tx.h)
                                - copiest info from ndp_packets to Header and Offset Buffer
                                - shifts rhp
             ==========-+
                        |
                        V
                        (nc_)ndp_tx_burst_put (swbase/libnfb/include/netcope/ndp_tx.h)
                        -+
             -+          |
              |          |
              V          V
              (nc_)ndp_(v2_)tx_burst_flush (swbase/libnfb/include/netcope/ndp_tx.h)
                  - sets sync.hwptr and sync.swptr to rhp (unlocks the TX Channel for other applications)

    ndp_channel_txsync (drivers/kernel/drivers/nfb/ndp/channel.c)
        -+
         |
         V
         ndp_ctrl_tx_set_swptr (swbase/drivers/kernel/drivers/nfb/ndp/ctrl_ndp.c)
             - is set as ndp_ctrl_tx_ops.set_swptr
             - creates descriptors from new items in Header and Offset Buffer
             - shifts sdp propagates it to HW
