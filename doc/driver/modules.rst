Modules
================

The NFB driver have modular structure and allows to attach simple extensions.

Important submodules (MI, boot, NDP...) are embedded, other can be added dynamically.

.. toctree::
   :maxdepth: 1
   :titlesonly:
   :hidden:

   main
   ndp

..
    Writing custom modules
    =======================

    Accessing the FDT
    ~~~~~~~~~~~~~~~~~

    Don't hold the offset of a node from DTB, another module can modify the FDT, which causes the offset to be invalid.

    Module API
    ----------
    .. kernel-doc:: kernel/drivers/nfb/char.c kernel/drivers/nfb/core.c
    :functions: nfb_char_register_mmap

    .. c:function:: int ioctl(int fd, int request)
