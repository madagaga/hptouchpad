#!/usr/bin/env python3
"""Wi-Fi TCP throughput of the tablet, measured from the PC (the tablet is the server: no PC firewall change needed).
Usage: python scripts/wifi-throughput.py <tablet ip> [megabytes]   (uses scripts/tablet-ssh.sh to start the servers)"""
import socket, subprocess, sys, time, os
ip = sys.argv[1]
mb = int(sys.argv[2]) if len(sys.argv) > 2 else 10
here = os.path.dirname(os.path.abspath(__file__))
KEY = os.path.join(here, "..", "secrets", "touchpad_ed25519")
def ssh(cmd):
    return subprocess.run(["ssh", "-i", KEY, "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=NUL",
                           "-o", "ConnectTimeout=10", "-o", "BatchMode=yes", "root@" + ip, cmd],
                          capture_output=True, text=True, timeout=120)

def connect(port):
    for _ in range(20):
        try:
            return socket.create_connection((ip, port), timeout=5)
        except OSError:
            time.sleep(0.5)
    raise OSError("port %d never opened" % port)

def upload():                      # PC -> tablet
    ssh("setsid sh -c 'timeout 90 busybox nc -l -p 5001 > /dev/null' > /dev/null 2>&1 < /dev/null &")
    s = connect(5001)
    buf = b"\0" * 65536
    n = mb * 16
    t = time.time()
    for _ in range(n):
        s.sendall(buf)
    s.close()
    dt = time.time() - t
    return mb * 8 / dt, dt

def download():                    # tablet -> PC
    ssh("setsid sh -c 'dd if=/dev/zero bs=65536 count=%d 2>/dev/null | timeout 90 busybox nc -l -p 5002' > /dev/null 2>&1 < /dev/null &" % (mb * 16))
    s = connect(5002)
    total = 0
    t = time.time()
    while True:
        d = s.recv(65536)
        if not d:
            break
        total += len(d)
    dt = time.time() - t
    return total * 8 / 1e6 / dt, dt

for name, fn in (("upload   PC -> tablet", upload), ("download tablet -> PC", download)):
    try:
        mbps, dt = fn()
        print("%s: %.2f Mbit/s (%d MB in %.1f s)" % (name, mbps, mb, dt))
    except Exception as e:
        print("%s: failed (%s)" % (name, e))
