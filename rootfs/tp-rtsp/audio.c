/*
 * tp-rtsp: internal microphone (ALSA hw:0,1, S16_LE stereo 48 kHz) -> mono 8 kHz G.711 A-law, 20 ms packets.
 * Stereo is averaged, low-passed with a 33-tap windowed-sinc FIR (cut-off 3.4 kHz) and decimated by 6.
 * The RTP time of the packets counts samples from the capture start time, which is measured once on the system clock.
 */
#include <alsa/asoundlib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tp-rtsp.h"

#define IN_RATE		48000
#define DECIM		(IN_RATE / AUD_RATE)
#define IN_FRAMES	(AUD_PKT * DECIM)	/* one packet = 960 input frames = 20 ms */
#define TAPS		33

static struct {
	snd_pcm_t *pcm;
	int fd;
	int32_t coef[TAPS];			/* Q15 */
	int32_t hist[TAPS];			/* mono input, newest at hist[0] */
	int phase;				/* input samples since the last output */
	double gain;
	uint64_t frames;			/* input frames read */
	uint32_t base8k;			/* RTP time of the first output sample */
	uint32_t out_n;				/* output samples produced */
	uint8_t out[AUD_PKT];			/* packet being filled (reads can return any number of frames) */
	int nout;
	int calibrated;
} A = { .fd = -1 };

static uint8_t alaw(int pcm)
{
	static const int seg_end[8] = { 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff };
	int mask, seg;
	uint8_t a;

	pcm >>= 3;				/* 13 bits */
	if (pcm >= 0) {
		mask = 0xd5;
	} else {
		mask = 0x55;
		pcm = -pcm - 1;
	}
	for (seg = 0; seg < 8 && pcm > seg_end[seg]; seg++)
		;
	if (seg >= 8)
		return 0x7f ^ mask;
	a = seg << 4;
	a |= (seg < 2 ? pcm >> 1 : pcm >> seg) & 0x0f;
	return a ^ mask;
}

static void make_filter(void)
{
	double fc = 3400.0 / IN_RATE, sum = 0, h[TAPS];
	int i;

	for (i = 0; i < TAPS; i++) {
		double x = i - (TAPS - 1) / 2.0;
		double s = x == 0 ? 2 * M_PI * fc : sin(2 * M_PI * fc * x) / x;

		h[i] = s * (0.54 - 0.46 * cos(2 * M_PI * i / (TAPS - 1)));
		sum += h[i];
	}
	for (i = 0; i < TAPS; i++)
		A.coef[i] = (int32_t)lrint(h[i] / sum * 32768.0);
}

void audio_close(void)
{
	if (A.pcm)
		snd_pcm_close(A.pcm);
	memset(&A, 0, sizeof(A));
	A.fd = -1;
}

int audio_fd(void)
{
	return A.fd;
}

int audio_open(const char *dev, double gain)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_uframes_t period = IN_FRAMES, buffer = IN_FRAMES * 8;
	unsigned rate = IN_RATE;
	struct pollfd pfd;
	int err;

	memset(&A, 0, sizeof(A));
	A.fd = -1;
	A.gain = gain;
	make_filter();
	err = snd_pcm_open(&A.pcm, dev, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
	if (err < 0) {
		say("audio: open %s: %s", dev, snd_strerror(err));
		A.pcm = NULL;
		return -1;
	}
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_sw_params_alloca(&sw);
	err = snd_pcm_hw_params_any(A.pcm, hw);
	if (!err)
		err = snd_pcm_hw_params_set_access(A.pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (!err)
		err = snd_pcm_hw_params_set_format(A.pcm, hw, SND_PCM_FORMAT_S16_LE);
	if (!err)
		err = snd_pcm_hw_params_set_channels(A.pcm, hw, 2);
	if (!err)
		err = snd_pcm_hw_params_set_rate_near(A.pcm, hw, &rate, NULL);
	if (!err && rate != IN_RATE)
		err = -EINVAL;
	if (!err)
		err = snd_pcm_hw_params_set_period_size_near(A.pcm, hw, &period, NULL);
	if (!err)
		err = snd_pcm_hw_params_set_buffer_size_near(A.pcm, hw, &buffer);
	if (!err && buffer < period * 4)
		dbg("audio: the driver gives only %lu frames of buffer", (unsigned long)buffer);
	if (!err)
		err = snd_pcm_hw_params(A.pcm, hw);
	if (err < 0) {
		say("audio: hardware parameters: %s", snd_strerror(err));
		goto fail;
	}
	snd_pcm_sw_params_current(A.pcm, sw);
	snd_pcm_sw_params_set_tstamp_mode(A.pcm, sw, SND_PCM_TSTAMP_ENABLE);
	snd_pcm_sw_params_set_tstamp_type(A.pcm, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC);
	snd_pcm_sw_params(A.pcm, sw);
	if (snd_pcm_poll_descriptors(A.pcm, &pfd, 1) != 1) {
		say("audio: no poll descriptor");
		goto fail;
	}
	A.fd = pfd.fd;
	err = snd_pcm_prepare(A.pcm);
	if (!err)
		err = snd_pcm_start(A.pcm);
	if (err < 0) {
		say("audio: start: %s", snd_strerror(err));
		goto fail;
	}
	dbg("audio: %s open, period %lu buffer %lu frames", dev, (unsigned long)period, (unsigned long)buffer);
	return 0;
fail:
	audio_close();
	return -1;
}

/* RTP time of the first sample: the trigger time stamp of the stream (system clock) */
static void calibrate(void)
{
	snd_pcm_status_t *st;
	snd_htimestamp_t ht;
	snd_pcm_sframes_t avail;
	uint64_t t, back;

	snd_pcm_status_alloca(&st);
	t = now_ns();
	if (snd_pcm_status(A.pcm, st) == 0) {
		snd_pcm_status_get_trigger_htstamp(st, &ht);
		if (ht.tv_sec || ht.tv_nsec) {
			t = (uint64_t)ht.tv_sec * 1000000000ull + ht.tv_nsec;	/* the capture start: sample 0 */
		} else {
			snd_pcm_status_get_htstamp(st, &ht);
			if (ht.tv_sec || ht.tv_nsec)
				t = (uint64_t)ht.tv_sec * 1000000000ull + ht.tv_nsec;
			avail = snd_pcm_status_get_avail(st);
			back = (uint64_t)(avail > 0 ? avail : 0) * 1000000000ull / IN_RATE;
			if (back < t)
				t -= back;
		}
		dbg("audio: stream start %.3f s before the first read", (now_ns() - t) / 1e9);
	}
	A.base8k = ts_audio(t);
	A.calibrated = 1;
}

int audio_service(audio_cb cb)
{
	int16_t in[IN_FRAMES * 2];

	if (!A.pcm)
		return -1;
	if (!A.calibrated)
		calibrate();
	for (;;) {
		snd_pcm_sframes_t r = snd_pcm_avail_update(A.pcm);
		int i, k;

		/* whole 20 ms chunks only: the DMA position moves continuously, reading what is there would never end */
		if (r >= 0 && r < IN_FRAMES)
			return 0;
		if (r >= 0)
			r = snd_pcm_readi(A.pcm, in, IN_FRAMES);
		if (r == -EAGAIN)
			return 0;
		if (r < 0) {
			say("audio: read: %s", snd_strerror((int)r));
			if (snd_pcm_recover(A.pcm, (int)r, 1) < 0)
				return -1;
			snd_pcm_start(A.pcm);
			/* samples were lost: keep the RTP time on the system clock, drop the half-filled packet and the filter state */
			A.base8k = ts_audio(now_ns()) - A.out_n;
			A.nout = A.phase = 0;
			memset(A.hist, 0, sizeof(A.hist));
			continue;
		}
		if (verbose > 1)
			dbg("audio: read %ld frames, total %llu", (long)r, (unsigned long long)A.frames + r);
		for (i = 0; i < r; i++) {
			int mono = (in[2 * i] + in[2 * i + 1]) / 2;

			memmove(A.hist + 1, A.hist, (TAPS - 1) * sizeof(A.hist[0]));
			A.hist[0] = mono;
			if (++A.phase == DECIM) {
				int64_t acc = 0;
				int64_t v;

				A.phase = 0;
				for (k = 0; k < TAPS; k++)
					acc += (int64_t)A.coef[k] * A.hist[k];
				v = (int64_t)((double)(acc >> 15) * A.gain);
				if (v > 32767)
					v = 32767;
				if (v < -32768)
					v = -32768;
				A.out[A.nout++] = alaw((int)v);
				if (A.nout == AUD_PKT) {
					cb(A.out, A.nout, A.base8k + A.out_n);
					A.out_n += A.nout;
					A.nout = 0;
				}
			}
		}
		A.frames += r;
		if (verbose && A.frames / IN_RATE != (A.frames - r) / IN_RATE) {
			/* how far the sample clock is behind the system clock: newest sample time vs now */
			uint64_t t_newest = t_origin_ns + (uint64_t)(A.base8k + A.out_n) * 125000ull;

			dbg("audio: timeline lag %.1f ms after %llu frames (%u samples out)", ((double)now_ns() - (double)t_newest) / 1e6,
			    (unsigned long long)A.frames, A.out_n);
		}
	}
}
