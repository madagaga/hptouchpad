#!/usr/bin/env bash
# Host helper (Git Bash): runs a command in the shell of the tablet's USB serial gadget.
# The gadget (1d6b:0104) must be attached to WSL with usbipd; it is reached through the Alpine WSL
# distro (/dev/ttyACM0). Usage: scripts/tablet-cmd.sh 'uname -a' [seconds_to_wait]
set -euo pipefail
CMD="${1:?usage: tablet-cmd.sh '<command>' [seconds]}"
WAIT="${2:-3}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

printf '%s\n' "$CMD" > "$HERE/out/tablet-cmd.txt"
rm -f "$HERE/out/tablet-reply.txt"
MSYS_NO_PATHCONV=1 wsl.exe -d Alpine -u root -- sh /mnt/d/SRC3/hptouchpad/scripts/tablet-cmd-inner.sh "$WAIT"
cat "$HERE/out/tablet-reply.txt"
