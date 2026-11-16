# Runs ON the tablet (serial shell or `sh -s` over SSH): pins cpu0 to each frequency of the OPP table and times the same shell loop,
# to check that the clock really changes (time must scale as 1/frequency). Restores the governor limits at the end.
P=/sys/devices/system/cpu/cpu0/cpufreq
MAXF=$(cat $P/cpuinfo_max_freq); MINF=$(cat $P/cpuinfo_min_freq)
for F in $(cat $P/scaling_available_frequencies); do
  echo $MINF > $P/scaling_min_freq; echo $F > $P/scaling_max_freq; echo $F > $P/scaling_min_freq
  sleep 1
  S=$(cut -d' ' -f1 /proc/uptime | tr -d .)
  taskset 1 sh -c 'i=0; while [ $i -lt 100000 ]; do i=$((i+1)); done'
  E=$(cut -d' ' -f1 /proc/uptime | tr -d .)
  echo "pinned $F kHz (reads $(cat $P/scaling_cur_freq)): $(( (E - S) * 10 )) ms"
done
echo $MINF > $P/scaling_min_freq; echo $MAXF > $P/scaling_max_freq
