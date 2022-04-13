libnfb register access 
=======================

libnfb exposes a simple API to the C language.
Let see how we can access to the firmware registers throught this API.

First thing in the code is to include header file.

.. code-block:: c

    #include <nfb/nfb.h>

We need a handle to the NFB device.
Simply get the first device in system.

.. code-block:: c

    struct nfb_device *dev = nfb_open("0");

As next step, obtain a handle to a component in the firmware.
But before that, the library needs to know the particular node in Device Tree, where the component we require is described.
Let find first node of the component whose `'compatible'` property value is the NDK RX MAC compatibility string.
Don't worry, with the :c:func:`nfb_comp_find` is really easy:

.. code-block:: c

    int node = nfb_comp_find(dev, "netcope,rxmac", 0);
    struct nfb_comp *comp = nfb_comp_open(dev, node);

Now we have a handle to access component's internal registers.
One would like to know, if the RX MAC is enabled for the packet reception.

.. code-block:: c

    const int RXMAC_REG_ENABLE = 0x20;
    int enabled = nfb_comp_read32(comp, RXMAC_REG_ENABLE) & 0x01;
    if (!enabled)
        nfb_comp_write32(comp, RXMAC_REG_ENABLE, 0x01);

Finally, the complete code needs to be compiled and linked with the `-lnfb` switch.

.. note::
    This example is not quite the right way, how to control the RX MAC.
    If we can't use the `nfb-eth -i0 -e1` as we write in C, we should to use the `rxmac` `libnetcope` API.


