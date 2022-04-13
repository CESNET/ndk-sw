.. _nfb_boot:

nfb-boot
---------

A simple tool that allows to write a new firmware image to the Flash memory on an FPGA card and reload the FPGA configuration on the fly.
Also can be used to obtain information from the firmware image file.

NFB card typically holds two slots for firmware image: `configuration` (work) and `recovery`.
Ordinary procedure is described below:

- On power-on the card loads the FPGA configuration firmware from the `recovery` slot.

- As the system starts up, user can upload new firmware image to the `configuration` slot and reload FPGA configuration.

This approach provides more safety. If something goes wrong (e.g. the firmware is corrupted) - the device is available in the system after next power cycle with the `recovery` firmware.

.. tip::
   If the firmware image is proven, it can be uploaded to the `recovery` slot,
   however this choice will be possible only with the `flash_recovery_ro=0` parameter of the nfb driver.

As the system starts up, user can reload existing FPGA configuration from the `configuration` slot and `recovery` as well, without need of the image upload.

- **-w** (write) Write firmware image from the file to specified device slot without reload.

- **-f** (write + reload) Write firmware image from the file to device slot and reload FPGA configuration from it.

- **-F** (reload) Reload FPGA configuration from already uploaded firmware image in specified slot.

- **-b** (quick boot) Reload the FPGA configuration from image in specified slot.
  Then compare signature of loaded firmware and the firmware image file.
  If signature differs, do entire write + reload process.

.. caution::
   Quick boot requires the slot already contains functional configuration.

- **-i** (info) Print information about the firmware file.

- **-l** (list) Print list of available slots.

- **-q** (quiet) Do not show progress


.. tip::
   The firmware.nfw is a TAR archive packed by GZIP, one can change the file suffix to `.tar.gz` and extract raw bitstream or the Device Tree.

.. warning::
   Some systems doesn't supports on-the-fly reconfiguration of the FPGA,
   because the PCIe link is interrupted in the reload process.
   The PCIe link partner can report errors or may fail in the rescan phase.
   There is the `boot_linkdown_enable=0` experimental parameter of the nfb driver.
