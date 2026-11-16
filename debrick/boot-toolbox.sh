#!/bin/sh
# Boots TPToolbox-serial in RAM on a TouchPad in the bootie DFU mode (0830:8070, Home + Power + Volume Up).
# Needs dfu-util and fastboot on a host that sees the tablet's USB device. Nothing is written to the flash.
set -e
cd "$(dirname "$0")"
dfu-util -d 0830:8070 -R -D moboot-dfu-v004 || true  # the status read after the reset fails, expected
fastboot flash bootmem TPToolbox-serial               # waits for moboot (18d1:d00d)
echo "toolbox started: open the USB serial port (0525:a4a7, /dev/ttyACM0 or COMx) at 115200 and log in as root"
