#!/bin/sh
# Runs ON the stock webOS (pushed by scripts/webos-recover.sh): same trick as our Linux init, with dd on
# /dev/mem since webOS has no devmem. Writes the Palm "recover" restart reason (0x6f656d11) to IMEM
# 0x2A05F65C, then drops PS_HOLD (0x00800820): the SoC resets and the bootloader enters recovery mode.
ls -l /dev/mem || exit 1
sync; sync
printf '\021\155\145\157' | dd of=/dev/mem bs=4 seek=$((0x2A05F65C / 4)) conv=notrunc 2>&1
dd if=/dev/mem bs=4 skip=$((0x2A05F65C / 4)) count=1 2>/dev/null | od -An -tx4
printf '\000\000\000\000' | dd of=/dev/mem bs=4 seek=$((0x00800820 / 4)) conv=notrunc 2>&1
sleep 5
echo "still running"
