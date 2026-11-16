#!/bin/sh
# Runs ON the stock webOS (pushed by scripts/webos-run.sh): reads the hardware state while the Wi-Fi
# works, to compare with our mainline device tree. Read-only.
mount -t debugfs none /sys/kernel/debug 2>&1
echo "== id =="; id; uname -a
echo "== debugfs =="; ls /sys/kernel/debug 2>&1
echo "== net =="; ls /sys/class/net; ifconfig -a 2>&1 | head -30
echo "== gpio =="; cat /sys/kernel/debug/gpio 2>&1
echo "== regulators =="; ls /sys/class/regulator 2>&1
for r in /sys/class/regulator/*; do echo "-- $r $(cat $r/name 2>&1) $(cat $r/microvolts 2>&1) $(cat $r/state 2>&1) $(cat $r/num_users 2>&1)"; done
echo "== clk =="; ls /sys/kernel/debug/clk 2>&1 | head; cat /proc/clk 2>&1 | head -5
echo "== mmc =="; ls /sys/class/mmc_host /sys/bus/mmc/devices 2>&1
for h in /sys/class/mmc_host/*; do echo "-- $h"; ls $h; done
echo "== sdio =="; for d in /sys/bus/mmc/devices/*; do echo "-- $d $(cat $d/type 2>&1) $(cat $d/vendor 2>&1) $(cat $d/device 2>&1)"; done
echo "== modules =="; cat /proc/modules
echo "== wlan =="; ls /sys/class/net/*/wireless 2>&1; ls /sys/kernel/debug/ieee80211 2>&1
echo "== ath6k params =="; ls /sys/module | grep -i -E "ath|wlan|bt|hci"; for f in /sys/module/ath6kl*/parameters/*; do echo "$f=$(cat $f 2>&1)"; done
echo "== processes =="; ps | grep -i -E "wpa|wifi|wlan|ath|bt|hci|hcid|bluez|sdio|connman|mojo|ConnectionManager|wan" | head -30
echo "== end =="
