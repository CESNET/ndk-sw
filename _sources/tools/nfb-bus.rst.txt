.. _nfb_bus:

nfb-bus
--------

This tool allows reading and writing registers (CSR) implemented in FPGAs firmware and is intended only for debug purposes.

For the write access, user enters the address and the data as arguments in hexadecimal format.
For the read access, the data argument is omitted.
The tool performs 32b access to the specified address regardless of the data argument width.

**Component mode** is used when the compatible string or Device Tree path is specified.
In this case the address is entered as offset relative to the base address of the component.
For the first 32b register in the component user enters simply address 0,
for the second 32b register is the address 4.

.. note::
   The boundary of the address space of the component is guarded, so the out-of-bound access will not be realized.

**Debug mode** is used when no compatible string nor Device Tree path is specified.
It uses entire memory space of the PCIe endpoint, user can access to whatever component.
But it can be unsafe as the requested offset doesn't have to be operated (no component responds at this address).
For that reason this mode must be previously enabled with the parameter of the nfb driver: ``modprobe nfb mi_debug=1``.

..
   TODO: Move to DT description
   warning::
   Author of the firmware is responsible to Device Tree consistency.
   Software can't assert if component described in Device Tree is truly populated in firmware.

.. warning::
   If user accesses an offset which is not handled by any component, the access will be not performed correctly
   (read access can return undefined value, typically 0xFFFFFFFF) and the system probably get stuck.
   This can happend also for read access to ``W/O`` registers, write access to ``R/O`` registers or any access to ``N/A`` registers.

- **-c** (compatible) will use the entered compatible string for component search.

- **-p** (path) will use the entered Device Tree path (ignores the **-i** argument).

- **-n** (count) Read more 32b values with the incrementing offset.

- **-a** (address) Print also address before readen data.

- **-l** (list) Print list of all available components in firmware, their offsets, compatible strings and Device Tree paths.

