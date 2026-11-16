#!/bin/sh
# Runs ON the stock webOS (pushed through novacom, started detached): read-only loop of "catnip scan --count 1" (signal against the baseline, 40 columns x 30 rows)
# for 240 s, one line "T <epoch>" then the 1200 values per scan, in /tmp/scanloop.log. hidd is stopped and started again, the controller is suspended at the end.
exec > /tmp/scanloop.log 2>&1
stop hidd
sleep 1
echo 1 > /sys/devices/platform/cy8ctma395/vdd
catnip resume; echo "resume rc=$?"
end=$(( $(date +%s) + 240 ))
while [ $(date +%s) -lt $end ]; do
  echo "T $(date +%s)"
  catnip scan --count 1
done
catnip suspend; echo "suspend rc=$?"
start hidd
echo done
