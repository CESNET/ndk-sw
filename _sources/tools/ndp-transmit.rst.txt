.. _ndp_transmit:

ndp-transmit
------------

Read packets from PCAP file and send them to TX queue.

The PCAP file must be set with argument **-f**.
In case of multi queue run, two modes are available:

  - Without **-m** argument each queue sends the same data readen from one PCAP file.
  - With **-m** argument is possible to send specific PCAP file to each queue.
    In this case the filename should contain ``%t`` or ``%d`` string,
    which will be replaced with thread or DMA queue index.

As default the tool loads entire file content to the cache, but for very large PCAPs it can fail due to lack of memory.
Argument **-Z** disables this cache.

.. note::
   Currently the tool isn't memory optimized and for the same file allocates same number of cache buffers as the requested TX queues.

The tool sends content of input file only once as default,
however number of repetitions can be specified with argument **-l**.
Special case is value *0*, which is processed as infinity.

For debug purposes is implemented simple rate limiter which throttles the transmit by software and is configurable with argument **-s**
