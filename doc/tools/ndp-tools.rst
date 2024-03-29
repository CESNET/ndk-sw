=========
NDP tools
=========

This group of tools can be used to transfer data from host to card and back via high speed DMA queues.

It is implemented as one binary tool into which the various modules are embedded.

The ndp-tool can use more queues at once (all available by default),
in this case each queue (or queue pair in case of loopback mode) is started in own thread.

The NDP queues are concurrent, there can be more readers on one RX queue,
so it is safe to run multiple instances on the same queue in parallel (all instances will get the same data).
On TX queue, multiple writers competing for one write lock.

Amount of transfered packets can be limited by argument **-p**,
similarly for byte-aware limit use **-b**. These limits are applied for each queue.

.. note::
   In RX modes (read, receive) the limit is applied by the tool, not by the DMA queue.
   The DMA queue can process more packet until receives the stop command.

Internally the data are transfered in bursts to overcome function call overhead, similarly to DPDK.
Number of packets in one burst can be overrided in **-B** argument.

For a single queue run the content of packet can be printed with argument **-D**.
User can choose header part, data part or both parts.
There is a special mode, in which the packet is only indicated with single character.
Also there is argument **-I** for sampling each Nth packet.

For multi queue run program shows statistic table with values for each queue and summary values.
The table is displayed and refreshed with ncurses library.
It can be disabled with **-I** argument set to 0, which otherwise sets interval of update.

.. tip::
   Multi queue run can be forced even for single queue, if an extra invalid queue index is entered (e.g. -i 0,-1).

The tool can be stopped manually for example by SIGINT generated by Ctrl+C.
At the end shows summary statistics, which can be disabled with quiet **-q** argument.

.. toctree::
   :maxdepth: 1
   :titlesonly:
   :hidden:

   ndp-read
   ndp-generate
   ndp-receive
   ndp-transmit
   ndp-loopback

.. rubric:: :ref:`ndp-read<ndp_read>`
.. include:: ndp-read.rst
   :start-line: 5
   :end-line: 6

.. rubric:: :ref:`ndp-generate<ndp_generate>`
.. include:: ndp-generate.rst
   :start-line: 5
   :end-line: 6

.. rubric:: :ref:`ndp-receive<ndp_receive>`
.. include:: ndp-receive.rst
   :start-line: 5
   :end-line: 6

.. rubric:: :ref:`ndp-transmit<ndp_transmit>`
.. include:: ndp-transmit.rst
   :start-line: 5
   :end-line: 6

.. rubric:: :ref:`ndp-loopback<ndp_loopback>`
.. include:: ndp-loopback.rst
   :start-line: 5
   :end-line: 6
