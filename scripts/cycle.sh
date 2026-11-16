#!/usr/bin/env bash
# Host helper (Git Bash): one full test cycle without touching the tablet.
#   1. if the tablet runs our Linux, ask it (through the USB serial shell) to reboot into the
#      bootloader recovery mode: write the Palm "recover" restart reason to IMEM, then drop PS_HOLD
#   2. attach the recovery device to WSL, restart the novacom container and boot the image in RAM
#   3. wait for the USB gadget of our Linux and attach it to WSL
# Usage: scripts/cycle.sh [image]        (default: out/kernel/uImage)
# From webOS (0830:8072) it cannot reboot by itself: use the buttons once, then it works.
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${1:-out/kernel/uImage}"

ulist() { powershell.exe -NoProfile -Command '$env:Path=[Environment]::GetEnvironmentVariable("Path","Machine")+";"+[Environment]::GetEnvironmentVariable("Path","User"); usbipd list' | tr -d '\r'; }
uattach() { powershell.exe -NoProfile -Command '$env:Path=[Environment]::GetEnvironmentVariable("Path","Machine")+";"+[Environment]::GetEnvironmentVariable("Path","User"); usbipd attach --wsl --busid '"$1"' 2>$null' >/dev/null 2>&1; }
busid_of() { ulist | grep "$1" | awk '{print $1}' | head -1; }
wait_for() {   # wait_for <vid:pid> <seconds>
  local t=0
  while [ $t -lt "$2" ]; do [ -n "$(busid_of "$1")" ] && return 0; sleep 2; t=$((t + 2)); done
  return 1
}

if [ -n "$(busid_of 1d6b:0104)" ]; then
  echo ">> tablet runs our Linux: rebooting into recovery"
  uattach "$(busid_of 1d6b:0104)"; sleep 3
  bash "$HERE/scripts/tablet-cmd.sh" '(devmem 0x2A05F65C 32 0x6f656d11 && devmem 0x00800820 32 0) || (busybox devmem 0x2A05F65C 32 0x6f656d11; busybox devmem 0x00800820 32 0)' 3 >/dev/null 2>&1 || true
elif [ -n "$(busid_of 0830:8072)" ]; then
  echo "!! the tablet runs webOS (0830:8072): put it in recovery with the buttons once (Volume Up + Power)"; exit 1
fi

echo ">> waiting for the recovery device (0830:8071)"
wait_for 0830:8071 60 || { echo "!! no recovery device after 60 s"; ulist | grep -E "0830|1d6b"; exit 1; }
uattach "$(busid_of 0830:8071)"; sleep 3

docker restart hptouchpad-novacom >/dev/null; sleep 3
docker exec hptouchpad-novacom novacom -l | head -2
echo ">> booting $IMAGE"
MSYS_NO_PATHCONV=1 docker exec -i hptouchpad-novacom sh -c 'novacom boot mem:// < "/work/$0"' "$IMAGE" || { echo "!! boot failed"; exit 1; }

echo ">> waiting for the USB gadget (1d6b:0104)"
wait_for 1d6b:0104 60 || { echo "!! the gadget did not appear"; exit 1; }
sleep 2; uattach "$(busid_of 1d6b:0104)"; sleep 4
ulist | grep -E "0830|1d6b"
echo ">> cycle done: use scripts/tablet-cmd.sh"
