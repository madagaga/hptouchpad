# Runs ON the tablet (image without the TSENS driver). Enables the temperature sensor by hand in the ORDER OF THE
# MAINLINE DRIVER (enable first, thresholds written afterwards) and samples it for a while: if the tablet dies
# during the test, the TSENS block itself (hardware thermal trip) is the cause, not the Linux driver logic.
CNTL=0x00903620; THR=0x00903624; S0=0x00903628; INT=0x0090363c
echo "before: CNTL=$(devmem $CNTL 32) THR=$(devmem $THR 32) INT=$(devmem $INT 32)"
R=$(devmem $CNTL 32)
devmem $CNTL 32 $(( R | 2 ))
R2=$(( R | (1 << 24) | 1 | (1 << 18) | (1 << 3) ))
devmem $CNTL 32 $R2
echo "enabled: CNTL=$(devmem $CNTL 32) THR=$(devmem $THR 32) INT=$(devmem $INT 32)"
sleep 1
echo "after 1 s: S0=$(devmem $S0 32) INT=$(devmem $INT 32) THR=$(devmem $THR 32)"
# thresholds as the mainline driver writes them: MAX = 120 C (code 0xEF here), MIN = 0 C (code 0x44)
devmem $THR 32 $(( 0xEF000000 | (0x44 << 16) ))
i=0
while [ $i -lt 40 ]; do
  echo "t=$i S0=$(devmem $S0 32) INT=$(devmem $INT 32) THR=$(devmem $THR 32)"
  sleep 1; i=$((i+1))
done
devmem $CNTL 32 $R
echo "restored CNTL=$(devmem $CNTL 32)"
