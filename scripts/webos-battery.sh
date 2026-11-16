# Runs ON the stock webOS (pushed through novacom): reads the battery gauge and the charger as the vendor
# kernel sees them (power_supply class and the A6 sysfs attributes). Read-only.
for d in /sys/class/power_supply/*; do
  echo "== $d"
  for f in type status health present online capacity voltage_now current_now temp technology charge_full charge_now; do
    [ -f $d/$f ] && echo "$f=$(cat $d/$f 2>&1)"
  done
done
for d in /sys/bus/i2c/devices/*-0031 /sys/bus/i2c/devices/*-0032; do
  [ -d $d ] || continue
  echo "== A6 $d"
  for f in getpercent getvoltage getcurrent getavgcurrent gettemp getfull status; do
    [ -f $d/$f ] && echo "$f=$(cat $d/$f 2>&1)"
  done
done
echo "== charger gpios (webOS debugfs)"
mount -t debugfs none /sys/kernel/debug 2>/dev/null
grep -E "gpio-(30|33|34|35|36|41|42|133|140) " /sys/kernel/debug/gpio
echo "== uptime"; cat /proc/uptime
