#!/usr/bin/env bash
# Host helper (Git Bash): copies a file from the tablet to the PC through the USB serial shell
# (base64 over scripts/tablet-cmd.sh) and verifies the size and the checksum.
# Usage: scripts/tablet-get.sh /remote/path local/path [seconds_to_wait]
set -euo pipefail
REMOTE="${1:?usage: tablet-get.sh <remote> <local> [seconds]}"
LOCAL="${2:?usage: tablet-get.sh <remote> <local> [seconds]}"
WAIT="${3:-8}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "$(dirname "$LOCAL")"
RAW="$(bash "$HERE/scripts/tablet-cmd.sh" "echo __SIZE__\$(wc -c < '$REMOTE'); echo __SUM__\$(md5sum < '$REMOTE' | cut -d' ' -f1); echo __B64BEGIN__; base64 '$REMOTE'; echo __B64END__" "$WAIT" | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' | tr -d '\r')"

python - "$LOCAL" <<EOF
import sys, re, base64, hashlib
raw = """$RAW"""
local = sys.argv[1]
size = int(re.search(r'^__SIZE__(\d+)', raw, re.M).group(1))
md5 = re.search(r'^__SUM__([0-9a-f]{32})', raw, re.M).group(1)
body = raw.split('__B64BEGIN__\n', 1)[1].split('\n__B64END__', 1)[0]
data = base64.b64decode(''.join(body.split()))
ok = len(data) == size and hashlib.md5(data).hexdigest() == md5
print(('OK ' if ok else 'MISMATCH ') + local + ' size=%d expected=%d md5=%s' % (len(data), size, hashlib.md5(data).hexdigest()))
open(local, 'wb').write(data)
sys.exit(0 if ok else 1)
EOF
