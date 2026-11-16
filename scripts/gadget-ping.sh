#!/bin/sh
# Runs inside the Alpine WSL distro (root): sends traffic to the tablet for a while so that the
# counters on the tablet screen move. Usage:
#   wsl -d Alpine -u root -- sh -c "sh /mnt/d/SRC3/hptouchpad/scripts/gadget-ping.sh > /mnt/d/SRC3/hptouchpad/out/gadget-ping.log 2>&1"
ip addr flush dev usb0 2>/dev/null
ip addr add 192.168.7.1/24 dev usb0
ip link set usb0 up
sleep 2
ping -c 40 -i 1 -W 1 192.168.7.2
echo "== host usb0 rx/tx packets:"
cat /sys/class/net/usb0/statistics/rx_packets /sys/class/net/usb0/statistics/tx_packets | tr '\n' ' '
echo
ip neigh show dev usb0
