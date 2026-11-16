#!/bin/sh
# Runs inside the Alpine WSL distro (root): host-side view of the tablet's USB gadget.
#   wsl -d Alpine -u root -- sh /mnt/d/SRC3/hptouchpad/scripts/gadget-info.sh
echo "== usb0 carrier / operstate"
cat /sys/class/net/usb0/carrier /sys/class/net/usb0/operstate 2>&1 | tr '\n' ' '
echo
echo "== host usb0 statistics"
for f in rx_packets tx_packets rx_errors tx_errors tx_dropped tx_carrier_errors; do
  echo "$f=$(cat /sys/class/net/usb0/statistics/$f 2>&1)"
done
echo "== negotiated speed (Mbit/s): $(cat /sys/bus/usb/devices/1-1/speed 2>&1)"
echo "== USB device tree"
for d in /sys/bus/usb/devices/1-1*; do
  echo "$d: $(cat $d/idVendor 2>/dev/null) $(cat $d/idProduct 2>/dev/null) driver=$(basename "$(readlink $d/driver 2>/dev/null)")"
done
echo "== kernel log (usb/cdc)"
dmesg | grep -E "usb 1-1|cdc_ether|cdc_acm|usb0" | tail -20
