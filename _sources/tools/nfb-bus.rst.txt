.. _nfb_bus:

nfb-bus
========

This tool allows reading and writing registers (CSR) implemented in FPGAs firmware and is intended only for debug purposes.

For the write access, user enters the address and the data as arguments in hexadecimal format.
For the read access, the data argument is omitted.
The tool performs 32b access to the specified address regardless of the data argument width.

List of available components
----------------------------

.. code-block:: console

    $ nfb-bus -l
    0x00002000: cesnet,pmci                         /firmware/mi_bus0/ofs_pmci
    0x00000000: cesnet,ofm,mi_test_space            /firmware/mi_bus0/mi_test_space
    0x00004000: netcope,tsu                         /firmware/mi_bus0/tsu
    0x01000000: netcope,dma_ctrl_ndp_rx             /firmware/mi_bus0/dma_module@0x01000000/dma_ctrl_ndp_rx0
    0x01000080: netcope,dma_ctrl_ndp_rx             /firmware/mi_bus0/dma_module@0x01000000/dma_ctrl_ndp_rx1
    ...
    0x00003110: netcope,i2c                         /firmware/mi_bus0/i2c1
    0x0000311c:                                     /firmware/mi_bus0/pmdctrl1
    0x00800000: netcope,pcsregs                     /firmware/mi_bus0/regarr0
    0x00008000: netcope,txmac                       /firmware/mi_bus0/txmac0
    0x00008200: netcope,rxmac                       /firmware/mi_bus0/rxmac0
    0x02000000: cesnet,nic,app_core                 /firmware/mi_bus0/application/nic_core_0
    ...
    0x00005100: cesnet,ofm,gen_loop_switch          /firmware/mi_bus0/dbg_gls1
    0x00005180: cesnet,ofm,mfb_generator            /firmware/mi_bus0/dbg_gls1/mfb_gen2dma
    0x000051c0: cesnet,ofm,mfb_generator            /firmware/mi_bus0/dbg_gls1/mfb_gen2eth

.. code-block:: console

    $ # write 32b value 0xdeadbeef to address 0x1c
    $ nfb-bus -c cesnet,ofm,mi_test_space 1c deadbeef
    $ # read 32b value from address 0x1c
    $ nfb-bus -c cesnet,ofm,mi_test_space 1c
    deadbeef

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

