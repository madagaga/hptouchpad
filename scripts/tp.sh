#!/usr/bin/env bash
# Host helper (Git Bash): runs a `tp` command on the tablet over SSH (Wi-Fi). Example: scripts/tp.sh wifi scan
# Usage: scripts/tp.sh [-i ip] <tp arguments...>
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IP=192.168.10.116
[ "${1:-}" = "-i" ] && { IP="$2"; shift 2; }
CMD="tp"; for a in "$@"; do CMD="$CMD $(printf '%q' "$a")"; done
exec bash "$HERE/scripts/tablet-ssh.sh" -i "$IP" "$CMD"
