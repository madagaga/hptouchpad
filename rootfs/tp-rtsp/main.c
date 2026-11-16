/*
 * tp-rtsp: RTSP server for the HP TouchPad. Serves the front camera (hardware H.264) and the internal microphone as
 *   rtsp://<tablet>:8554/cam (video + audio), /video, /mic     RTP over the RTSP TCP connection only.
 * The camera, the encoder and the microphone are opened at the first PLAY and closed when the last client leaves.
 * Test modes without clients: --record FILE (camera -> encoder -> .h264 file), --fake (synthetic picture instead of the
 * camera, also for the server), --selftest FILE (= --fake --record FILE). Design: docs/RTSP-SERVER.md.
 */
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "tp-rtsp.h"

#define SEC		1000000000ull
#define STALL_NS	(3 * SEC)	/* encoder or camera silent for that long: restart the video pipeline */

int verbose;
uint64_t t_origin_ns;

static struct {
	int port, bitrate, gop, fps, seconds, cycles;
	const char *cam, *enc, *mic, *record;
	double gain;
	int fake;
} opt = { 8554, 1000000, 30, 15, 10, 1, NULL, NULL, "hw:0,1", NULL, 1.0, 0 };

static volatile sig_atomic_t quit;
static int vid_on, aud_on, vid_fail, aud_fail, idr_wanted;
static uint64_t retry_vid, retry_aud, enc_last_out, cap_last, cap_first, cap_t0, fake_next;
static unsigned cap_count;
static int slow_tries, slow_checked;
static unsigned long fake_n, dropped;

/* record mode */
static FILE *rec;
static unsigned long rec_frames, rec_bytes, rec_idr;
static int rec_first_idr;
static uint64_t rec_lat_min, rec_lat_max;

/* ---- clock and log ---- */

uint64_t now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * SEC + t.tv_nsec;
}

uint32_t ts_video(uint64_t ns)
{
	return ns > t_origin_ns ? (uint32_t)((ns - t_origin_ns) * 9 / 100000) : 0;
}

uint32_t ts_audio(uint64_t ns)
{
	return ns > t_origin_ns ? (uint32_t)((ns - t_origin_ns) / 125000) : 0;
}

void say(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "tp-rtsp: [%7.3f] ", (now_ns() - t_origin_ns) / 1e9);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

void dbg(const char *fmt, ...)
{
	va_list ap;

	if (!verbose)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "tp-rtsp: [%7.3f] ", (now_ns() - t_origin_ns) / 1e9);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* ---- synthetic picture: diagonal colour ramps and two bars that move ---- */

static void fake_frame(uint8_t *nv12, unsigned long f)
{
	uint8_t *y = nv12, *uv = nv12 + VID_W * VID_H;
	int x, yy, bar = (int)(f * 8 % VID_W), bar2 = (int)(f * 5 % VID_H);

	for (yy = 0; yy < VID_H; yy++)
		for (x = 0; x < VID_W; x++) {
			int v = (x * 255 / VID_W + yy / 4 + (int)f) & 255;

			if (x >= bar && x < bar + 32)
				v = 235;
			if (yy >= bar2 && yy < bar2 + 16)
				v = 16;
			y[yy * VID_W + x] = v;
		}
	for (yy = 0; yy < VID_H / 2; yy++)
		for (x = 0; x < VID_W; x += 2) {
			uv[yy * VID_W + x] = (uint8_t)((x / 4 + f * 2) & 255);
			uv[yy * VID_W + x + 1] = (uint8_t)(255 - ((yy / 2 + f) & 255));
		}
}

/* ---- video pipeline: camera (or synthetic picture) -> encoder -> access units ---- */

static void noop_au(const uint8_t *d, size_t n, uint64_t ts, int idr)
{
	(void)d;
	(void)n;
	(void)ts;
	(void)idr;
}

static void record_au(const uint8_t *d, size_t n, uint64_t ts, int idr)
{
	static uint8_t buf[512 * 1024 + 1024];
	size_t m = h264_normalise(d, n, idr, buf, sizeof(buf));
	uint64_t lat = now_ns() - ts;

	if (!rec_frames)
		rec_first_idr = idr;
	if (!rec_frames || lat < rec_lat_min)
		rec_lat_min = lat;
	if (lat > rec_lat_max)
		rec_lat_max = lat;
	rec_frames++;
	rec_bytes += m;
	rec_idr += idr;
	if (fwrite(buf, 1, m, rec) != m)
		say("record: write error");
}

static void on_au(const uint8_t *d, size_t n, uint64_t ts, int idr)
{
	enc_last_out = now_ns();
	if (rec)
		record_au(d, n, ts, idr);
	else
		rtsp_video(d, n, ts, idr);
}

static int video_start(void)
{
	char path[32];
	uint64_t now = now_ns();

	if (enc_open(opt.enc, opt.bitrate, opt.gop, opt.fps))
		return -1;
	if (enc_start()) {
		enc_close();
		return -1;
	}
	if (!opt.fake) {
		if (opt.cam)
			snprintf(path, sizeof(path), "%s", opt.cam);
		else if (find_video("msm8660-vfe", path, sizeof(path))) {
			say("camera: no msm8660-vfe device");
			enc_close();
			return -1;
		}
		if (cap_open(path)) {
			enc_close();
			return -1;
		}
	}
	vid_on = 1;
	cap_first = now_ns();
	cap_t0 = cap_count = slow_checked = 0;
	vid_fail = 0;
	idr_wanted = 0;
	enc_last_out = cap_last = fake_next = now;
	dbg("video pipeline started");
	return 0;
}

static void video_stop(void)
{
	if (!vid_on)
		return;
	cap_close();
	enc_close();
	vid_on = 0;
	dbg("video pipeline stopped");
}

void pipeline_request_idr(void)
{
	idr_wanted = 1;
}

/* SPS/PPS only exist once the encoder has produced a frame: run it briefly on the synthetic picture */
int pipeline_probe_params(void)
{
	static uint8_t frame[VID_W * VID_H * 3 / 2];
	uint64_t end = now_ns() + 3 * SEC;
	unsigned long f = 0;
	int ret = -1;

	if (g_ps.sps_len && g_ps.pps_len)
		return 0;
	if (vid_on)
		return -1;		/* running: the first frame is on its way */
	if (enc_open(opt.enc, opt.bitrate, opt.gop, opt.fps) || enc_start()) {
		enc_close();
		return -1;
	}
	while (now_ns() < end && !(g_ps.sps_len && g_ps.pps_len)) {
		struct pollfd p = { .fd = enc_fd(), .events = POLLIN };

		fake_frame(frame, f++);
		if (enc_push(frame, now_ns()) < 0)
			break;
		poll(&p, 1, 100);
		if (enc_service(noop_au) < 0)
			break;
	}
	if (g_ps.sps_len && g_ps.pps_len) {
		ret = 0;
		say("SPS/PPS obtained from the encoder (%zu + %zu bytes)", g_ps.sps_len, g_ps.pps_len);
	} else {
		say("encoder gave no SPS/PPS within 3 s");
	}
	enc_close();
	return ret;
}

static void camera_ready(void)
{
	const uint8_t *d;
	uint64_t ts;
	int r;

	while ((r = cap_get(&d, &ts)) > 0) {
		int e = enc_push(d, ts);

		cap_done();
		if (cap_first) {
			dbg("camera: first frame %.3f s after the start", (now_ns() - cap_first) / 1e9);
			cap_first = 0;
			cap_t0 = now_ns();
		}
		cap_count++;
		cap_last = now_ns();
		if (e < 0) {
			vid_fail = 1;
			return;
		}
		if (e > 0)
			dropped++;
	}
	if (r < 0)
		vid_fail = 1;
}

static void fake_tick(uint64_t now)
{
	static uint8_t frame[VID_W * VID_H * 3 / 2];

	if (!vid_on || !opt.fake || now < fake_next)
		return;
	fake_frame(frame, fake_n++);
	if (enc_push(frame, now) < 0)
		vid_fail = 1;
	fake_next += SEC / opt.fps;
	if (fake_next < now)
		fake_next = now;
	cap_last = now;
}

/* the video descriptors, appended to p; returns how many */
static int video_fds(struct pollfd *p)
{
	int n = 0;

	if (!vid_on)
		return 0;
	if (cap_fd() >= 0) {
		p[n].fd = cap_fd();
		p[n].events = POLLIN;
		p[n++].revents = 0;
	}
	p[n].fd = enc_fd();
	p[n].events = POLLIN;
	p[n++].revents = 0;
	return n;
}

static void video_dispatch(const struct pollfd *p, int n)
{
	int i;

	for (i = 0; i < n && vid_on && !vid_fail; i++) {
		if (!p[i].revents)
			continue;
		if (p[i].fd == cap_fd())
			camera_ready();
		else if (enc_service(on_au) < 0)
			vid_fail = 1;
	}
}

/*
 * About one start in five of the camera (msm8660_vfe + MT9M113) delivers near-black frames at about 3 fps for the whole
 * session instead of 15 fps (seen 2026-11-15, see STATUS.md). In server mode such a start is stopped and repeated
 * (at most three times in a row) when fewer than SLOW_MIN frames arrive in the first 1.5 s after the first frame.
 */
#define SLOW_MIN	8
#define SLOW_TRIES	3

static void video_watchdog(uint64_t now)
{
	if (!vid_on || vid_fail)
		return;
	if (!slow_checked && cap_t0 && !rec && now - cap_t0 >= 1500000000ull) {
		slow_checked = 1;
		if (cap_count < SLOW_MIN && slow_tries < SLOW_TRIES) {
			slow_tries++;
			say("camera started slowly (%u frames in 1.5 s), restarting (%d/%d)", cap_count, slow_tries, SLOW_TRIES);
			vid_fail = 2;
			return;
		}
	}
	if (enc_inflight() > 0 && now - enc_last_out > STALL_NS) {
		say("encoder silent for 3 s with %d frames queued", enc_inflight());
		vid_fail = 1;
	} else if (now - cap_last > STALL_NS) {
		say("no camera frame for 3 s");
		vid_fail = 1;
	}
}

/* ---- audio ---- */

static void on_audio(const uint8_t *pcma, size_t n, uint32_t ts8k)
{
	rtsp_audio(pcma, n, ts8k);
}

/* ---- record mode: camera (or synthetic picture) -> encoder -> file, no network ---- */

static int run_record(void)
{
	int cyc, bad = 0;

	for (cyc = 1; cyc <= opt.cycles && !quit; cyc++) {
		uint64_t t0, end;
		unsigned long fr0 = fake_n;

		rec = fopen(opt.record, "wb");
		if (!rec) {
			say("cannot write %s: %s", opt.record, strerror(errno));
			return 1;
		}
		rec_frames = rec_bytes = rec_idr = 0;
		rec_first_idr = 0;
		rec_lat_min = rec_lat_max = 0;
		dropped = 0;
		t0 = now_ns();
		if (video_start()) {
			say("cycle %d: start failed", cyc);
			fclose(rec);
			rec = NULL;
			return 1;
		}
		end = now_ns() + (uint64_t)opt.seconds * SEC;
		while (now_ns() < end && !quit && !vid_fail) {
			struct pollfd p[2];
			int n;

			fake_tick(now_ns());
			n = video_fds(p);
			poll(p, n, 20);
			video_dispatch(p, n);
			video_watchdog(now_ns());
		}
		for (end = now_ns() + SEC; !vid_fail && enc_inflight() > 0 && now_ns() < end;) {	/* drain */
			struct pollfd p[2];
			int n = video_fds(p);

			poll(p, n, 50);
			video_dispatch(p, n);
		}
		video_stop();
		fclose(rec);
		rec = NULL;
		say("cycle %d: %lu frames (%lu IDR, first %s), %lu bytes, %.1f fps, %lu not encoded, latency %llu-%llu ms%s", cyc,
		    rec_frames, rec_idr, rec_first_idr ? "IDR" : "not IDR", rec_bytes,
		    rec_frames * 1e9 / (double)(now_ns() - t0), dropped + (opt.fake ? fake_n - fr0 - rec_frames : 0),
		    (unsigned long long)(rec_lat_min / 1000000), (unsigned long long)(rec_lat_max / 1000000),
		    vid_fail ? " FAILED" : "");
		if (vid_fail || !rec_frames)
			bad++;
	}
	say("%d cycle(s), %d failed", opt.cycles, bad);
	return bad ? 1 : 0;
}

/* ---- server ---- */

static void on_signal(int s)
{
	(void)s;
	quit = 1;
}

static int run_server(void)
{
	if (rtsp_listen(opt.port))
		return 1;
	say("listening on port %d (rtsp://<tablet>:%d/cam, /video, /mic)%s", opt.port, opt.port, opt.fake ? ", synthetic video" : "");
	while (!quit) {
		struct pollfd p[32];
		int wv, wa, nr, nv, na = 0, timeout = 500;
		uint64_t now = now_ns();

		rtsp_demand(&wv, &wa);
		if (wv && !vid_on && now >= retry_vid && video_start())
			retry_vid = now + 2 * SEC;
		if (!wv && vid_on) {
			video_stop();
			slow_tries = 0;
		}
		if (wa && !aud_on && now >= retry_aud) {
			if (audio_open(opt.mic, opt.gain))
				retry_aud = now + 2 * SEC;
			else
				aud_on = 1;
		}
		if (!wa && aud_on) {
			audio_close();
			aud_on = 0;
		}
		if (vid_on && idr_wanted) {
			enc_force_idr();
			idr_wanted = 0;
		}
		fake_tick(now);
		nr = rtsp_fill(p, 16);
		nv = video_fds(p + nr);
		if (aud_on) {
			p[nr + nv].fd = audio_fd();
			p[nr + nv].events = POLLIN;
			p[nr + nv].revents = 0;
			na = 1;
		}
		if (vid_on && opt.fake) {
			int64_t w = (int64_t)(fake_next - now) / 1000000 + 1;

			timeout = w < 1 ? 1 : w < timeout ? (int)w : timeout;
		}
		if (poll(p, nr + nv + na, timeout) < 0 && errno != EINTR)
			break;
		rtsp_events(p, nr);
		video_dispatch(p + nr, nv);
		if (na && p[nr + nv].revents && audio_service(on_audio) < 0)
			aud_fail = 1;
		now = now_ns();
		rtsp_tick();
		video_watchdog(now);
		if (vid_fail) {
			if (vid_fail != 2)
				say("video pipeline failed, restarting");
			video_stop();
			retry_vid = vid_fail == 2 ? now : now + SEC;
			vid_fail = 0;
		}
		if (aud_fail) {
			say("audio failed, restarting");
			audio_close();
			aud_on = 0;
			retry_aud = now + SEC;
			aud_fail = 0;
		}
	}
	video_stop();
	if (aud_on)
		audio_close();
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: tp-rtsp [options]\n"
		"  -p PORT      RTSP port (8554)             -b BPS     video bit rate (1000000)\n"
		"  -g FRAMES    GOP length (30)              -r FPS     encoder frame rate (15)\n"
		"  -c DEVICE    camera node (found by name)  -e DEVICE  encoder node (found by name)\n"
		"  -m PCM       microphone (hw:0,1)          -G GAIN    microphone gain, multiplier (1.0)\n"
		"  -v           verbose\n"
		"  --fake       synthetic moving picture instead of the camera\n"
		"  --record F   no server: camera -> encoder -> F (Annex B), -s SECONDS (10), -n CYCLES (1)\n"
		"  --selftest F the same with --fake (encoder test without the camera)\n");
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "fake", 0, NULL, 1 }, { "record", 1, NULL, 2 }, { "selftest", 1, NULL, 3 }, { "help", 0, NULL, 'h' }, { 0, 0, 0, 0 }
	};
	struct sigaction sa;
	int c;

	while ((c = getopt_long(argc, argv, "p:b:g:r:c:e:m:G:s:n:vh", lo, NULL)) != -1) {
		switch (c) {
		case 'p': opt.port = atoi(optarg); break;
		case 'b': opt.bitrate = atoi(optarg); break;
		case 'g': opt.gop = atoi(optarg); break;
		case 'r': opt.fps = atoi(optarg); break;
		case 'c': opt.cam = optarg; break;
		case 'e': opt.enc = optarg; break;
		case 'm': opt.mic = optarg; break;
		case 'G': opt.gain = atof(optarg); break;
		case 's': opt.seconds = atoi(optarg); break;
		case 'n': opt.cycles = atoi(optarg); break;
		case 'v': verbose++; break;
		case 1: opt.fake = 1; break;
		case 2: opt.record = optarg; break;
		case 3: opt.record = optarg; opt.fake = 1; break;
		default: usage(); return c == 'h' ? 0 : 2;
		}
	}
	if (opt.port < 1 || opt.port > 65535 || opt.bitrate < 32000 || opt.gop < 1 || opt.fps < 1 || opt.fps > 30 || opt.seconds < 1 ||
	    opt.cycles < 1 || opt.gain <= 0) {
		usage();
		return 2;
	}
	t_origin_ns = now_ns();
	/* the microphone buffer holds two 128 ms periods: a loop that is late by that much loses samples */
	if (setpriority(PRIO_PROCESS, 0, -10))
		dbg("cannot raise the priority: %s", strerror(errno));
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	return opt.record ? run_record() : run_server();
}
