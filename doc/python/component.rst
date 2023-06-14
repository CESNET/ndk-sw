Simple component class example
------------------------------

Simple component class can be derived from :class:`nfb.BaseComp`.
The derived class must specify the DT_COMPATIBLE property,
which serves the base class to find the right FDT node.

Afterwards the _comp property can be accessed for configuration access.
Similarly the _dev can be accesed to handle additional requirements.

.. literalinclude:: ../../pynfb/examples/04-mi-comp-wrap.py
  :start-at: import nfb
  :end-before: Usage

.. literalinclude:: ../../pynfb/examples/04-mi-comp-wrap.py
  :start-after: "__main__"
