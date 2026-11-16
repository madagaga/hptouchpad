#!/bin/sh
# Runs inside the Alpine WSL distro (root), with the tablet's USB gadget (1d6b:0104) attached with usbipd:
#   wsl -d Alpine -u root -- sh /mnt/d/SRC3/hptouchpad/scripts/gadget-test.sh
# Configures the host side of the USB network and tests the network and serial channels.
ip addr flush dev usb0 2>/dev/null
ip addr add 192.168.7.1/24 dev usb0
ip link set usb0 up
sleep 2

echo "== host usb0"
ip -o addr show usb0
cat /sys/class/net/usb0/statistics/rx_packets /sys/class/net/usb0/statistics/tx_packets 2>/dev/null | tr '\n' ' '
echo

echo "== ping 192.168.7.2"
ping -c 3 -W 2 192.168.7.2

echo "== neighbours"
ip neigh show dev usb0

echo "== telnet port 23"
( sleep 1; echo "uname -a"; sleep 2 ) | timeout 5 nc 192.168.7.2 23 | head -5

echo "== serial /dev/ttyACM0"
stty -F /dev/ttyACM0 raw -echo 115200 2>&1
( timeout 6 cat /dev/ttyACM0 > /tmp/serial.out & )
sleep 1
printf '\recho SERIAL_OK\r' > /dev/ttyACM0
sleep 3
cat /tmp/serial.out
echo "== done"
