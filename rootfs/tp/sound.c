/*
 * tp: sound (card "TouchPad": LPAIF -> WM8958, kernel machine driver tenderloin.c), through alsa-utils (amixer, aplay).
 *   - at daemon start: the playback path of the WM8958 mixer (AIF1.1 -> DAC1 -> output mixers -> LINEOUT1/2 for the speaker
 *     amplifier, DAC -> HPOUT1 for the headphones; DAPM powers what the jack and the "Ext Spk"/"Headphones" pins select),
 *     the capture path of the internal microphone (capture_path), then the saved volume and mute (tpd.conf sound_volume,
 *     sound_mute)
 *   - volume 0-100 % on "DAC1 Volume" (0.75 dB steps): 1-100 % maps to -30..-12 dB, 0 % mutes; mute on "DAC1 Switch"
 *   - test: plays TEST_WAV, detached
 *   - microphone: level 0-100 % on "IN1L Volume" (tpd.conf mic_volume, default = the webOS gain), mute on "DAC2 Switch"
 *     (mic_mute); mic-test records 3 s, measures the level and plays the recording back, detached; mic-level reads the result
 * tinyplay (tinyalsa 2.0) was dropped: it starts the stream with an empty buffer and underruns at once on this driver.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tp.h"

#define CARD_ID		"/proc/asound/card0/id"
#define AMIXER		"/usr/bin/amixer"
#define APLAY		"/usr/bin/aplay"
#define TEST_WAV	"/usr/share/sounds/tp-test.wav"
#define DAC_MIN		56	/* -30 dB (0.75 dB steps, 96 = 0 dB) */
#define DAC_MAX		80	/* -12 dB: 0 dB saturates the speaker (2026-11-08) */
#define ARECORD		"/usr/bin/arecord"
#define MIC_MAX		31	/* "IN1L Volume": 0-31, 1.5 dB steps, -16.5..+30 dB */
#define MIC_DEFAULT	87	/* percent -> 27 (+24 dB), the webOS gain */
#define MIC_SECONDS	3
#define MIC_SECONDS_S	"3"
#define MIC_WAV		"/tmp/tp-mic.wav"
#define MIC_STATE	"/run/tp.mic"

static void write_file(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");

	if (f) {
		fputs(s, f);
		fclose(f);
	}
}

static const char *const path_on[] = {
	"DAC1L Mixer AIF1.1 Switch", "DAC1R Mixer AIF1.1 Switch",
	"Left Output Mixer DAC Switch", "Right Output Mixer DAC Switch",
	"LINEOUT1N Mixer Left Output Switch", "LINEOUT1P Mixer Left Output Switch",
	"LINEOUT2N Mixer Right Output Switch", "LINEOUT2P Mixer Right Output Switch",
	"LINEOUT1N Switch", "LINEOUT1P Switch", "LINEOUT2N Switch", "LINEOUT2P Switch",
};

/*
 * Capture path of the internal microphone (STATUS.md "Microphone works"): IN1LN -> IN1L PGA -> MIXINL -> ADCL -> Left Sidetone ->
 * AIF2DAC2L/R mixers -> AIF2 ADC (PCM hw:0,1). Gains of webOS (0x18, 0x29, 0x603). DAC2 Switch unmutes the DAC2 mixer output
 * (0x612/0x613), muted by default: without it the capture is all zeros.
 */
static const char *const capture_path[][2] = {
	{ "IN1L PGA IN1LN Switch", "1" },
	{ "IN1L Switch", "1" },
	{ "IN1L Volume", "27" },
	{ "MIXINL IN1L Switch", "1" },
	{ "MIXINL IN1L Volume", "0" },
	{ "AIF2DAC2L Mixer Left Sidetone Switch", "1" },
	{ "AIF2DAC2R Mixer Left Sidetone Switch", "1" },
	{ "DAC2 Left Sidetone Volume", "12" },
	{ "DAC2 Switch", "1" },
};

static int card_present(void)
{
	char id[32];

	return !read_line(CARD_ID, id, sizeof(id)) && !strcmp(id, "TouchPad");
}

/* amixer cset name='<ctl>' <val>: one value is applied to every channel of the control */
static int mix(const char *ctl, const char *val)
{
	char id[96];

	snprintf(id, sizeof(id), "name=%s", ctl);
	return run((char *[]){ AMIXER, "-q", "-c", "0", "cset", id, (char *)val, NULL }, NULL, 0);
}

static int apply(int volume, int mute)
{
	char v[8];

	snprintf(v, sizeof(v), "%d", volume ? DAC_MIN + (DAC_MAX - DAC_MIN) * volume / 100 : 0);
	if (mix("DAC1 Volume", v))
		return -1;
	return mix("DAC1 Switch", mute || !volume ? "0" : "1");
}

/* microphone: gain on the input PGA, mute on the switch that gates the AIF2 capture stream */
static int mic_apply(int volume, int mute)
{
	char v[8];

	snprintf(v, sizeof(v), "%d", (volume * MIC_MAX + 50) / 100);
	if (mix("IN1L Volume", v))
		return -1;
	return mix("DAC2 Switch", mute ? "0" : "1");
}

/* peak and RMS of the left channel of a 16-bit stereo WAV file, in dB below full scale (0 = full scale, -96 = silence) */
static int wav_level(const char *path, int *peak_db, int *rms_db)
{
	FILE *f = fopen(path, "rb");
	short s[2 * 256];
	long long sum = 0, n = 0, sq = 0;
	int peak = 0, mean;
	size_t r;

	if (!f)
		return -1;
	if (fseek(f, 44, SEEK_SET)) {
		fclose(f);
		return -1;
	}
	while ((r = fread(s, 4, 256, f)) > 0)
		for (size_t i = 0; i < r; i++) {
			sum += s[2 * i];
			n++;
		}
	if (!n) {
		fclose(f);
		return -1;
	}
	mean = (int)(sum / n);		/* the capture has a small DC offset */
	fseek(f, 44, SEEK_SET);
	while ((r = fread(s, 4, 256, f)) > 0)
		for (size_t i = 0; i < r; i++) {
			int v = s[2 * i] - mean;

			if (v < 0)
				v = -v;
			if (v > peak)
				peak = v;
			sq += (long long)v * v;
		}
	fclose(f);
	*peak_db = peak ? (int)(20 * log10(peak / 32768.0) - 0.5) : -96;
	*rms_db = sq ? (int)(10 * log10((double)sq / n / (32768.0 * 32768.0)) - 0.5) : -96;
	return 0;
}

/*
 * Microphone test, detached from the caller: records MIC_SECONDS, measures the level, then plays the recording back.
 * MIC_STATE holds "recording", then "done <peak dB> <rms dB>" or "failed".
 */
static void mic_test(void)
{
	pid_t pid = fork();

	if (pid == 0) {
		if (fork() == 0) {
			int peak, rms;
			char line[48];

			setsid();
			/* no exec here: drop the daemon's descriptors (the client connection would otherwise stay open) */
			for (int fd = 3; fd < 256; fd++)
				close(fd);
			write_file(MIC_STATE, "recording\n");
			if (run((char *[]){ ARECORD, "-q", "-D", "hw:0,1", "-f", "S16_LE", "-c", "2", "-r", "48000", "-d",
					    MIC_SECONDS_S, MIC_WAV, NULL }, NULL, 0) || wav_level(MIC_WAV, &peak, &rms)) {
				write_file(MIC_STATE, "failed\n");
				_exit(1);
			}
			snprintf(line, sizeof(line), "done %d %d\n", peak, rms);
			write_file(MIC_STATE, line);
			run((char *[]){ APLAY, "-q", "-D", "hw:0,0", MIC_WAV, NULL }, NULL, 0);
			unlink(MIC_WAV);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

void sound_init(void)
{
	int bad = 0;

	if (!card_present()) {
		tp_log("sound: no TouchPad card");
		return;
	}
	for (size_t i = 0; i < sizeof(path_on) / sizeof(path_on[0]); i++)
		bad |= mix(path_on[i], "1");
	bad |= mix("Left Headphone Mux", "DAC");
	bad |= mix("Right Headphone Mux", "DAC");
	for (size_t i = 0; i < sizeof(capture_path) / sizeof(capture_path[0]); i++)
		bad |= mix(capture_path[i][0], capture_path[i][1]);
	bad |= mic_apply(conf_int("mic_volume", MIC_DEFAULT), conf_int("mic_mute", 0) == 1);
	bad |= apply(conf_int("sound_volume", 70), conf_int("sound_mute", 0) == 1);
	tp_log("sound: mixer path set%s", bad ? " (some controls failed)" : "");
}

void cmd_sound(int argc, char **argv, struct buf *out)
{
	const char *sub = argc > 1 ? argv[1] : "get";
	int volume = conf_int("sound_volume", 70), mute = conf_int("sound_mute", 0) == 1, v;
	int mic_volume = conf_int("mic_volume", MIC_DEFAULT), mic_mute = conf_int("mic_mute", 0) == 1;
	char s[8], state[48];

	if (!card_present())
		FAIL(out, "no sound card");
	if (!strcmp(sub, "volume")) {
		if (argc < 3 || int_arg(argv[2], 0, 100, &v))
			FAIL(out, "volume is 0-100");
		snprintf(s, sizeof(s), "%d", v);
		if (conf_set("sound_volume", s))
			FAIL(out, "cannot save the setting");
		volume = v;
		if (apply(volume, mute))
			FAIL(out, "mixer not reachable");
	} else if (!strcmp(sub, "mute")) {
		if (argc < 3 || (strcmp(argv[2], "on") && strcmp(argv[2], "off")))
			FAIL(out, "usage: sound mute on|off");
		mute = !strcmp(argv[2], "on");
		if (conf_set("sound_mute", mute ? "1" : "0"))
			FAIL(out, "cannot save the setting");
		if (apply(volume, mute))
			FAIL(out, "mixer not reachable");
	} else if (!strcmp(sub, "test")) {
		if (access(TEST_WAV, R_OK))
			FAIL(out, "no test sound");
		if (mute || !volume)
			FAIL(out, "sound is muted (or volume 0): nothing to hear");
		spawn((char *[]){ APLAY, "-q", "-D", "hw:0,0", TEST_WAV, NULL });
	} else if (!strcmp(sub, "mic-volume")) {
		if (argc < 3 || int_arg(argv[2], 0, 100, &v))
			FAIL(out, "microphone level is 0-100");
		snprintf(s, sizeof(s), "%d", v);
		if (conf_set("mic_volume", s))
			FAIL(out, "cannot save the setting");
		mic_volume = v;
		if (mic_apply(mic_volume, mic_mute))
			FAIL(out, "mixer not reachable");
	} else if (!strcmp(sub, "mic-mute")) {
		if (argc < 3 || (strcmp(argv[2], "on") && strcmp(argv[2], "off")))
			FAIL(out, "usage: sound mic-mute on|off");
		mic_mute = !strcmp(argv[2], "on");
		if (conf_set("mic_mute", mic_mute ? "1" : "0"))
			FAIL(out, "cannot save the setting");
		if (mic_apply(mic_volume, mic_mute))
			FAIL(out, "mixer not reachable");
	} else if (!strcmp(sub, "mic-test")) {
		if (mic_mute)
			FAIL(out, "the microphone is muted");
		if (!read_line(MIC_STATE, state, sizeof(state)) && !strcmp(state, "recording"))
			FAIL(out, "a microphone test is already running");
		mic_test();
		buf_printf(out, "{\"ok\":true,\"seconds\":%d}\n", MIC_SECONDS);
		return;
	} else if (!strcmp(sub, "mic-level")) {
		int peak = 0, rms = 0;

		if (read_line(MIC_STATE, state, sizeof(state)))
			snprintf(state, sizeof(state), "none");
		if (sscanf(state, "done %d %d", &peak, &rms) == 2)
			snprintf(state, sizeof(state), "done");
		buf_printf(out, "{\"ok\":true,\"state\":");
		buf_json_str(out, state);
		buf_printf(out, ",\"peak_db\":%d,\"rms_db\":%d}\n", peak, rms);
		return;
	} else if (strcmp(sub, "get")) {
		FAIL(out, "usage: sound get|volume <0-100>|mute on|off|test|mic-volume <0-100>|mic-mute on|off|mic-test|mic-level");
	}
	buf_printf(out, "{\"ok\":true,\"volume\":%d,\"mute\":%s,\"mic_volume\":%d,\"mic_mute\":%s}\n", volume, mute ? "true" : "false",
		   mic_volume, mic_mute ? "true" : "false");
}
