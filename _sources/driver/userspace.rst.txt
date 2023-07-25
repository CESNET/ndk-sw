==============================
Userspace access to NFB Driver
==============================

Large diversity of hardware boards and firmware blocks needs a flexible system for describing and driving hardware and firmware platform.
As the main building block for solving this challenge was chosen the Device Tree model (http://www.devicetree.org).
With well designed Device Tree structure the whole system can react very fast and safely for new hardware or firmware modifications and features.

NFB Linux driver is designed to be modular.
It can have user-written modules (either embedded or loadable).

Device Tree introduction
========================

Device Tree can have several forms.
DTS is human-readable source format resembling branched C structure. It's not suitable for machine processing.
DTB is binary format, which holds string names of properties and nodes.
Both format are convertible to each other with no information loss (except DTS comments). For this conversion is suitable compiler tool called DTC.

Typically the firmware provides the main Device Tree frame as DTB (e.g. compressed with XZ).
Card-specific part of the driver load this DTB.
Device Tree is explored for known units (typically specified by ``compatible`` property).
Recognized units are passed into corresponding attached modules.

FDT description
===============

This structure describes hardware and module interface of the device.

It contains typically three sections (nodes):

drivers
-------

Here are described all attached modules.
Embedded modules and their nodes are documented in :doc:`/modules`.

firmware
--------

Firmware node in NFB Device Tree structure mainly decribes address space of currently programmed firmware.
Components with ``reg`` property can be mapped and accessed in user application.
Other components (virtual or hierarchical) serves for information purposes.

board
-----

This node contains physical informations of the device and is not affected by changing firmware.
Card-specific part of the driver fills it with informations e.g. from Flash.
These holds information about card name, serial number, birth date, programmable chip name, ethernet interfaces and so on.

Character device
================

Typical access to the NFB device is through the node in *dev* filesystem.
Node is named with the ":file:`nfb%d`" format string.

Node supports these basic operations:

- read
   obtain FDT structure.
- seek
   obtain size of FDT structure.
- ioctl
   do a specific commands in attached NFB module.
- mmap
   map a space of attached NFB modules to virtual memory of application.

sysfs
=====

Basic informations about device are located in path :file:`/sys/class/nfb/nfb%d`.

Each device contains these entries:

- name
   hardware name
- pcislot
   PCIe slot ID in format 0000:00:00.0
- serial
   Serial number of the card

Attached modules can have additional entries in their subfolders.
