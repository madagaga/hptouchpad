#!/bin/sh
# Runs ON the stock webOS (pushed through novacom, started detached): read-only loop on the touch controller raw buffer (catnip raw getter) for 180 s,
# one line "T <epoch>" then the 1200 values per sample, in /tmp/rawloop.log. hidd is stopped and started again, the controller is suspended at the end.
exec > /tmp/rawloop.log 2>&1
stop hidd
sleep 1
echo 1 > /sys/devices/platform/cy8ctma395/vdd
catnip resume; echo "resume rc=$?"
end=$(( $(date +%s) + 180 ))
while [ $(date +%s) -lt $end ]; do
  echo "T $(date +%s)"
  catnip raw
done
catnip suspend; echo "suspend rc=$?"
start hidd
echo done
