# Runs ON the tablet (pushed through the serial shell): reads the SoC temperature sensor 0 by hand with
# devmem, with the sequence of the downstream drivers/thermal/msm_tsens.c (MSM8x60): QFPROM 0xBC holds the
# calibration code at 30 C (bits 23:16, redundant copy in 31:24 wins when non-zero), slope 0.702 C/code.
CNTL=0x00903620; THR=0x00903624; S0=0x00903628; INT=0x0090363c; QF=0x007000BC
Q=$(devmem $QF 32); echo "QFPROM[0xBC]=$Q"
cal=$(( (Q >> 16) & 255 )); red=$(( (Q >> 24) & 255 )); [ $red != 0 ] && cal=$red
echo "calibration code at 30C: $cal (primary $(( (Q >> 16) & 255 )), redundant $red)"
R=$(devmem $CNTL 32); echo "CNTL before=$R"
devmem $CNTL 32 $(( R | 2 ))                         # SW_RST
R2=$(( R | (1 << 24) | 1 | (4 << 16) | (1 << 9) | (1 << 10) | (1 << 8) | (1 << 11) | (1 << 3) ))
R2=$(( (R2 & ~(3 << 28)) | (1 << 28) ))              # TSENS_CONFIG = 1
devmem $CNTL 32 $R2
devmem $THR 32 $(( 0x50 | (0xdf << 8) | (0x38 << 16) | (0xff << 24) ))
echo "CNTL after=$(devmem $CNTL 32)"
i=0; while [ $i -lt 50 ]; do [ $(( $(devmem $INT 32) & 0x80 )) != 0 ] && break; sleep 0.1; i=$((i+1)); done
echo "TRDY after $i x 100 ms, INT_STATUS=$(devmem $INT 32)"
n=0; while [ $n -lt 5 ]; do
  code=$(( $(devmem $S0 32) & 255 ))
  echo "sensor0 code=$code -> $(( (code * 702 + 30000 - 702 * cal) / 1000 )) C"
  sleep 1; n=$((n+1))
done
for k in 1 2 3 4; do echo "sensor$k raw=$(devmem $((0x00903628 + 4 * k)) 32)"; done
devmem $CNTL 32 $(( R & ~((1 << 24) | 1 | (1 << 3)) ))   # back to disabled
echo "CNTL restored=$(devmem $CNTL 32)"
