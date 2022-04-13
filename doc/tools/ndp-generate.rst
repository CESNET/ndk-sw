.. _ndp_generate:

ndp-generate
------------

Generate packets to the TX queue.

This tool generates packet of entered size, but doesn't fill packet data with any values for performance reasons.
In fact it generates only packet metadata (data_length and header_length in ``struct ndp_packet``).

.. note::
   Real transmitted content can be zeros or any previous sent data due to ring buffer system used for queue.

Size of the packets is entered by argument **-s**.
It can be one value or list of values separated by comma, in which case the list will be used as repeated sequence of packet length.
Item can be also a range and the tool generates pseudo-random length from entered range for each corresponding packet.

For example,
``-s 64,100-120,80,200-250``
can generate sequence of packet length
64, 110, 80, 220, 64, 115, 80, 210...

