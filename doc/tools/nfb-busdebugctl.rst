.. _nfb_busdebugctl:

nfb-busdebugctl
---------------

This tool can be very useful for debugging streaming buses (typically MFB and MVB in the NDK).
It communicates with the Streaming Debug components in the FW - Streaming Debug Master and Probe.
The Debug Probes are (need to be) inserted into the pipeline between neighboring components.
More info can be found in the documentation for `STREAMING_DEBUG_MASTER <https://cesnet.github.io/ndk-fpga/devel/comp/debug/streaming_debug/readme.html>`_.

The Streaming Debug system (Master+Probe) in the FW (must be integrated first) provides counts of:

- `Data words`: A data word is transferred from one FW component (the Source), which has valid data prepared to send to the neighboring component (the Destination/Sink), which is ready to accept it (SRC_RDY='1', DST_RDY='1').
- `Wait cycles`: A wait cycle is when the Source does not have valid data prepared to send to the Destination, however, this Destination component is not ready to accept new data anyway (SRC_RDY='0', DST_RDY='0').
- `Destination hold cycles`: The Source has valid data prepared to send to the Destination, but the Destination is not ready to accept new data (SRC_RDY='1', DST_RDY='0').
- `Source hold cycles`: The Source does not have valid data prepared to send to the Destination, which is (would be) able to accept them (SRC_RDY='0', DST_RDY='1').
- `Started transactions`: the number of started transactions (SRC_RDY='1', DST_RDY='1', SOP='1') - should be similar to the number of Ended transactions; both give a rough estimate of the number of processed packets.
- `Ended transactions`: the number of ended transactions (SRC_RDY='1', DST_RDY='1', EOP='1') - should be similar to the number of Started transactions; both give a rough estimate of the number of processed packets.

Parameters
~~~~~~~~~~

.. code-block:: console

    $ nfb-busdebugctl -h
    Usage: nfb-busdebugctl [-ABDElhV] [-d path] [-e 0|1] [-i probe] [-n probe]
 
    Only one command may be used at a time.
    -d path        Path to device file to use
    -e 0|1         Start(1)/Stop(0) counters (start also resets their values)
    -i probe       Select probe using "master_index:probe_id" (default "0:0")
    -n probe       Select probe using "probe_name"
    -A             Print status or affect all probes
    -B             Block data on probed bus
    -D             Drop data on probed bus
    -E             Enable normal functionality on probed bus
    -l             List available probes
    -h             Show this text
    -V             Show version

Counters start counting immediately after boot/power up.
Keep in mind that the counter of `Source hold cycles` will probably never show zero, as there is always some delay before the first data arrives.
This is the expected behavior.
To reset the counters, use parameter "-e": `-e0` to stop the counters, `-e1` to reset their values and enable them.

Parameters "-i" and "-n" identify one of the Probes

- `-i 0:1`: using two numbers in format **x:y** where **x** is the ID of the Master and **y** is the ID of one of the Master's Probes.
            Beware that the behavior of this parameter is not very consistent.
- `-n XYZA`: This one uses a four-letter string (name) that has been assigned to the Probe in the FW design (as a generic parameter).
             These Probe names and their connections can be obtained either from their instantiation in the FW or its documentation - if provided.
             Notice that multiple Probes of different Masters have the same name (common for multi-stream pipelines where one Master is per one stream).
             See the output of `nfb-busdebugctl -l` below.

All available Probes can be listed using `nfb-busdebugctl -l`.
Both forms of Probe identification ("-i", "-n") are used in the output.

.. code-block:: console

    $ nfb-busdebugctl -l
    0:0 - RXEH
    0:1 - RXED
    0:2 - HFEO
    0:3 - RSSO
    0:4 - FLTO
    0:5 - PFIO
    0:6 - TXCH
    0:7 - TXCD
    0:8 - RXDH
    0:9 - RXDD
    1:0 - RXEH
    1:1 - RXED
    1:2 - HFEO
    1:3 - RSSO
    ...

This FW design contains two (identical) 100G streams (pipelines), and the Master+Probes have been instantiated per each stream.
Notice how the Probe names are the same for both Masters.

Parameters "-B", "-D", and "-E" are used for advanced features for "Bus Control", see documentation of `Streaming Debug <https://cesnet.github.io/ndk-fpga/devel/comp/debug/streaming_debug/readme.html#bus-control>`_.
TL;DR: if the Master is set accordingly and the Probes are connected appropriately, use:

- `-B XYZA` to block traffic at the selected Probe,
- `-D XYZA` to drop traffic at the selected Probe, and
- `-E XYZA` to return the selected Probe to its flow-through state.

.. warning::
    Beware that the "Bus Control" feature is often not needed, which is reflected by the connection of Probes in the FW (see documentation of the `Streaming Debug components <https://cesnet.github.io/ndk-fpga/devel/comp/debug/streaming_debug/readme.html>`_). Hence, these three parameters may not work.

How to use
~~~~~~~~~~

This tool is typically used when locating a place that either decreases throughput (bottleneck) or blocks all traffic (design is stuck).
The most common way to identify this is to issue the `nfb-busdebugctl -A` command and watch the `Destination hold cycles` counter of all Probes.
It is recommended to analyze the difference in these counters over a (short) period of time:

.. code-block:: console

    $ nfb-busdebugctl -A > first_sample.txt
    $ nfb-busdebugctl -A > second_sample.txt
    $ vimdiff first_sample.txt second_sample.txt

This creates a bounded interval, which nullifies idle clock cycles before any data are transmitted and other potentially unwanted effects.

How to read the probed data
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following diagram is borrowed from the documentation of the `Streaming Debug components <https://cesnet.github.io/ndk-fpga/devel/comp/debug/streaming_debug/readme.html>`_.
It shows a simple pipeline of three FW components (modules): A, B, and C.

.. _figure1:

.. image:: img/nfb-busdebugctl_example1.drawio.svg
    :align: center
    :width: 60 %

    An example of a stream pipeline where component A sends data to comp B and B to C (blue lines) and where C uses backpressure to pause data from B and B to A (red lines).

When performing analysis, watch for incrementing counters of the `Destination hold cycles`.
These are the ones that indicate that something is holding back the traffic.
When viewing the difference between the samples of `nfb-busdebugctl -A`, three scenarios can occur.
The `Destination hold cycles` counter is incremented on:

#. all Probes in the pipeline -> the blocking occurs somewhere further downstream of the probed pipeline.
#. none of the Probes in the pipeline -> the blocking occurs somewhere before the probed pipeline.
#. only some of the Probes in the pipeline -> the blocking is done by a component in the probed pipeline.

The third point indicates an inefficiency in the pipeline.
How to determine which component is at flaw?
This is best explained using a picture.
The following picture builds on the example above, showing the same pipeline with connected Debug Probes.

.. _figure2:

.. image:: img/nfb-busdebugctl_example2.drawio.svg
    :align: center
    :width: 60 %

    Debug Probes on the input and output of the pipeline and in between the components A, B, and C.

If the `Destination hold cycles` counters increment on Probes:

#. on the input before component A, and
#. between components A and B 

then this would indicate that component C is the one blocking the traffic.

