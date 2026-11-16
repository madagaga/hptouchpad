#!/usr/bin/env bash
# Host helper (Git Bash): reboots the tablet from the stock webOS (0830:8072) into the USB recovery mode
# (0830:8071) through the novacom container, with no button: builds (./build.sh reboot-cmd) a tiny static `reboot(RESTART2, "recover")`
# tool, pushes it to the tablet and runs it. The webOS device must be attached to WSL (usbipd attach).
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
bash "$HERE/build.sh" reboot-cmd || exit 1
docker restart hptouchpad-novacom >/dev/null; sleep 4
docker exec hptouchpad-novacom novacom -l | head -2
MSYS_NO_PATHCONV=1 docker exec -i hptouchpad-novacom sh -c 'novacom put file:///tmp/reboot-cmd < /work/out/reboot-cmd/reboot-cmd && novacom run file:///bin/chmod 755 /tmp/reboot-cmd; novacom run file:///tmp/reboot-cmd recover'
echo ">> reboot requested: the recovery device (0830:8071) should appear in a few seconds"
