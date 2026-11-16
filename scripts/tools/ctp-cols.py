#!/usr/bin/env python3
"""Reports which sensor columns/rows of the CY8CTMA395 carry data (noise or touch) in a raw UART capture
(lines `ff 43 <row> + 40 bytes`). Usage: python -I ctp-cols.py capture.raw"""
import re
import sys

d = open(sys.argv[1], 'rb').read()
cols = [0] * 40
rows = [0] * 30
lines = 0
for m in re.finditer(rb'\xff\x43', d):
    p = m.start()
    r = d[p + 2] & 0x1f
    row = d[p + 3:p + 43]
    if len(row) < 40 or r >= 30:
        continue
    lines += 1
    for j, v in enumerate(row):
        if 0 < v < 255:
            cols[j] += 1
            rows[r] += 1
active = [j for j in range(40) if cols[j] > lines * 0.02]
print('%d bytes, %d lines, %d scans' % (len(d), lines, d.count(b'\xff\x47')))
print('active columns (>2%% of lines): %s' % (('%d..%d (%d)' % (active[0], active[-1], len(active))) if active else 'none'))
print('per column:', cols)
print('per row:', rows)
