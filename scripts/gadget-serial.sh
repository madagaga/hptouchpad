#!/bin/sh
# Runs inside the Alpine WSL distro (root): listens on the tablet's USB serial gadget and sends a line.
# The tablet init writes "TABLET_ALIVE <uptime>" every 2 s on ttyGS0 and echoes what it receives.
#   wsl -d Alpine -u root -- sh -c "sh /mnt/d/SRC3/hptouchpad/scripts/gadget-serial.sh > /mnt/d/SRC3/hptouchpad/out/gadget-serial.log 2>&1"
echo "== device"
ls -l /dev/ttyACM0
stty -F /dev/ttyACM0 raw -echo 115200 2>&1
( timeout 14 cat /dev/ttyACM0 > /tmp/serial.out 2>&1 & )
sleep 6
echo "== sending ping line"
printf 'HELLO_FROM_HOST\n' > /dev/ttyACM0
sleep 9
echo "== received from tablet:"
cat /tmp/serial.out
echo "== done"
