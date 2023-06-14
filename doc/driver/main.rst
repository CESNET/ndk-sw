Base module
===========

This is a core driver for submodules.
It holds table of PCI compatible devices, for which it creates character node in /dev/nfb* .

Component locking
~~~~~~~~~~~~~~~~~

When the userspace application or attached module needs to get exclusive access to an component (based on FDT node), it can use the lock functions.
This ensures, that no other application on module can acquire lock to the same component.


IOCTL
~~~~~

- NFB_LOCK_IOC_TRY_LOCK - Try to lock a component feature in FDT for exclusive access
- NFB_LOCK_IOC_UNLOCK - Unlock component in FDT


MI submodule
============

Allows userspace application to map components from ``/firmware`` node to its virtual space.

Device Tree
~~~~~~~~~~~

Occupied node: ``/driver/mi``

subnode "PCIx,BARx" for each PCIe endpoint and its associated BAR configured as memory contains:

- property ``mmap_base``: (uint64_t) 
- property ``mmap_size``: (uint64_t)

Memory Map
~~~~~~~~~~
	Region bounded by base and size DT properties.

IOCTL
~~~~~
	None


Boot submodule
==============

This module allows to the user completely reconfigure programable device.

IOCTL
~~~~~

- NFB_BOOT_IOC_RELOAD
   Reconfigure device. This command requires the caller to be the last opener of the ``nfb%d`` character device node.
   After caller closes the node, module removes NFB device from system and issues the reconfigure command.
- NFB_BOOT_IOC_ERRORS_DISABLE
- NFB_BOOT_IOC_MTD_INFO
- NFB_BOOT_IOC_MTD_READ
- NFB_BOOT_IOC_MTD_WRITE
- NFB_BOOT_IOC_MTD_ERASE
