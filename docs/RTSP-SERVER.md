# `tp-rtsp`: a small RTSP server for the TouchPad

`tp-rtsp` is one small C program (about 39 KB, `rootfs/tp-rtsp/`) that serves the front camera (hardware H.264) and the internal
microphone over RTSP. Usage, options and test aids: [scripts/rtsp/README.md](../scripts/rtsp/README.md). This document describes its
design.

| URL | Content |
|---|---|
| `rtsp://<tablet>:8554/cam` | video + audio |
| `rtsp://<tablet>:8554/video` | video only |
| `rtsp://<tablet>:8554/mic` | audio only |

## Scope

- Opt-in build setting, not in the default image: `RTSP=1 ./build.sh rootfs` (or `all`). Not started at boot.
- Our own code, no media framework: only libc and, for audio, alsa-lib (already in the image through `alsa-utils`). Linked dynamically
  against musl and `libasound`, `-O2 -W -Wall`, no warning.
- RTP only over the RTSP TCP connection (interleaved, RFC 2326 section 10.12). No UDP transport, no authentication, no TLS, no WebRTC,
  one resolution (640x480). RTCP sender reports are sent for audio/video sync; client RTCP is ignored.
- Audio: G.711 A-law (PCMA, 8 kHz mono, static payload type 8, 20 ms packets). The 48 kHz stereo microphone is averaged, low-passed and
  decimated by 6 in the program.
- Needs a kernel with the camera, the vidc codec and the microphone capture link, and the codec firmware (`VIDC_FW=webos`, see
  [FIRMWARE.md](FIRMWARE.md)).

## The devices

Looked up by name, not by number (`/sys/class/video4linux/*/name`).

- **Camera** `/dev/video0`, driver `msm8660-vfe` (`kernel/src/drivers/media/platform/qcom/msm8660_vfe.c`): single-planar capture, NV12
  640x480 only (460800 bytes per frame), about 15 fps. The picture is rotated 90 degrees (sensor orientation); RTSP has no standard
  rotation signal, so the client rotates it. One opener at a time.
- **Encoder** `/dev/video2`, "Qualcomm VIDC 1080p Encoder" (`vidc_enc.c`): V4L2 mem2mem, multi-planar API. OUTPUT queue = raw linear NV12,
  CAPTURE queue = `V4L2_PIX_FMT_H264`; both `MMAP` and `DMABUF`. Controls used: `BITRATE`, `BITRATE_MODE`, `GOP_SIZE`, `FORCE_KEY_FRAME`,
  `HEADER_MODE`, `H264_PROFILE`, `H264_LEVEL`; frame rate through `VIDIOC_S_PARM` on the OUTPUT queue; `VIDIOC_ENCODER_CMD` for the
  drain. The stream is H.264 Constrained Baseline in Annex B. `ffmpeg -c:v h264_v4l2m2m` drives it unchanged.
- **Microphone** ALSA `hw:0,1`, S16_LE, 2 channels, 48 kHz. The `tp` daemon sets the mixer path; `tp-rtsp` only opens the PCM. The PCM
  offers only two periods of 6144 frames (128 ms), and its DMA position moves continuously, so a read-until-empty loop never ends.

## Architecture

A single process and a single `poll()` loop, no threads.

| File | Role |
|---|---|
| `main.c` | options, the poll loop, pipeline start and stop on demand |
| `rtsp.c` | TCP listener, per-client state, request parser, OPTIONS / DESCRIBE / SETUP / PLAY / TEARDOWN / GET_PARAMETER, SDP |
| `rtp.c` | RTP header, H.264 packetisation (RFC 6184: single NAL unit and FU-A, packetization-mode 1), PCMA packets, interleaved `$` framing |
| `capture.c` | camera: `VIDIOC_S_FMT`, MMAP buffers, stream on/off, dequeue |
| `encoder.c` | encoder: formats, controls, queues, Annex B split into NAL units, SPS/PPS cache |
| `audio.c` | ALSA capture, downmix, resample to 8 kHz, A-law |

Behaviour:

- The pipeline (camera, encoder, microphone) opens at the first PLAY and closes when the last client leaves. Up to 8 clients.
- Video SDP: `m=video 0 RTP/AVP 96`, `a=rtpmap:96 H264/90000`, `a=fmtp:96 packetization-mode=1;profile-level-id=...;sprop-parameter-sets=...`.
- A key frame is forced when a client starts playing. The encoder emits SPS/PPS only in the first frame after start, so they are cached
  and sent before every IDR. A new client receives nothing until the next IDR.
- A slow client never blocks the others or the capture: non-blocking sockets and a bounded per-client queue. When the queue overflows,
  that client's video is dropped until the next IDR; a client without progress for 15 s, or idle for 90 s, is closed.
- Timestamps: video 90 kHz and audio 8 kHz from the same `CLOCK_MONOTONIC` origin. The camera capture timestamp is carried through the
  encoder (the m2m driver copies it from the OUTPUT to the CAPTURE buffer).
- Camera to encoder is a `memcpy` into the encoder's OUTPUT buffer (460 kB at 15 fps is negligible).
- If fewer than 8 frames arrive within 1.5 s after the first one, the camera is restarted (up to three tries): see the camera start
  limitation in the README of `scripts/rtsp/`.
- Every length that comes from the network (request line, headers, `Content-Length`, interleaved frame size) is validated and the request
  size is capped. The process runs at `nice -10`; without it the microphone loop was late and the audio overran every 1-2 minutes.

## Build

- `RTSP=1 ./build.sh rootfs` installs `/usr/local/bin/tp-rtsp`; `./build.sh tp-rtsp` builds the binary alone (`out/tp-rtsp/tp-rtsp`).
  The `Dockerfile` has the stages `tp-rtsp`, `tp-rtsp-bin` and, in `rootfs-tree`, a conditional install on `ARG RTSP`.
- Without `RTSP=1` the image is unchanged: the only difference with `RTSP=1` is that one file.

## Measured behaviour

| Test | Result |
|---|---|
| Encoder alone (`--selftest`) | 10 s of synthetic picture: 146 frames, first frame IDR, GOP 30 honoured, full decode without error |
| Camera to file | 10 s: 148 frames; camera stamp to output 17-33 ms; 20 starts and stops in a row without a failure |
| Video, one client | First packet 1.1-1.3 s after PLAY, 14-15 fps, no sequence gap |
| On demand, several clients | 2 clients at once; the pipeline stops when both leave; 20 connect and disconnect cycles, half without TEARDOWN; no fd or memory growth (RSS 1.2-1.6 MB) |
| Audio | `/mic` 50 packets/s, tones from 500 to 3000 Hz come back at the right frequencies; `/cam` A/V offset +10 ms, drift +3 ms over 7 min |
| Slow client | Loses frames, resumes at the next IDR, closed when its queue overflows |

Not tested: UDP (not supported), more than 2 clients, VLC and ffplay playback, speech intelligibility, a one-hour run, A/V sync with
players other than the test client.
