libnfb packet transmission
============================

Let see how to transfer single packet to the Ethernet port with C API.

We will need again the common NFB header and newly the NDP header.

.. code-block:: c

    #include <nfb/nfb.h>
    #include <nfb/ndp.h>

We need a handle to the NFB device.

.. code-block:: c

    struct nfb_device *dev = nfb_open("0");

First, we need to open a transmit queue and start it.

.. code-block:: c

    struct ndp_queue *q = ndp_open_tx_queue(dev, 0);
    ndp_queue_start(q);

The NDP API uses a precreated buffer for data (in both RX and TX direction).
Thus we firstly do a request for packet placeholder.

.. code-block:: c

    struct ndp_packet packet = {.data_length = 128, .header_length = 0};
    int cnt = ndp_tx_burst_get(q, &packet, 1);

The returned value from the :c:func:`ndp_tx_burst_get` indicates, how much packets are sucessfully reserved.
We should ensure, that the `cnt` value is currently 1. Oh, we should check all of the return values by the way!
Let's say, that we did it.

Then we can fill the data pointer with some real values. For simplicity, pretend the IPv4 packet header.

.. code-block:: c

    packet.data[14] = 0x08;

Finally, we can hand over the requested burst (of size 1) to the library.

.. code-block:: c

    ndp_tx_burst_flush(q);

Of course, you should enable the TX MAC.
And check if the packet is really transmitted.

.. code-block:: bash

   nfb-eth -i0 -t -e1
   nfb-dma -i0 -t
   nfb-eth -i0 -q tx_transmitted
