Glossary
========

.. glossary::

    NFB device
      A HW card compatible with the NFB driver.

    DMA channel
      A part of the DMA Module (device firmware) responsible for control of DMA transfers within one independent DMA Channel.

    PCI interface
      The PCI socket where the device is connected to the host computer.

    NDP driver
      | The NFB driver module responsible for DMA data transfer.
      | Source located in ``drivers/kernel/drivers/nfb/ndp/``.

    NDP ctrl
      | The module, which controls the DMA Module from the software.
      | Source located in ``drivers/kernel/drivers/nfb/ndp/ctrl_ndp.c`` and ``ctrl.c``

    NDP channel
      The module which creates interface for other modules to use the NDP Ctrl module. It mainly includes synchronisation between multiple users. (Source located in ``drivers/kernel/drivers/nfb/ndp/channel.c``)

    NDP subscription
      Together with the "Subscriber" module it allows to connect multiple DMA Channels to a single user. In this case it doesn't have any critical purpose other than abstraction. (Source located in ``drivers/kernel/drivers/nfb/ndp/subscription.c``)

    Libnfb
      Library included by the user application which contains easy-to-use functions to communicate with the driver. (Source located in ``libnfb/``)

    User app
      An arbitrary user application.
