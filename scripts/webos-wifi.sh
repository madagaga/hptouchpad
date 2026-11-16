#!/bin/sh
# Runs ON the stock webOS: turns the Wi-Fi on through the webOS Wi-Fi service, waits, then reads
# the state of the hardware that the working driver sets up (GPIO, regulators, clocks, SDIO host).
# Usage: novacom put file:///tmp/wifi.sh < scripts/webos-wifi.sh ; novacom run file:///bin/sh /tmp/wifi.sh
mount -t debugfs none /sys/kernel/debug 2>/dev/null
LS=$(which luna-send 2>/dev/null || echo /usr/bin/luna-send)
echo "== luna-send: $LS"; ls -l $LS 2>&1
echo "== enable wifi"; $LS -n 1 palm://com.palm.wifi/setstate '{"state":"enabled"}' 2>&1
sleep 15
echo "== wifi status"; $LS -n 1 palm://com.palm.wifi/getstatus '{}' 2>&1
echo "== scan"; $LS -n 1 palm://com.palm.wifi/findnetworks '{}' 2>&1 | head -c 3000
sleep 5
echo "== net"; ifconfig -a 2>&1 | grep -A1 -E "^(wlan|eth)"
echo "== iw"; ls /sys/class/net/*/wireless 2>&1; ls /sys/class/net
echo "== modules"; cat /proc/modules
echo "== ath6k params"; for f in /sys/module/ar6000/parameters/* /sys/module/ath6kl*/parameters/*; do [ -f "$f" ] && echo "$f=$(cat $f 2>&1)"; done
echo "== gpio"; cat /sys/kernel/debug/gpio
echo "== regulators"
for r in /sys/class/regulator/*; do echo "-- $(cat $r/name 2>&1) $(cat $r/microvolts 2>&1) $(cat $r/state 2>&1) users=$(cat $r/num_users 2>&1)"; done
echo "== regulator debugfs"; ls /sys/kernel/debug/regulator | head -80
echo "== clocks (rate / enabled)"
for c in sdc1_clk sdc1_pclk sdc4_clk sdc4_pclk sdc4_p_clk sdc3_clk sdc2_clk gsbi6_uart_clk gsbi6_pclk gsbi6_p_clk gsbi5_uart_clk; do
  d=/sys/kernel/debug/clk/$c; [ -d $d ] && { echo "-- $c: $(for f in $d/*; do echo "$(basename $f)=$(cat $f 2>&1)"; done | tr '\n' ' ')"; }
done
ls /sys/kernel/debug/clk | grep -i -E "sdc|gsbi" | tr '\n' ' '; echo
echo "== mmc debugfs"; for d in /sys/kernel/debug/mmc0 /sys/kernel/debug/mmc1 /sys/kernel/debug/msmsdcc; do echo "-- $d"; ls $d 2>&1; for f in $d/ios $d/state $d/clock; do [ -f $f ] && { echo "$f:"; cat $f; }; done; done
echo "== sdio devices"; ls /sys/bus/mmc/devices /sys/bus/sdio/devices 2>&1
echo "== interrupts"; cat /proc/interrupts | grep -i -E "sdcc|sdc|wlan|bt|ath|gpio" 
echo "== dmesg (wifi related)"; dmesg | grep -i -E "ath|wlan|sdio|sdcc|mmc1|wifi|ar6|bluetooth|hci" | tail -80
echo "== iomem sdcc"; grep -i -E "sdc|msm_sdcc" /proc/iomem
echo "== end"
