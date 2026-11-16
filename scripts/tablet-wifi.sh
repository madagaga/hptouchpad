#!/usr/bin/env bash
# Host helper (Git Bash): after a boot, connects the tablet (Alpine rootfs) to a WPA network through the USB serial shell, applies
# the DHCP lease, turns Wi-Fi power saving off (or SSH times out) and sets the clock from the PC. The passphrase is only passed
# to the tablet RAM (mnt/media/touchpad/wifi-wpa writes it to /tmp); the serial command files are wiped afterwards.
# Usage: scripts/tablet-wifi.sh <ssid> <passphrase>      -> prints the address, then test with scripts/tablet-ssh.sh -i <address>
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SSID="${1:?usage: tablet-wifi.sh <ssid> <passphrase>}"
PSK="${2:?usage: tablet-wifi.sh <ssid> <passphrase>}"
wipe() { rm -f "$HERE/../out/tablet-cmd.txt" "$HERE/../out/tablet-reply.txt"; }
trap wipe EXIT
for i in 1 2 3 4 5 6 7 8; do "$HERE/tablet-cmd.sh" "echo READY" 5 2>/dev/null | grep -q "^READY" && break; done
"$HERE/tablet-cmd.sh" "pkill wpa_supplicant; pkill udhcpc; sleep 1; (sh /mnt/media/touchpad/wifi-wpa '$SSID' '$PSK' >/tmp/wifi.log 2>&1 &)" 5 >/dev/null 2>&1
wipe
for i in $(seq 1 12); do "$HERE/tablet-cmd.sh" "wpa_cli -i wlan0 status 2>&1 | grep wpa_state" 8 2>/dev/null | grep -q COMPLETED && break; done
LEASE=$("$HERE/tablet-cmd.sh" "busybox udhcpc -i wlan0 -q -n -t 5 -s /bin/true 2>&1 | grep 'lease of'" 25 2>/dev/null | grep -o "lease of [0-9.]* obtained from [0-9.]*" | head -1)
IP=$(echo "$LEASE" | awk '{print $3}'); GW=$(echo "$LEASE" | awk '{print $6}')
[ -n "$IP" ] || { echo "no DHCP lease" >&2; exit 1; }
"$HERE/tablet-cmd.sh" "busybox ifconfig wlan0 $IP netmask 255.255.255.0; busybox route add default gw $GW dev wlan0; echo nameserver $GW > /etc/resolv.conf; iw dev wlan0 set power_save off; date -u -s '$(date -u '+%Y-%m-%d %H:%M:%S')' >/dev/null" 8 >/dev/null 2>&1
echo "$IP"
