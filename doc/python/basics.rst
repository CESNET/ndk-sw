Basic device manipulation
=========================

First of all, we need to include nfb module and get device handle.

.. literalinclude:: ../../pynfb/examples/01-basics.py
  :start-at: 1.A
  :end-before: 1.B

Device and its features can be examined through Device Tree structure.

.. literalinclude:: ../../pynfb/examples/01-basics.py
  :start-at: 1.B
  :end-before: 1.C

After obtaining a node, handle of appropriate component can be used for register access.

.. literalinclude:: ../../pynfb/examples/01-basics.py
  :start-at: 1.C
