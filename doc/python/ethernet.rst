Ethernet
--------

NFB device handle opened through nfb.open() is already wrapped by EthManager.
This allows simple configuration access to the ethernet ports.

.. literalinclude:: ../../pynfb/examples/03-eth.py
  :start-at: 3.A
  :end-before: 3.B

Or use the advanced configuration through PCS/PMA registers and also transceivers.
Some of the features are so useful that they have own property.

.. literalinclude:: ../../pynfb/examples/03-eth.py
  :start-at: 3.B
  :end-before: 3.C

Shortcuts are also handy.

.. literalinclude:: ../../pynfb/examples/03-eth.py
  :start-at: 3.C
