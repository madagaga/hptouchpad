# Task brief: `tp-rtsp`, a small RTSP server for the TouchPad

Written on 2026-11-14 for the agent that implements it. Read [MEMORY.md](../MEMORY.md) first (owner's rules, tablet workflow,
build on this host, pitfalls), then [STATUS.md](../STATUS.md). The owner speaks French in the chat; everything in the repository
is English, with LF line endings.

## What to build

One small C program, `tp-rtsp`, that serves the front camera (hardware H.264) and the internal microphone over RTSP, to
replace the go2rtc + ffmpeg test setup of `scripts/rtsp/` (61 MB installed by hand) with a binary of a few tens of kilobytes.

| URL | Content |
|---|---|
| `rtsp://<tablet>:8554/cam` | video + audio |
| `rtsp://<tablet>:8554/video` | video only |
| `rtsp://<tablet>:8554/mic` | audio only |

Clients to satisfy: `ffplay -rtsp_transport tcp rtsp://<tablet>:8554/cam` and VLC, from a PC on the same network.

## Decisions already taken with the owner (do not reopen)

- **Not in the default image.** It is an opt-in build setting: `RTSP=1 ./build.sh rootfs` (or `all`). Without it the image must
  be byte-for-byte what it is today. No boot service: the binary is started by hand.
- **Our own code, no media framework**: no GStreamer, no ffmpeg, no live555. Only libc and, for audio, alsa-lib (already in the
  image through `alsa-utils`).
- **RTP over the RTSP TCP connection only** (interleaved, RFC 2326 section 10.12). No UDP transport, no RTCP processing beyond
  ignoring what the client sends, no authentication, no TLS, no WebRTC, one resolution (640x480).
- **Audio codec: G.711 A-law (PCMA, 8 kHz mono, static payload type 8)**, the owner's default unless they say otherwise. It
  needs no library. Downsample 48 kHz stereo to 8 kHz mono in the program (average the two channels, simple low-pass + decimate
  by 6).
- **Video first**: steps 1 to 4 below complete before any audio work.
- **Prerequisite**: the base must be consolidated first (STATUS.md "Next steps", 1): a full build from the current sources,
  RAM-tested, ideally flashed. Do not start step 2 on a tablet whose kernel lacks the camera start fix, vidc or the capture link.

## The devices (what the kernel provides)

Check the device numbers on the tablet with `v4l2-ctl --list-devices` (or `/sys/class/video4linux/*/name`); do not hard-code
them without a lookup by name, the numbers below are what was seen on 2026-11-13.

- **Camera** `/dev/video0`, driver `msm8660-vfe` (`kernel/src/drivers/media/platform/qcom/msm8660_vfe.c`): single-planar
  capture, NV12 640x480 only (460800 bytes per frame), about 15 fps delivered. The picture is rotated 90 degrees (sensor
  orientation); RTSP has no standard rotation signal, leave it to the client. It can be opened by one process at a time.
- **Encoder** `/dev/video2`, "Qualcomm VIDC 1080p Encoder" (`kernel/src/drivers/media/platform/qcom/vidc/vidc_enc.c`): V4L2
  mem2mem, **multi-planar** API. OUTPUT queue = raw NV12 (`V4L2_PIX_FMT_NV12`, linear; do not use the tiled `NV12MT`), CAPTURE
  queue = `V4L2_PIX_FMT_H264`. Both queues support `MMAP` and `DMABUF`. Controls present in the driver:
  `V4L2_CID_MPEG_VIDEO_BITRATE`, `_BITRATE_MODE`, `_GOP_SIZE`, `_FORCE_KEY_FRAME`, `_HEADER_MODE`, `_H264_PROFILE`,
  `_H264_LEVEL`; frame rate through `VIDIOC_S_PARM` on the OUTPUT queue; `VIDIOC_ENCODER_CMD` (`V4L2_ENC_CMD_STOP`/`START`) for
  the drain. The stream is H.264 Constrained Baseline in Annex B byte-stream format (start codes).
  `ffmpeg -c:v h264_v4l2m2m` drives it unchanged: when in doubt about the call order, that is the reference behaviour.
  The decoder is `/dev/video1`; not needed here.
- **Microphone** ALSA `hw:0,1`, S16_LE, 2 channels, 48 kHz. The mixer path is set by the `tp` daemon at start; the program only
  opens the PCM. One opener at a time.
- The codec needs its firmware: build with `VIDC_FW=webos` (see MEMORY.md "Build").

Read both driver files before writing the V4L2 code: format negotiation, buffer counts and the behaviour at stream-off are the
driver's, not the generic documentation's.

## Architecture

A single process, a single `poll()` loop, no threads. Sources in `rootfs/tp-rtsp/` with a Makefile modelled on
`rootfs/tp/Makefile`.

| File | Role |
|---|---|
| `main.c` | options (`-p` port, `-b` bit rate, `-g` GOP, device overrides), the poll loop, pipeline start/stop on demand |
| `rtsp.c` | TCP listener, per-client state, request parser, OPTIONS / DESCRIBE / SETUP / PLAY / TEARDOWN / GET_PARAMETER (keep-alive), SDP |
| `rtp.c` | RTP header, H.264 packetisation (RFC 6184: single NAL unit and FU-A, packetization-mode 1), PCMA packets, interleaved `$` framing |
| `capture.c` | camera: `VIDIOC_S_FMT`, MMAP buffers, stream on/off, dequeue |
| `encoder.c` | encoder: formats, controls, OUTPUT/CAPTURE queues, Annex B split into NAL units, SPS/PPS cache |
| `audio.c` | ALSA capture, downmix, resample to 8 kHz, A-law |

Rules of behaviour:

- The pipeline (camera, encoder, microphone) opens at the first PLAY and closes when the last client leaves.
- SDP for video: `m=video 0 RTP/AVP 96`, `a=rtpmap:96 H264/90000`, `a=fmtp:96 packetization-mode=1;profile-level-id=...;
  sprop-parameter-sets=<SPS>,<PPS>` (base64), `a=control:` per track. If the SPS/PPS are not known yet at DESCRIBE time, start
  the encoder to get them rather than sending an SDP without them.
- Force a key frame (`V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME`) when a client starts playing, and send SPS/PPS in-band before each
  IDR. A new client receives nothing until the next IDR.
- A slow client must never block the others or the capture: non-blocking sockets, a bounded per-client queue, and when it
  overflows drop that client's video until the next IDR.
- Timestamps: video 90 kHz and audio 8 kHz, both derived from the same `CLOCK_MONOTONIC` origin. Use the capture timestamp of
  the camera buffer, carried through the encoder (the m2m driver copies timestamps from OUTPUT to CAPTURE buffers; verify).
- Camera to encoder: start with a `memcpy` from the camera MMAP buffer into the encoder OUTPUT buffer (460 kB at 15 fps is
  negligible). DMABUF sharing is an optional later optimisation, only if the VFE driver exports buffers (check `VIDIOC_EXPBUF`).
- Validate every length that comes from the network (request line, headers, `Content-Length`, interleaved frame size); cap the
  request size; no `sprintf` into fixed buffers without a bound.

## Build integration (`RTSP=1`)

- `build.sh`: add `RTSP` to the list of variables passed as `--build-arg` (the `for v in ...` loop) and to the usage comment.
  **Keep `DTB_NAME` the last variable of that loop** or fix the loop (`if [ -n ... ]; then ...; fi`): see MEMORY.md, Niubash.
- `Dockerfile`: a stage `tp-rtsp` on the `armv7` base (add `alsa-lib-dev` there or in the stage) and a `tp-rtsp-bin` scratch
  stage, like `tp` / `tp-bin`; a new `./build.sh tp-rtsp` target for quick builds. In the `rootfs-tree` stage, `ARG RTSP=0` and
  a conditional install to `/usr/local/bin/tp-rtsp` (mode 755), for example
  `RUN --mount=from=tp-rtsp-bin,target=/mnt/rtsp if [ "$RTSP" = 1 ]; then install -m 755 /mnt/rtsp/tp-rtsp /usr/local/bin/; fi`.
- Link dynamically against musl and `libasound` (both are in the image). `-O2 -W -Wall`, zero warnings.
- Check that `RTSP=0` gives the same rootfs content as before the change (file list and sizes).

## Steps and acceptance

Each step ends with a test on the tablet and a short result in STATUS.md. One deliberate change per test.

1. **Encoder alone**: a synthetic moving NV12 pattern (generated in the program, `--selftest`) through the encoder into a
   `.h264` file. Accept: the file plays on the PC (`ffplay file.h264`), `ffprobe` reports H.264 640x480 and the expected frame
   count, first frame IDR.
2. **Camera to encoder to file** for 10 s. Accept: about 150 frames, correct picture, no error in `dmesg`, 20 start/stop cycles
   in a row without a failure.
3. **RTSP, video, one client**. Accept: `ffplay -rtsp_transport tcp rtsp://<tablet>:8554/video` shows the picture within 2 s
   of PLAY; VLC plays it too; `ffprobe` on the URL reports the stream.
4. **On demand and several clients**. Accept: two clients at once; the camera stops when both left (check that `/dev/video0` can
   be opened again); 20 connect/disconnect cycles; a client killed without TEARDOWN is cleaned up.
5. **Audio** (`/mic`, then `/cam`). Accept: speech is intelligible in ffplay and VLC, audio and video stay in sync over
   10 minutes (drift below 200 ms).
6. **Endurance**: one hour of `/cam` to a PC; report CPU load, memory (no growth), dropped frames.
7. **Docs**: rewrite `scripts/rtsp/README.md` (or move it to `docs/`) for `tp-rtsp`, keep the go2rtc notes as the alternative;
   update STATUS.md and MEMORY.md.

Suggested hand test of the RTSP layer before any hardware is involved: a fake pipeline (the `--selftest` pattern or a `.h264`
file read in a loop) so that steps 3 and 4 can be debugged from a serial shell without the camera.

## Working rules for this task

- Tablet access, RAM boot, file copy and the build command line are in MEMORY.md. Ask the owner before flashing.
- Before each test session: check the running kernel (`uname -v`), that the camera, the encoder and the capture PCM exist, and
  that `go2rtc`/`ffmpeg` are not holding the devices.
- Do not change the kernel drivers for this task. If a driver bug blocks a step (wrong timestamps, a missing control, a stall
  at stream-off), stop, write what was observed in STATUS.md and tell the owner: driver work is decided with them.
- Commit only when the owner asks; never commit `STATUS.md`, `MEMORY.md` or `docs/history/`.
- Report honestly: a step is done only when its acceptance test passed on the tablet; say what was not tested.

## Known facts from the go2rtc test (2026-11-13)

- Camera + hardware encoder at 1 Mbit/s, GOP 30: 640x480, about 13 fps received on the PC; audio AAC in that test.
- The PC (192.168.10.x) reached the tablet at 192.168.1.108 across subnets; RTSP over TCP worked through that route.
- The encoder's power-up rewrites shared MMSS gating registers; no visible effect on the camera.
- Details: journal sections "RTSP test server, first userspace H.264 path" and "H.264 hardware: LuneOS qcom-vidc driver on
  6.12, WORKS" in [docs/history/STATUS-journal-2026-11-14.md](history/STATUS-journal-2026-11-14.md).
