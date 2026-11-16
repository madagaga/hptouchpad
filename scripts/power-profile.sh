# Runs ON the tablet (sh -s over SSH): differences of the battery current (uA, positive = charging) between states, with the tablet
# plugged on an adapter in the DC mode (charge-mode 1400): the adapter regulates, so a drop of the net current = more consumption.
cur() { cat /sys/class/power_supply/battery/current_now; }
avg() { s=0; n=0; i=0; while [ $i -lt 6 ]; do sleep 2; s=$((s + $(cur))); n=$((n+1)); i=$((i+1)); done; echo $((s / n)); }
echo "idle:            $(avg) uA"
taskset 1 sh -c 'while :; do :; done' & P1=$!
echo "1 CPU burning:   $(avg) uA"
taskset 2 sh -c 'while :; do :; done' & P2=$!
echo "2 CPUs burning:  $(avg) uA"
kill $P1 $P2 2>/dev/null; sleep 2
echo "idle again:      $(avg) uA"
