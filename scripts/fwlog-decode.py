#!/usr/bin/env python3
"""Decodes an AR6003 firmware debug log captured with scripts/fwlog.sh.
Usage: python scripts/fwlog-decode.py out/fwlog.bin [--all]
The record format (timestamp, id, module, argument count) and the names come from the Atheros
dbglog.h / dbglog_id.h files read from the stock webOS (firmware/webos/ath6k/)."""
import re, struct, sys, collections, os

HERE = os.path.dirname(os.path.abspath(__file__))
IDS = os.path.join(HERE, "..", "firmware", "webos", "ath6k", "dbglog_id.h")
MODS = ['INF', 'WMI', 'MISC', 'PM', 'TXRX_MGMTBUF', 'TXRX_TXBUF', 'TXRX_RXBUF', 'WOW', 'WHAL',
        'DC', 'CO', 'RO', 'CM', 'MGMT', 'TMR', 'BTCOEX']
NOISE = (4, 5, 6)          # buffer pool modules, not interesting here

table, mod = {}, None
for line in open(IDS, errors="replace"):
    m = re.match(r"#define\s+(\w+)_DBGID_DEFINITION_START", line)
    if m:
        mod = m.group(1)
    m = re.match(r"#define\s+(\w+)\s+(\d+)\s*$", line)
    if m and mod:
        table.setdefault(mod, {})[int(m.group(2))] = m.group(1)

data = open(sys.argv[1], "rb").read()
words = struct.unpack("<%dI" % (len(data) // 4), data)
recs, i = [], 3                                   # 3 header words
while i < len(words):
    w = words[i]
    n = (w >> 30) & 3
    if i + 1 + n > len(words):
        break
    recs.append((w & 0xffff, (w >> 26) & 0xf, (w >> 16) & 0x3ff, words[i + 1:i + 1 + n]))
    i += 1 + n

def name(r):
    return table.get(MODS[r[1]], {}).get(r[2], "id%d" % r[2])

print("%d records, last timestamp %#x" % (len(recs), recs[-1][0] if recs else 0))
rx = [(r[0], r[3]) for r in recs if name(r) in ("WMI_CMD_RX", "WMI_CMD_RX_XTND")]
print("WMI commands received by the firmware (id:len@timestamp):")
print(" ".join("%s:%s@%x" % (hex(a[0]), a[1] if len(a) > 1 else "-", t) for t, a in rx))
print()
keys = {"DC_START_SEARCH", "DC_END_SEARCH", "DC_STOP_SEARCH", "DC_SCAN_CHAN_START", "DC_SCAN_CHAN_FINISH",
        "WMI_EVENT_SEND", "WMI_SENDING_SCAN_COMPLETE_EVENT", "WMI_TARGET_GET_STATS_CMD", "WMI_CMD_RX",
        "WMI_CMD_RX_XTND"}
last = None
for r in recs:
    n = name(r)
    if "--all" in sys.argv:
        ok = r[1] not in NOISE
    else:
        ok = n in keys
    if ok:
        s = "ts=%04x %-6s %-34s %s" % (r[0], MODS[r[1]], n, [hex(a) for a in r[3]])
        if s != last:
            print(s)
        last = s
