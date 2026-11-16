#!/usr/bin/env bash
# Host helper (Git Bash): restarts wlan0, requests a scan and captures the AR6003 firmware debug log
# The firmware log is read straight from the target memory through the debugfs file `fwlog` (the driver
# reads the dbglog ring over the diagnostic window, so it does not depend on events reaching the host).
# The pool is small (about 7.5 KB): it is drained BEFORE the scan so that the messages of the scan
# itself are kept, then read again after it.
# Usage: scripts/fwlog.sh [out/fwlog.bin]   then   python scripts/fwlog-decode.py out/fwlog.bin
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-out/fwlog.bin}"

bash "$HERE/scripts/tablet-cmd.sh" 'D=/sys/kernel/debug/ieee80211/phy0/ath6kl; killall iw 2>/dev/null; ifconfig wlan0 down; sleep 1; ifconfig wlan0 up; echo 0xffff > $D/fwlog_mask; sleep 1; timeout 5 cat $D/fwlog > /tmp/drain.bin; (iw dev wlan0 scan > /tmp/scan.txt 2>&1 &); sleep 4; timeout 5 cat $D/fwlog > /tmp/fw.bin; ls -l /tmp/drain.bin /tmp/fw.bin' 60 | grep "bin"
bash "$HERE/scripts/tablet-get.sh" /tmp/fw.bin "$HERE/$OUT" 15
