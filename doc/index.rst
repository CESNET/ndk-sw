NFB framework
=============

This bundle implements basic software environment for NDK_ based NFB [1]_ devices.

It is distributed as nfb-framework package (RPM, DEB) and consists mainly from:

* Configuration and data transmission tools
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

   install
..
   glossary

.. toctree::
   :hidden:
   :maxdepth: 3
   :includehidden:
   :caption: Tools

   tools/quickstart
   tools/index
   tools/nfb-tools
   tools/ndp-tools

.. toctree::
   :caption: libnfb reference
   :hidden:

   quick-start
   Examples <libnfb-example>
   Base API reference <libnfb-api-base>
   NDP API reference <libnfb-api-ndp>

.. toctree::
   :caption: Python module
   :hidden:
   :maxdepth: 3

   python/quick
   python/examples
   python/reference

.. toctree::
   :caption: libnfb reference
   :hidden:

..
   .. toctree::
   :caption: libnetcope reference
   :hidden:


.. toctree::
   :maxdepth: 1
   :caption: Linux driver

   Modules <driver/modules>
   Userspace access <driver/userspace>
