#!/bin/bash

# Script updates MAX10 FPGA firmware on the 100G2QLP card
# Usage: ./nfb-maxfw-update <file.pof>
# Card should power off to apply the new fimware

# Path to the binary tool
TOOL=./nfb-max-spi

# CFM block starting address in flash: AC00 for the MAX10-08 device,
#                                      12800 for the MAX10-16 device
CFM_START_ADDR=AC00
CFM_SIZE=35840
F_START=178344

## MAX10-16 (card SN P005 (T05432) only)
#CFM_START_ADDR=12800
#CFM_SIZE=67584
#F_START=307368

if [[ "$#" -lt 1 ]]; then
    echo "Missing file name"
    exit 1
fi

echo "Updating MAX10 firmware from file $1"
echo -n "Current MAX10 revision: "
$TOOL -s ver
echo -n "Current MAX10 date: "
$TOOL -s date

# Dump and format configuration data from the .pof file
hexdump -v -s $F_START -e '1/4 "%08x" "\n"' $1 | rev | tr '0123456789abcdef' '084c2a6e195d3b7f' | head -n $CFM_SIZE > max10.cfg.tmp

# Disable write protect on CFM sector
$TOOL -s fctrl 1 37ffffff

# Clear CFG memory
$TOOL -s fctrl 1 375fffff

# Wait for erase operation to complete
sleep 0.5

# Get flash status - bit 4 must be set
echo -n "Flash status: "
$TOOL -s fctrl 0
# should return fffffc10

# Stop clearing the CFM sector
$TOOL -s fctrl 1 37ffffff

echo -n "Writing CFM FLASH......."
# Write config file
$TOOL -s flash -W $CFM_START_ADDR < max10.cfg.tmp
echo "done"

# Enable write protect on CFG sector
$TOOL -s fctrl 1 3fffffff

# verify the flash content
echo -n "Verifing......"
$TOOL -s flash -c $CFM_SIZE $CFM_START_ADDR > max10.cfg.dump
echo "done"

diff -q max10.cfg.dump max10.cfg.tmp > /dev/null
if [ "$?" -eq "0" ]; then
	echo "Firmware update sucessfull. Power cycle the card to apply the changes."
else
	echo "Verification failed, please try again. Do not power off the card!"
fi

# Clean up
rm -f max10.cfg.dump
rm -f max10.cfg.tmp
