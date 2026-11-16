/*
 * tp: power policy (port of the shell tp-powerd, STATUS.md "Charging and battery") and the battery, charge, power commands.
 * Every 5 s:
 *   - source: the cable VBUS is on the charger DC input even from a PC port, so the USB device controller tells them apart
 *     ("not attached" = wall adapter, any other state = a PC enumerates us);
 *   - input current of the MAX8903 raised one step at a time while the source stays healthy (PC: usb 500 mA -> 750 -> 900,
 *     wall: 900 -> 1400), lowered and remembered when the input collapses within 30 s of a step up or the charger reports a fault;
 *   - with a cable the battery always charges (CEN, GPIO 41, high = charging), even on a nearly empty battery right after a boot,
 *     except when the charge window (charge_limit, charge_min, charge_max in tpd.conf) holds it at its stop level; without a cable
 *     CEN is left to the max8903 driver;
 *   - low battery: <= 15 % backlight at most 20 % and CPUs at their lowest frequency (lifted again at 20 %); <= 5 % clean power-off without
 *     a cable, the screen off with one (a PC port gives less than the tablet draws with the backlight on).
 * A gauge read that fails (the A6 asleep, STATUS.md) is "unknown": it is logged and never stops the rest of the loop; on battery with
 * the gauge unreadable for GAUGE_LOST_MS the tablet is powered off cleanly instead of being left to die.
 */
#include <dirent.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include "tp.h"

#ifndef UDC
#define UDC		"/sys/class/udc"
#endif
#ifndef PS	/* overridden by the local tests (fake sysfs) */
#define PS		"/sys/class/power_supply"
#endif
#define LOW		15
#define CRIT		5
#define SAVE_BL		20	/* backlight percent in power saving: still readable indoors */
#define GAUGE_LOST_MS	(15 * 60 * 1000LL)

/* charger pins (TLMM): DCM 42 (high = USB mode), IUSB 133 (high = 500 mA), USUS 33, ISET1 34, ISET2 30, CEN 41 */
#define G_DCM	42
#define G_IUSB	133
#define G_USUS	33
#define G_ISET1	34
#define G_ISET2	30
#define G_CEN	41

static const char *const pc_steps[] = { "usb", "750", "900" };
static const char *const wall_steps[] = { "900", "1400" };

static char last_source[8] = "none";
static long long last_up, gone;
static int stable;
static const char *mode = "";

static int charge_mode(const char *m)
{
	if (!strcmp(m, "usb")) {
		tlmm_set(G_DCM, 1); tlmm_set(G_IUSB, 1); tlmm_set(G_USUS, 0); tlmm_set(G_ISET1, 0); tlmm_set(G_ISET2, 0);
	} else if (!strcmp(m, "750")) {
		tlmm_set(G_DCM, 0); tlmm_set(G_ISET1, 0); tlmm_set(G_ISET2, 0);
	} else if (!strcmp(m, "900")) {
		tlmm_set(G_DCM, 0); tlmm_set(G_ISET1, 0); tlmm_set(G_ISET2, 1);
	} else if (!strcmp(m, "2000")) {
		tlmm_set(G_DCM, 0); tlmm_set(G_ISET1, 1); tlmm_set(G_ISET2, 0);
	} else if (!strcmp(m, "1400")) {
		tlmm_set(G_DCM, 0); tlmm_set(G_ISET1, 1); tlmm_set(G_ISET2, 1);
	} else {
		return -1;
	}
	return 0;
}

/*
 * Kernels with the tenderloin charger driver detect the kind of source on D+/D- and set the input current themselves
 * (usb_type, input_current_limit, charge_behaviour in sysfs); the step-up policy below is only for the older kernels.
 */
static int kernel_managed(void)
{
	return !access(PS "/max8903_charger/usb_type", F_OK);
}

static void detect_source(char *src, size_t size)
{
	char udc[32] = "";
	long online = 0;
	struct dirent *e;
	DIR *d;

	read_long(PS "/max8903_charger/online", &online);
	if (!online) {
		snprintf(src, size, "none");
		return;
	}
	if (kernel_managed()) {
		/* the charger driver probed D+/D-: "Unknown [SDP] DCP CDP", the bracketed one is the current kind */
		char t[64] = "";
		const char *b;

		read_line(PS "/max8903_charger/usb_type", t, sizeof(t));
		b = strchr(t, '[');
		snprintf(src, size, "%s", b && (!strncmp(b, "[SDP]", 5) || !strncmp(b, "[CDP]", 5)) ? "pc" : "wall");
		return;
	}
	/* the UDC (gadget controller) state: "not attached" when no host enumerates us */
	d = opendir(UDC);
	while (d && (e = readdir(d)))
		if (e->d_name[0] != '.') {
			char path[300];

			snprintf(path, sizeof(path), UDC "/%s/state", e->d_name);
			read_line(path, udc, sizeof(udc));
			break;
		}
	if (d)
		closedir(d);
	snprintf(src, size, "%s", !strcmp(udc, "not attached") ? "wall" : "pc");
}

static int nsteps(const char *src)
{
	return !strcmp(src, "pc") ? 3 : !strcmp(src, "wall") ? 2 : 0;
}

static const char *step_mode(const char *src, int idx)
{
	return !strcmp(src, "pc") ? pc_steps[idx - 1] : wall_steps[idx - 1];
}

static int *limit_of(const char *src)
{
	return !strcmp(src, "pc") ? &st.lim_pc : &st.lim_wall;
}

static void apply(void)
{
	const char *m = step_mode(st.source, st.step);

	if (kernel_managed())
		return;

	if (!strcmp(m, mode))
		return;
	mode = m;
	charge_mode(m);
	tp_log("%s: input current %s (%d/%d allowed)", st.source, m, st.step, *limit_of(st.source));
}

static void power_off(const char *why)
{
	tp_log("%s: powering off", why);
	sync();
	/* a sudden cut would corrupt the vfat media volume and the rootfs file on it */
	mount(NULL, "/mnt/media", NULL, MS_REMOUNT | MS_RDONLY, NULL);
	sync();
	run((char *[]){ "poweroff", "-f", NULL }, NULL, 0);
}

void power_init(void)
{
	st.lim_pc = 3;
	st.lim_wall = 2;
	st.step = 1;
	st.capacity = -1;
	snprintf(st.source, sizeof(st.source), "none");
}

void power_tick(long long t)
{
	char src[8], health[32] = "";
	long cap;
	int have_cap = !read_long(PS "/battery/capacity", &cap);
	FILE *f;

	read_line(PS "/max8903_charger/health", health, sizeof(health));
	detect_source(src, sizeof(src));

	/* source and input current steps */
	if (!strcmp(src, "none")) {
		if (strcmp(st.source, "none")) {
			/* input lost right after a step up: the source cannot give that much */
			if (t - last_up < 30000 && st.step > 1) {
				*limit_of(st.source) = --st.step;
				tp_log("%s: input lost after a step up, limit lowered to %d", st.source, st.step);
			}
			snprintf(last_source, sizeof(last_source), "%s", st.source);
			gone = t;
			snprintf(st.source, sizeof(st.source), "none");
			mode = "";
		}
	} else if (strcmp(src, st.source)) {
		/* a source (re)appears: keep what was learned when the same kind comes back quickly */
		if (!strcmp(src, last_source) && t - gone < 120000) {
			st.step = *limit_of(src);
		} else {
			*limit_of(src) = nsteps(src);
			st.step = 1;
		}
		snprintf(st.source, sizeof(st.source), "%s", src);
		stable = 0;
		mode = "";
		tp_log("source detected: %s", st.source);
		apply();
	} else if (kernel_managed()) {
		/* nothing to step: the driver chose the limit from the D+/D- detection */
	} else if (strcmp(health, "Good") && st.step > 1) {
		*limit_of(st.source) = --st.step;
		stable = 0;
		tp_log("%s: charger health %s, limit lowered to %d", st.source, health, st.step);
		apply();
	} else if (++stable >= 4 && st.step < *limit_of(st.source)) {
		st.step++;
		stable = 0;
		last_up = t;
		apply();
	}

	/* gauge: last good reading kept; unreadable on battery for too long -> clean power-off */
	if (have_cap) {
		if (st.gauge_lost_ms)
			tp_log("battery gauge answers again: %ld %%", cap);
		st.capacity = (int)cap;
		st.capacity_ms = t;
		st.gauge_lost_ms = 0;
	} else if (!strcmp(src, "none")) {
		if (!st.gauge_lost_ms) {
			st.gauge_lost_ms = t;
			if (st.capacity >= 0)
				tp_log("battery gauge unreadable on battery (last reading %d %%)", st.capacity);
			else
				tp_log("battery gauge unreadable on battery (no reading yet)");
		} else if (t - st.gauge_lost_ms >= GAUGE_LOST_MS) {
			power_off("battery gauge unreadable on battery for 15 minutes");
		}
	}

	/* charge window: hold = charging stopped at the stop level, until the battery falls to the start level */
	if (conf_int("charge_limit", 0) == 1 && have_cap) {
		int cmin = conf_int("charge_min", 20), cmax = conf_int("charge_max", 80);

		if (cap >= cmax && !st.hold) {
			st.hold = 1;
			tp_log("battery %ld %%: charge stopped (window %d-%d %%)", cap, cmin, cmax);
		} else if (cap <= cmin && st.hold) {
			st.hold = 0;
			tp_log("battery %ld %%: charge resumed (window %d-%d %%)", cap, cmin, cmax);
		}
	} else if (conf_int("charge_limit", 0) != 1 && st.hold) {
		st.hold = 0;
		tp_log("charge window off: charge resumed");
	}
	/*
	 * A cable means charging, whatever the battery level or the gauge (also right after a boot on a nearly empty battery):
	 * CEN high unless the window holds the charge. Without a cable CEN is left alone (the max8903 driver drives it).
	 */
	if (kernel_managed()) {
		static int held = -1;

		if (held != st.hold) {
			FILE *b = fopen(PS "/max8903_charger/charge_behaviour", "w");

			if (b) {
				fputs(st.hold ? "inhibit-charge" : "auto", b);
				if (!fclose(b))
					held = st.hold;
			}
		}
	} else if (strcmp(src, "none")) {
		tlmm_set(G_CEN, !st.hold);
	}

	/* low battery: saving at LOW whatever the source; at CRIT power off without a cable, screen off with one so that it charges */
	if (have_cap) {
		if (cap <= LOW && !st.power_saving) {
			st.power_saving = 1;
			tp_log("battery %ld %%: power saving (backlight at most %d %%, CPU at the lowest frequency)", cap, SAVE_BL);
			/* 5 % looked like a dead screen (2026-11-15); a level already below SAVE_BL is left alone */
			if (backlight_get() > SAVE_BL)
				backlight_percent(SAVE_BL);
			run((char *[]){ "sh", "-c", "for f in /sys/devices/system/cpu/cpu*/cpufreq; do "
					"cat $f/cpuinfo_min_freq > $f/scaling_max_freq; done", NULL }, NULL, 0);
		} else if (cap >= LOW + 5 && st.power_saving) {
			st.power_saving = 0;
			tp_log("battery %ld %%: power saving off (CPU limit lifted)", cap);
			run((char *[]){ "sh", "-c", "for f in /sys/devices/system/cpu/cpu*/cpufreq; do "
					"cat $f/cpuinfo_max_freq > $f/scaling_max_freq; done", NULL }, NULL, 0);
		}
		if (cap <= CRIT) {
			if (!strcmp(src, "none"))
				power_off("battery at or below 5 % without a charger");
			if (!st.crit_screen_off) {
				/* the backlight is about half of the load: without it even a PC port (500-900 mA) charges */
				st.crit_screen_off = 1;
				tp_log("battery %ld %%: charging with the screen off", cap);
				screen_power(0);
			}
		} else if (cap > CRIT + 2) {
			st.crit_screen_off = 0;
		}
	}

	/* state for the shell commands that still read it (tp power, tp info) */
	f = fopen("/tmp/tp-powerd.state", "w");
	if (f) {
		fprintf(f, "class=%s mode=%s step=%d limit=%d health=%s hold=%d\n", st.source, *mode ? mode : "none", st.step,
			strcmp(st.source, "none") ? *limit_of(st.source) : 0, health, st.hold);
		fclose(f);
	}
}

void cmd_battery(int argc, char **argv, struct buf *out)
{
	const char *sub = argc > 1 ? argv[1] : "get";
	int a, b;

	if (!strcmp(sub, "get")) {
		char status[32] = "", src[8];
		long cap, cur;
		int have_cap = !read_long(PS "/battery/capacity", &cap);
		int have_cur = !read_long(PS "/battery/current_now", &cur);

		read_line(PS "/battery/status", status, sizeof(status));
		if (in_daemon)
			snprintf(src, sizeof(src), "%s", st.source);
		else
			detect_source(src, sizeof(src));
		buf_printf(out, "{\"ok\":true,\"capacity\":");
		if (have_cap)
			buf_printf(out, "%ld", cap);
		else
			buf_printf(out, "null");
		/* last good reading of the daemon, when the gauge does not answer now */
		if (!have_cap && in_daemon && st.capacity >= 0)
			buf_printf(out, ",\"last_capacity\":%d,\"last_age_s\":%lld", st.capacity, (now_ms() - st.capacity_ms) / 1000);
		buf_printf(out, ",\"status\":");
		buf_json_str(out, *status ? status : "Unknown");
		buf_printf(out, ",\"source\":");
		buf_json_str(out, src);
		buf_printf(out, ",\"current_ma\":");
		if (have_cur)
			buf_printf(out, "%ld", cur / 1000);
		else
			buf_printf(out, "null");
		buf_printf(out, ",\"limit\":%s,\"min\":%d,\"max\":%d,\"held\":%s}\n",
			   conf_int("charge_limit", 0) == 1 ? "true" : "false", conf_int("charge_min", 20),
			   conf_int("charge_max", 80), st.hold ? "true" : "false");
	} else if (!strcmp(sub, "limit")) {
		if (argc < 3 || (strcmp(argv[2], "on") && strcmp(argv[2], "off")))
			FAIL(out, "usage: battery limit on|off");
		if (conf_set("charge_limit", strcmp(argv[2], "on") ? "0" : "1"))
			FAIL(out, "cannot save the setting");
		buf_ok(out);
	} else if (!strcmp(sub, "range")) {
		char v[8];

		if (argc < 4 || int_arg(argv[2], 5, 90, &a) || int_arg(argv[3], 20, 100, &b))
			FAIL(out, "start is 5-90 %%, stop is 20-100 %%");
		if (b < a + 10)
			FAIL(out, "stop must be at least 10 %% above start");
		snprintf(v, sizeof(v), "%d", a);
		conf_set("charge_min", v);
		snprintf(v, sizeof(v), "%d", b);
		if (conf_set("charge_max", v))
			FAIL(out, "cannot save the setting");
		buf_ok(out);
	} else {
		buf_fail(out, "usage: battery get|limit on|off|range <start> <stop>");
	}
}

void cmd_charge(int argc, char **argv, struct buf *out)
{
	const char *m = argc > 1 ? argv[1] : "status";

	if (tlmm_get(G_DCM) < 0)
		FAIL(out, "cannot reach the charger pins (/dev/mem)");
	if (strcmp(m, "status") && charge_mode(m))
		FAIL(out, "usage: charge usb|750|900|1400|2000|status");
	if (tlmm_get(G_DCM) == 1)
		buf_printf(out, "{\"ok\":true,\"mode\":\"usb\",\"input_ma\":%d}\n", tlmm_get(G_IUSB) == 1 ? 500 : 100);
	else
		buf_printf(out, "{\"ok\":true,\"mode\":\"dc\",\"iset1\":%d,\"iset2\":%d}\n", tlmm_get(G_ISET1), tlmm_get(G_ISET2));
}

void cmd_power(int argc, char **argv, struct buf *out)
{
	(void)argc;
	(void)argv;
	if (!in_daemon)
		FAIL(out, "the tp daemon is not running");
	buf_printf(out, "{\"ok\":true,\"source\":\"%s\",\"input\":\"%s\",\"step\":%d,\"limit\":%d,\"charge_held\":%s,"
		   "\"power_saving\":%s,\"gauge\":%s}\n", st.source, *mode ? mode : "none", st.step,
		   strcmp(st.source, "none") ? *limit_of(st.source) : 0, st.hold ? "true" : "false",
		   st.power_saving ? "true" : "false", st.gauge_lost_ms ? "\"unreadable\"" : "\"ok\"");
}

/*
 * CPU boost: the OPPs above 756 MHz (810 .. 1188 MHz, HP voltage table, STATUS.md) are "turbo-mode" OPPs, used by
 * schedutil only while cpufreq boost is on. The choice is saved as cpu_boost in tpd.conf and applied when the daemon
 * starts (off by default).
 */
#define CPUFREQ		"/sys/devices/system/cpu/cpufreq"
#define CPU_POLICY(n)	"/sys/devices/system/cpu/cpu" #n "/cpufreq"

static int cpu_boost_write(int on)
{
	int ret = write_str(CPUFREQ "/boost", on ? "1" : "0");

	/* boost resets the limit to the new maximum: keep the low battery cap */
	if (!ret && in_daemon && st.power_saving)
		run((char *[]){ "sh", "-c", "for f in /sys/devices/system/cpu/cpu*/cpufreq; do "
				"cat $f/cpuinfo_min_freq > $f/scaling_max_freq; done", NULL }, NULL, 0);
	return ret;
}

void cpu_boost_apply(void)
{
	int on = conf_int("cpu_boost", 0) == 1;

	if (cpu_boost_write(on))
		tp_log("cpu boost: cannot write %s", CPUFREQ "/boost");
	else
		tp_log("cpu boost %s", on ? "on (up to the highest OPP)" : "off (up to 756 MHz)");
}

void cmd_cpu(int argc, char **argv, struct buf *out)
{
	const char *sub = argc > 1 ? argv[1] : "get";
	long boost = 0, max0 = 0, cur0 = 0, cur1 = 0;

	if (!strcmp(sub, "boost")) {
		if (argc < 3 || (strcmp(argv[2], "on") && strcmp(argv[2], "off")))
			FAIL(out, "usage: cpu boost on|off");
		if (cpu_boost_write(!strcmp(argv[2], "on")))
			FAIL(out, "cannot change the CPU boost");
		if (conf_set("cpu_boost", strcmp(argv[2], "on") ? "0" : "1"))
			FAIL(out, "cannot save the setting");
		tp_log("cpu boost %s (user)", argv[2]);
	} else if (strcmp(sub, "get")) {
		FAIL(out, "usage: cpu get|boost on|off");
	}
	read_long(CPUFREQ "/boost", &boost);
	read_long(CPU_POLICY(0) "/scaling_max_freq", &max0);
	read_long(CPU_POLICY(0) "/scaling_cur_freq", &cur0);
	read_long(CPU_POLICY(1) "/scaling_cur_freq", &cur1);
	buf_printf(out, "{\"ok\":true,\"boost\":%s,\"max_mhz\":%ld,\"cur_mhz\":[%ld,%ld]}\n", boost ? "true" : "false",
		   max0 / 1000, cur0 / 1000, cur1 / 1000);
}
