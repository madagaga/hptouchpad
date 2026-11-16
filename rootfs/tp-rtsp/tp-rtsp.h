/*
 * tp-rtsp: RTSP server for the HP TouchPad (front camera + hardware H.264 encoder + internal microphone).
 * Shared declarations. Design: docs/RTSP-SERVER.md. One process, one poll() loop, no threads.
 */
#ifndef TP_RTSP_H
#define TP_RTSP_H

#include <poll.h>
#include <stddef.h>
#include <stdint.h>

#define VID_W		640
#define VID_H		480
#define RTP_MTU		1400		/* largest RTP payload */
#define AUD_RATE	8000		/* PCMA, mono */
#define AUD_PKT		160		/* samples per audio packet (20 ms) */

/* ---- main.c: clock, log, pipeline hooks used by the RTSP layer ---- */
extern int verbose;
extern uint64_t t_origin_ns;
uint64_t now_ns(void);
uint32_t ts_video(uint64_t ns);			/* 90 kHz RTP time of a CLOCK_MONOTONIC time */
uint32_t ts_audio(uint64_t ns);			/* 8 kHz RTP time */
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int pipeline_probe_params(void);		/* get SPS/PPS from the encoder (synthetic picture), 0 on success */
void pipeline_request_idr(void);

/* ---- capture.c: camera (V4L2 single-planar, MMAP) ---- */
int find_video(const char *name_prefix, char *path, size_t size);	/* by /sys/class/video4linux/videoN/name */
int cap_open(const char *path);
void cap_close(void);
int cap_fd(void);
int cap_get(const uint8_t **data, uint64_t *ts_ns);	/* 1 = frame (call cap_done), 0 = none, -1 = error */
void cap_done(void);

/* ---- encoder.c: qcom-vidc H.264 encoder (V4L2 mem2mem, multi-planar) ---- */
struct h264_ps {
	uint8_t sps[96], pps[96];
	size_t sps_len, pps_len;
};
extern struct h264_ps g_ps;
typedef void (*au_cb)(const uint8_t *annexb, size_t len, uint64_t ts_ns, int idr);
int enc_open(const char *path, int bitrate, int gop, int fps);
int enc_start(void);
void enc_close(void);
int enc_fd(void);
int enc_push(const uint8_t *nv12, uint64_t ts_ns);	/* 0 = queued, 1 = no free buffer (frame dropped), -1 = error */
int enc_service(au_cb cb);				/* dequeue finished frames, returns their count or -1 */
int enc_inflight(void);
void enc_force_idr(void);
int h264_next_nal(const uint8_t *d, size_t n, size_t *pos, const uint8_t **nal, size_t *len);
size_t h264_normalise(const uint8_t *au, size_t n, int idr, uint8_t *out, size_t cap);

/* ---- audio.c: ALSA capture -> mono 8 kHz A-law ---- */
typedef void (*audio_cb)(const uint8_t *pcma, size_t n, uint32_t ts8k);
int audio_open(const char *dev, double gain);
void audio_close(void);
int audio_fd(void);
int audio_service(audio_cb cb);			/* 0 = ok, -1 = error */

/* ---- rtp.c ---- */
struct outq {
	uint8_t *buf;
	size_t cap, head, tail;
};
struct rtp_track {
	int setup, sr_sent;
	uint8_t ch, pt;
	uint16_t seq;
	uint32_t ssrc, ts_off, pkts, octets;
};
uint8_t *outq_put(struct outq *q, size_t n);
size_t outq_space(const struct outq *q);
size_t rtp_h264_bound(size_t au_len);
int rtp_h264(struct outq *q, struct rtp_track *t, const uint8_t *au, size_t len, uint32_t ts, int idr);
int rtp_pcma(struct outq *q, struct rtp_track *t, const uint8_t *data, size_t n, uint32_t ts);
int rtp_sr(struct outq *q, struct rtp_track *t, uint32_t rtp_ts);

/* ---- rtsp.c ---- */
int rtsp_listen(int port);
int rtsp_fill(struct pollfd *p, int max);
void rtsp_events(const struct pollfd *p, int n);
void rtsp_tick(void);
void rtsp_demand(int *video, int *audio);	/* tracks that some playing client receives */
void rtsp_video(const uint8_t *au, size_t len, uint64_t ts_ns, int idr);
void rtsp_audio(const uint8_t *pcma, size_t n, uint32_t ts8k);
int rtsp_clients(void);

#endif
