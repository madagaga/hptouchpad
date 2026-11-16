#!/bin/sh
# Run in the persistent container, with the tablet booted into webOS (read-only):
#   docker exec hptouchpad-novacom sh /work/scripts/collect-webos.sh
# Results go to /work/data/webos/
OUT=/work/data/webos
mkdir -p "$OUT"

# Files: novacom get -> local file (name = flattened path).
for f in /proc/cpuinfo /proc/meminfo /proc/iomem /proc/ioports /proc/interrupts /proc/devices \
         /proc/partitions /proc/cmdline /proc/version /proc/modules /proc/misc /proc/clk \
         /proc/config.gz /proc/mtd /proc/bus/input/devices /proc/asound/cards /proc/asound/devices \
         /proc/tty/drivers /proc/fb \
         /sys/kernel/debug/gpio /sys/kernel/debug/clk/clk_summary /sys/kernel/debug/regulator/regulator_summary \
         /sys/class/graphics/fb0/modes /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/stride \
         /sys/class/graphics/fb0/bits_per_pixel /sys/class/graphics/fb0/name \
         /sys/class/power_supply/battery/uevent /sys/class/power_supply/usb/uevent \
         /etc/palm-build-info /etc/passwd; do
  name=$(echo "$f" | tr '/' '_')
  timeout 20 novacom get "file://$f" > "$OUT/$name" 2>/dev/null || rm -f "$OUT/$name"
  [ -s "$OUT/$name" ] || rm -f "$OUT/$name"
done

# Directory listings (webOS novacom does not allow `ls`, so these usually come back empty).
for d in /sys/bus/i2c/devices /sys/bus/platform/devices /sys/bus/spi/devices /sys/bus/mmc/devices \
         /sys/class/regulator /sys/class/input /sys/class/backlight /sys/class/leds /sys/class/net \
         /sys/class/mmc_host /sys/class/block /sys/class/power_supply /sys/class/sound /sys/class/video4linux \
         /dev /lib/modules /lib/firmware /boot; do
  name=$(echo "$d" | tr '/' '_')
  timeout 20 novacom run file:///bin/ls -l "$d" > "$OUT/ls$name" 2>/dev/null
  [ -s "$OUT/ls$name" ] || rm -f "$OUT/ls$name"
done

# Full dmesg (drivers, addresses, detected chips).
timeout 30 novacom run file:///bin/dmesg > "$OUT/dmesg.txt" 2>/dev/null
[ -s "$OUT/dmesg.txt" ] || rm -f "$OUT/dmesg.txt"

echo "Collected files:"; ls -l "$OUT"
