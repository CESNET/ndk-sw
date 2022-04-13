.. _nfb_tsu:

nfb-tsu
-------

Tool for synchronizing timestamping unit with system time.

Timestamping unit is typically included in NDK-based firmware.
This tool can control and synchronize the TSU with system time.
It runs on background and periodically computes time difference and adjusts increment register in the TSU.

If the TSU is not controlled, it doesn't produces valid timestamps and the timestamp value in received packets will be typically null.

For debugging use the **-D** argument.
