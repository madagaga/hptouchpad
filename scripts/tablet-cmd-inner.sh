#!/bin/sh
# Runs inside the Alpine WSL distro (root): sends the command stored in out/tablet-cmd.txt to the
# shell of the tablet's USB serial gadget (/dev/ttyACM0) and writes the reply to out/tablet-reply.txt.
# Used by scripts/tablet-cmd.sh. Usage: tablet-cmd-inner.sh <seconds to wait for the reply>
WAIT="${1:-3}"
# /dev/ttyACM0 must be the real character device of the attached gadget: a plain file with that name (created
# by a write while the device was absent) silently swallows every command and shows a stale reply.
if [ ! -c /dev/ttyACM0 ]; then
  rm -f /dev/ttyACM0
  echo "serial device /dev/ttyACM0 missing: re-attach the gadget (usbipd detach/attach)" > /mnt/d/SRC3/hptouchpad/out/tablet-reply.txt
  exit 1
fi
DIR=/mnt/d/SRC3/hptouchpad/out
stty -F /dev/ttyACM0 raw -echo 115200 2>/dev/null
rm -f /tmp/reply
( timeout $((WAIT + 2)) cat /dev/ttyACM0 > /tmp/reply 2>/dev/null & )
sleep 1
printf '\n' > /dev/ttyACM0
# Send the command followed by a marker so the end of the output is recognisable.
{ cat "$DIR/tablet-cmd.txt"; printf '\necho __END__\n'; } > /dev/ttyACM0
sleep "$WAIT"
cp /tmp/reply "$DIR/tablet-reply.txt"
