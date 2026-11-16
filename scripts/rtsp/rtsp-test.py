#!/usr/bin/env python3
"""Minimal RTSP test client for tp-rtsp (RTP over TCP, interleaved): measures what arrives, no decoding.

  rtsp-test.py rtsp://<tablet>:8554/cam -t 30            play 30 s, print a summary
  rtsp-test.py URL -t 600 --every 60                     also print a line every 60 s (long runs, A/V sync drift)
  rtsp-test.py URL -t 5 --no-teardown                    close the socket without TEARDOWN (the server must clean up)
  rtsp-test.py URL -t 20 --stall 5                       play 5 s, then stop reading for the rest (a slow client)

Reports: time from PLAY to the first RTP packet of each track, packets, bytes, frames (marker bits), sequence gaps, and for
audio + video the A/V offset: for every packet the wall-clock time mapped through the first RTCP sender report is compared with
its arrival time; the difference between the two tracks is the sync error, its change over the run is the drift.
"""
import argparse
import re
import socket
import struct
import sys
import time


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(2)


class Client:
    def __init__(self, url):
        m = re.match(r"rtsp://([^:/]+)(?::(\d+))?(/.*)?$", url)
        if not m:
            die("bad URL")
        self.host, self.port, self.path = m.group(1), int(m.group(2) or 554), m.group(3) or "/"
        self.url = url
        self.sock = socket.create_connection((self.host, self.port), timeout=10)
        self.buf = b""
        self.cseq = 0
        self.session = None

    def send(self, method, url, headers=()):
        self.cseq += 1
        req = "%s %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: rtsp-test\r\n" % (method, url, self.cseq)
        if self.session:
            req += "Session: %s\r\n" % self.session
        for h in headers:
            req += h + "\r\n"
        self.sock.sendall((req + "\r\n").encode())
        return self.cseq

    def fill(self):
        d = self.sock.recv(65536)
        if not d:
            raise EOFError("connection closed")
        self.buf += d

    def response(self):
        """next RTSP response (interleaved frames before it are returned through self.frames)"""
        while True:
            if self.buf[:1] == b"$":
                f = self.frame()
                if f is None:
                    self.fill()
                    continue
                self.pending.append(f)
                continue
            i = self.buf.find(b"\r\n\r\n")
            if i < 0:
                self.fill()
                continue
            head = self.buf[:i].decode(errors="replace")
            m = re.search(r"(?i)^content-length:\s*(\d+)", head, re.M)
            n = int(m.group(1)) if m else 0
            if len(self.buf) < i + 4 + n:
                self.fill()
                continue
            body = self.buf[i + 4:i + 4 + n].decode(errors="replace")
            self.buf = self.buf[i + 4 + n:]
            code = int(head.split()[1])
            hd = {}
            for line in head.split("\r\n")[1:]:
                k, _, v = line.partition(":")
                hd[k.strip().lower()] = v.strip()
            return code, hd, body

    pending = None

    def pump(self):
        """read what is available; returns the interleaved frames, drops RTSP responses (keep-alive answers)"""
        frames = list(self.pending)
        self.pending = []
        self.fill()
        while self.buf:
            if self.buf[:1] == b"$":
                f = self.frame()
                if f is None:
                    break
                frames.append(f)
                continue
            i = self.buf.find(b"\r\n\r\n")
            if i < 0:
                break
            head = self.buf[:i].decode(errors="replace")
            m = re.search(r"(?i)^content-length:\s*(\d+)", head, re.M)
            n = int(m.group(1)) if m else 0
            if len(self.buf) < i + 4 + n:
                break
            self.buf = self.buf[i + 4 + n:]
        return frames

    def frame(self):
        if len(self.buf) < 4:
            return None
        n = struct.unpack(">H", self.buf[2:4])[0]
        if len(self.buf) < 4 + n:
            return None
        f = (self.buf[1], self.buf[4:4 + n])
        self.buf = self.buf[4 + n:]
        return f

    def request(self, method, url, headers=()):
        self.send(method, url, headers)
        code, hd, body = self.response()
        return code, hd, body


def av_offset(st):
    """audio minus video, in ms: the smallest (arrival time - capture time, from the first RTCP sender report of each track)
    seen since the last call; the minimum removes the delivery jitter. Resets the minima."""
    v = [s["wmin"] for s in st.values() if s["kind"] == "video" and s["wmin"] is not None]
    a = [s["wmin"] for s in st.values() if s["kind"] == "audio" and s["wmin"] is not None]
    for s in st.values():
        s["wmin"] = None
    if v and a:
        print("   raw minimum of (PC arrival time - tablet wall clock at capture): video %.3f s, audio %.3f s" % (v[0], a[0]))
    return (a[0] - v[0]) * 1000 if v and a else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("url")
    ap.add_argument("-t", "--seconds", type=float, default=10)
    ap.add_argument("--every", type=float, default=0, help="print a status line every N seconds")
    ap.add_argument("--no-teardown", action="store_true")
    ap.add_argument("--stall", type=float, default=0, help="stop reading after N seconds (slow client)")
    ap.add_argument("--tracks", default="", help="only these track ids, e.g. 0 or 1 (default all offered)")
    a = ap.parse_args()

    c = Client(a.url)
    c.pending = []
    code, hd, body = c.request("OPTIONS", a.url)
    if code != 200:
        die("OPTIONS %d" % code)
    code, hd, sdp = c.request("DESCRIBE", a.url, ["Accept: application/sdp"])
    if code != 200:
        die("DESCRIBE %d" % code)
    base = hd.get("content-base", a.url)
    media = [(m.group(1), m.group(2)) for m in re.finditer(r"m=(\w+) [^\n]*\n(?:(?!m=)[^\n]*\n)*?a=control:([^\r\n]+)", sdp)]
    if not media:
        die("no media in the SDP:\n" + sdp)
    print("SDP: " + ", ".join("%s (%s)" % m for m in media))
    chans = {}
    for i, (kind, ctl) in enumerate(media):
        if a.tracks and ctl[-1] not in a.tracks:
            continue
        url = ctl if ctl.startswith("rtsp://") else base + ctl
        code, hd, _ = c.request("SETUP", url, ["Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d" % (2 * i, 2 * i + 1)])
        if code != 200:
            die("SETUP %s: %d" % (url, code))
        c.session = hd["session"].split(";")[0]
        chans[2 * i] = kind
    t_play = time.time()
    code, hd, _ = c.request("PLAY", base)
    if code != 200:
        die("PLAY %d" % code)
    print("PLAY ok, RTP-Info: " + hd.get("rtp-info", "-"))

    st = {ch: dict(kind=k, pkts=0, bytes=0, frames=0, gaps=0, seq=None, first=None, sr=None, last_ts=None, d0=None, dn=None, wmin=None, w0=None)
          for ch, k in chans.items()}
    clock = {"video": 90000.0, "audio": 8000.0}
    hist = []
    t_end = t_play + a.seconds
    next_print = t_play + a.every if a.every else None
    last_keep = t_play
    stalled = False
    c.sock.settimeout(1.0)
    while time.time() < t_end:
        if a.stall and time.time() - t_play > a.stall:
            if not stalled:
                print("not reading any more (slow client)")
                stalled = True
            time.sleep(0.5)
            continue
        if time.time() - last_keep > 20:
            c.send("GET_PARAMETER", base)
            last_keep = time.time()
        try:
            frames = c.pump()
        except socket.timeout:
            continue
        except EOFError:
            print("connection closed by the server")
            break
        now = time.time()
        for ch, pkt in frames:
            if ch in st and len(pkt) >= 12:
                s = st[ch]
                seq, ts = struct.unpack(">HI", pkt[2:4] + pkt[4:8])
                if s["first"] is None:
                    s["first"] = now - t_play
                if s["seq"] is not None and seq != (s["seq"] + 1) & 0xffff:
                    s["gaps"] += 1
                s["seq"] = seq
                s["pkts"] += 1
                s["bytes"] += len(pkt)
                s["frames"] += pkt[1] >> 7
                s["last_ts"] = ts
                if s["sr"]:
                    ntp = s["sr"][0] + ((ts - s["sr"][1]) & 0xffffffff if ((ts - s["sr"][1]) & 0xffffffff) < 2**31 else ((ts - s["sr"][1]) & 0xffffffff) - 2**32) / clock[s["kind"]]
                    d = now - ntp
                    if s["d0"] is None:
                        s["d0"] = d
                    s["dn"] = d
                    if s["wmin"] is None or d < s["wmin"]:
                        s["wmin"] = d
            elif ch - 1 in st and len(pkt) >= 28 and pkt[1] == 200:
                s = st[ch - 1]
                msw, lsw, rts = struct.unpack(">III", pkt[8:20])
                if not s["sr"]:
                    s["sr"] = (msw + lsw / 2**32, rts)
        if next_print and now >= next_print:
            next_print += a.every
            line = "%6.0f s:" % (now - t_play)
            for s in st.values():
                line += " %s %d pkts %d frames %d gaps;" % (s["kind"], s["pkts"], s["frames"], s["gaps"])
            off = av_offset(st)
            if off is not None:
                hist.append(off)
                line += " A/V offset %+.0f ms (change since the first window %+.0f ms)" % (off, off - hist[0])
            print(line, flush=True)

    dur = time.time() - t_play
    ok = True
    for s in st.values():
        print("%-5s first packet %s after PLAY, %d packets, %d bytes (%.0f kbit/s), %d frames (%.1f/s), %d sequence gaps" % (
            s["kind"], "%.2f s" % s["first"] if s["first"] is not None else "never", s["pkts"], s["bytes"],
            s["bytes"] * 8 / dur / 1000, s["frames"], s["frames"] / dur, s["gaps"]))
        ok = ok and s["pkts"] > 0
    off = av_offset(st)
    if off is not None:
        hist.append(off)
    if hist:
        print("A/V offset (audio minus video, windows: %d): first %+.0f ms, last %+.0f ms, change %+.0f ms, extremes %+.0f / %+.0f ms" % (
            len(hist), hist[0], hist[-1], hist[-1] - hist[0], min(hist), max(hist)))
    if not a.no_teardown:
        try:
            c.sock.settimeout(3)
            c.send("TEARDOWN", base)
        except OSError:
            pass
    c.sock.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
