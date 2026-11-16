#!/usr/bin/env bash
# Host helper (Git Bash): runs a command on the tablet over SSH (Wi-Fi), with the project key secrets/touchpad_ed25519
# (generated once, never committed). Usage: scripts/tablet-ssh.sh [-i ip] 'command'     (default ip: 192.168.10.116)
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IP=192.168.10.116
[ "${1:-}" = "-i" ] && { IP="$2"; shift 2; }
exec ssh.exe -i "$HERE/secrets/touchpad_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL \
  -o ConnectTimeout=10 -o BatchMode=yes "root@$IP" "$@"
