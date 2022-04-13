NFB framework
=============

This bundle implements basic software support for NDK_ based NFB [1]_ devices.

It is distributed as nfb-framework package (RPM, DEB) and consists mainly from:

* Configuration and transmission tools
* Userspace library
* Linux kernel driver

Using this framework is possible to control and configure firmware features as
well as process data (network traffic) at very high speeds.

.. _NDK: https://www.liberouter.org/ndk/

.. [1] NFB device is typically a PCI-Express card, which holds a programmable chip (FPGA) and has Ethernet interface.
       It is designed for processing Ethernet data stream.

.. toctree::
   :caption: Introduction
   :hidden:

   quick-start
..
   libnfb-quick-start-registers
   libnfb-quick-start-ndp

.. toctree::
   :hidden:
   :maxdepth: 3
   :includehidden:
   :caption: Tools

   tools/index
   tools/nfb-tools
   tools/ndp-tools

.. toctree::
   :caption: Examples
   :hidden:

   libnfb-example

.. toctree::
   :caption: libnfb reference
   :hidden:

   libnfb-api-base
   libnfb-api-ndp

..
   .. toctree::
   :caption: libnetcope reference
   :hidden:


..
   .. toctree::
   :maxdepth: 1
   :caption: Driver
   drivers/index
