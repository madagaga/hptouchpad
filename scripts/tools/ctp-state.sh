#!/bin/sh
# Runs ON the stock webOS (pushed through novacom, started detached because stopping hidd drops the novacom session): read-only dump of the
# touch controller state with catnip (same list as data/webos-touch/state-before-recalibration-2026-10-26.log). Nothing is set or flashed:
# every catnip call below is a getter. hidd is stopped and started again, the controller is suspended at the end. Output: /tmp/state.log
exec > /tmp/state.log 2>&1
date
stop hidd
sleep 1
echo 1 > /sys/devices/platform/cy8ctma395/vdd
catnip resume; echo "resume rc=$?"
for c in id cols pid mode subs cycles freq gain noise timer cmn jitter raw baseline signal idac; do
  echo "== $c"; catnip $c; echo "rc=$?"
done
echo "== ver"; catnip --i2c-only ver -a; echo "rc=$?"
echo "== sysfs"; for f in id checksum device_config; do echo "$f: $(cat /sys/devices/platform/cy8ctma395/$f 2>&1)"; done
catnip suspend; echo "suspend rc=$?"
start hidd
echo done
