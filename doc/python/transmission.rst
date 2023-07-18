Data transmission
-----------------

Receiving and sending data can be done through NDP subsystem.

.. note::
  Ensure the firmware datapath isn't blocked (e.g. MACs are enabled)

.. literalinclude:: ../../pynfb/examples/02-data-transfer.py
  :start-at: 2.A
  :end-before: 2.B

The NDP subsystem can transmit or receive data over multiple queues at once.

.. literalinclude:: ../../pynfb/examples/02-data-transfer.py
  :start-at: 2.B
  :end-before: 2.C

Queue statistics can obtained as dictionary and reseted.

.. literalinclude:: ../../pynfb/examples/02-data-transfer.py
  :start-at: 2.C
