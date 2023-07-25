libnfb examples
===============

Simple access to the control registers
--------------------------------------

This example will perform some writes and reads to the user component in the firmware.

.. literalinclude:: examples/csr.c
  :language: c

Do not forget to compile with `-lnfb` switch.

..
    FDT examine example
    --------------------

NDP data transmit example
-------------------------

.. literalinclude:: examples/ndp.c
  :language: c
