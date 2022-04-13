NDP API
=======


This part of the libnfb library is used to access high-speed queues for packet transmissions.

Basic structures
----------------

.. doxygenstruct:: ndp_packet
   :members:

Init and deinit functions
-------------------------

.. doxygenfunction:: ndp_open_rx_queue

.. doxygenfunction:: ndp_open_tx_queue

.. doxygenfunction:: ndp_close_rx_queue

.. doxygenfunction:: ndp_close_tx_queue

Transmission functions
-------------------------

.. doxygenfunction:: ndp_queue_start

.. doxygenfunction:: ndp_queue_stop



.. doxygenfunction:: ndp_rx_burst_get

.. doxygenfunction:: ndp_rx_burst_put

.. doxygenfunction:: ndp_tx_burst_get

.. doxygenfunction:: ndp_tx_burst_put

.. doxygenfunction:: ndp_tx_burst_copy

Miscellaneous functions
-------------------------

.. doxygenfunction:: ndp_queue_get_numa_node
