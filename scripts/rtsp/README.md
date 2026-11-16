# RTSP server `tp-rtsp` (and the go2rtc alternative)

`tp-rtsp` is a small C program (about 39 KB, sources in `rootfs/tp-rtsp/`, design in [docs/RTSP-SERVER.md](../../docs/RTSP-SERVER.md))
that serves the front camera (hardware H.264 encoder) and the internal microphone over RTSP. It is **not in the default image**.

| URL | Content |
|---|---|
| `rtsp://<tablet>:8554/cam` | video + audio |
| `rtsp://<tablet>:8554/video` | video only |
| `rtsp://<tablet>:8554/mic` | audio only |

- Video: H.264 Constrained Baseline 640x480, about 14-15 fps, 1 Mbit/s CBR, GOP 30, RFC 6184 (single NAL + FU-A), SPS/PPS before
  each IDR. The picture is rotated 90 degrees (sensor orientation): rotate in the client.
- Audio: G.711 A-law (PCMA) 8 kHz mono, 20 ms packets (the microphone is 48 kHz stereo, averaged, low-passed, decimated).
- RTP only **interleaved in the RTSP TCP connection**: use `-rtsp_transport tcp` (VLC falls back to TCP by itself, or use
  `--rtsp-tcp`). No UDP, no authentication, no TLS. RTCP sender reports are sent (audio/video sync); client RTCP is ignored.
- The camera, the encoder and the microphone are opened at the first PLAY and released when the last client leaves. Up to 8
  clients. A slow client loses frames (its video resumes at the next IDR) and is dropped after 15 s without progress; an idle one
  after 90 s. Start-up: the first picture packet arrives about 1.1-1.4 s after PLAY (camera start); ffmpeg-based players show
  the first picture about 3 s after PLAY because they analyse 20 frames first.

## Build and install

```sh
RTSP=1 VIDC_FW=webos DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh rootfs   # image with /usr/local/bin/tp-rtsp (run with niu -c on Windows)
VIDC_FW=webos DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh tp-rtsp          # the binary alone: out/tp-rtsp/tp-rtsp
```

Without `RTSP=1` the image is unchanged (the only difference with `RTSP=1` is that one file). To try it on a running
tablet, copy the binary: `ssh root@<tablet> dd of=/usr/local/bin/tp-rtsp bs=64k < out/tp-rtsp/tp-rtsp`, then `chmod 755` it.
Needs a kernel with the camera, vidc (firmware, `VIDC_FW=webos`) and the microphone capture link.

## Run

```sh
tp-rtsp > /tmp/rtsp.log 2>&1 &         # not started at boot
tp-rtsp -h                              # options: -p port, -b bit rate, -g GOP, -r fps, -c/-e device, -m PCM, -G mic gain, -v
```

Client examples: `ffplay -rtsp_transport tcp rtsp://<tablet>:8554/cam`, VLC (open the URL).

Test aids (no client needed): `tp-rtsp --record out.h264 -s 10 [-n cycles]` (camera -> encoder -> file, one start/stop per cycle),
`--selftest out.h264` (the same with a synthetic moving picture, no camera), `--fake` (server with the synthetic picture).
Check a file with `ffprobe` / `ffplay out.h264`.

`scripts/rtsp/rtsp-test.py <url> -t 600 --every 60` is a PC-side test client (Python 3, no decoding): time to the first packet,
packets, frame rate, sequence gaps, and the audio/video offset and its drift (`--no-teardown`, `--stall N` to test the server).

Things to know: the camera starts about once in four with near-black frames at about 3 fps; the server detects it (fewer than 8
frames in 1.5 s) and restarts the camera, up to three times. The microphone buffer holds only two 128 ms periods; the process
runs at nice -10 so that the loop is never late (without it audio overruns happened every 1-2 minutes).

## Alternative: go2rtc + ffmpeg

Not part of the image; 61 MB installed by hand with `apk add go2rtc v4l-utils` (pulls ffmpeg), lost at the next rootfs flash.
`go2rtc.yaml` serves the same URLs (audio AAC instead of PCMA) plus a web page on port 1984 and WebRTC/MSE players.

```bat
ssh root@<tablet> "cat > /etc/go2rtc/go2rtc.yaml" < scripts\rtsp\go2rtc.yaml
```

```sh
go2rtc -config /etc/go2rtc/go2rtc.yaml > /tmp/go2rtc.log 2>&1 &
```

ffmpeg starts at the first client and stops with the last one; the camera and the microphone serve one stream at a time.
