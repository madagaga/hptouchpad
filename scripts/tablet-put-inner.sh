#!/bin/sh
# Runs inside the Alpine WSL distro (root): sends out/tablet-cmd.txt to the tablet's USB serial shell one line at a time
# with a short pause (the tty of the gadget loses data when it is fed faster than the shell consumes it), then waits
# for the reply and writes it to out/tablet-reply.txt. Used by scripts/tablet-put.sh. Usage: tablet-put-inner.sh <seconds to wait at the end>
WAIT="${1:-5}"
DIR=/mnt/d/SRC3/hptouchpad/out
if [ ! -c /dev/ttyACM0 ]; then
  rm -f /dev/ttyACM0
  echo "serial device /dev/ttyACM0 missing: re-attach the gadget (usbipd detach/attach)" > "$DIR/tablet-reply.txt"
  exit 1
fi
stty -F /dev/ttyACM0 raw -echo 115200 2>/dev/null
rm -f /tmp/reply
( timeout 600 cat /dev/ttyACM0 > /tmp/reply 2>/dev/null & )
sleep 1
printf '\n' > /dev/ttyACM0
while IFS= read -r line; do
  printf '%s\n' "$line" > /dev/ttyACM0
  sleep 0.25
done < "$DIR/tablet-cmd.txt"
printf 'echo __END__\n' > /dev/ttyACM0
sleep "$WAIT"
pkill -f 'cat /dev/ttyACM0' 2>/dev/null
cp /tmp/reply "$DIR/tablet-reply.txt"
