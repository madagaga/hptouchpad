#!/usr/bin/env bash
# Host helper (Git Bash): copies a small file from the PC to the tablet through the USB serial shell
# (gzip + base64 in short lines, paced, over scripts/tablet-put-inner.sh) and verifies the checksum.
# The tty of the gadget is in canonical mode: lines must stay below 4096 characters and must not arrive too fast.
# Usage: scripts/tablet-put.sh local/path /remote/path [seconds_to_wait]
set -euo pipefail
LOCAL="${1:?usage: tablet-put.sh <local> <remote> [seconds]}"
REMOTE="${2:?usage: tablet-put.sh <local> <remote> [seconds]}"
WAIT="${3:-20}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

python - "$LOCAL" "$REMOTE" "$HERE/out/tablet-cmd.txt" <<'EOF'
import sys, gzip, base64, hashlib
local, remote, out = sys.argv[1:4]
data = open(local, 'rb').read()
b64 = base64.b64encode(gzip.compress(data, 9)).decode()
lines = ["rm -f %s.b64" % remote]
for i in range(0, len(b64), 600):
    lines.append("echo %s >> %s.b64" % (b64[i:i + 600], remote))
lines.append("base64 -d %s.b64 | gunzip > %s; rm -f %s.b64; chmod +x %s" % (remote, remote, remote, remote))
lines.append("echo __MD5__$(md5sum < %s | cut -d' ' -f1)" % remote)
open(out, 'w', newline='\n').write("\n".join(lines) + "\n")
print("local md5", hashlib.md5(data).hexdigest(), len(data), "bytes,", len(b64), "base64 characters")
EOF

rm -f "$HERE/out/tablet-reply.txt"
MSYS_NO_PATHCONV=1 wsl.exe -d Alpine -u root -- sh /mnt/d/SRC3/hptouchpad/scripts/tablet-put-inner.sh "$WAIT"
grep -a '^__MD5__' "$HERE/out/tablet-reply.txt" | tr -d '\r' || echo "no checksum in the reply"
# nothing sensitive is sent, but keep the working files tidy
[ -n "${KEEP:-}" ] || rm -f "$HERE/out/tablet-cmd.txt" "$HERE/out/tablet-reply.txt"
