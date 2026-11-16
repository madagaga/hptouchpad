# Runs ON the tablet (serial shell or `sh -s` over SSH): battery current (uA, positive = charging) for several backlight levels, 12 s of settling
# (the gauge lags) then the average of 8 samples. The charger must stay in one mode (USB, 500 mA, or the DC mode) for the whole run.
for L in 100 60 30 10 0 100; do tp backlight $L > /dev/null; sleep 12; s=0; i=0; while [ $i -lt 8 ]; do sleep 2; s=$((s + $(cat /sys/class/power_supply/battery/current_now))); i=$((i+1)); done; echo "backlight $L %: $((s / 8000)) mA"; done; tp backlight 60
