/*
 * tp: input side (port of tpd) and the panel, screen commands.
 *   - Home pressed 3 times or more, each within HOME_GAP_MS of the previous one: the settings panel opens or closes in Cog
 *     (cogctl on the session bus of S90browser); pressed once: the screen goes off, or on again.
 *   - Power (PM8058 power key, KEY_POWER) pressed: the screen goes on if it is off, off otherwise (no panel gesture).
 *   - no input on any device for screen_off seconds (tpd.conf, default 300, 0 = never): backlight 0, saved level restored on wake;
 *     the touch screen is grabbed (EVIOCGRAB) while the screen is off and its events are dropped: only Home or Power wakes the
 *     screen (user choice), so nothing reaches the page while it is off, and the touch controller is powered off meanwhile
 *     (ts_srv control socket).
 * Input devices are rescanned every RESCAN_MS: ts_srv creates its uinput device after start-up or on restart.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "tp.h"

#define MAX_DEV		32
#define HOME_GAP_MS	600
#define RESCAN_MS	5000
#define DEFAULT_LEVEL	60
#define BL		"/sys/class/backlight/backlight/brightness"
#define PANEL_URL	"http://127.0.0.1:8080/"
#define PANEL_FLAG	"/run/tp.panel"		/* exists while the panel is shown */

#define BITS_LONG (8 * sizeof(long))
#define TEST_BIT(b, a) (((a)[(b) / BITS_LONG] >> ((b) % BITS_LONG)) & 1)

struct dev {
	int fd;
	char path[32];
	int home;	/* reports KEY_HOMEPAGE */
	int power;	/* reports KEY_POWER */
	int touch;	/* multitouch screen */
};

static struct dev devs[MAX_DEV];
static int ndev, screen_off_s = 300, home_count;
static long long home_deadline, idle_since, next_scan;

static void grab_touch(int on)
{
	for (int i = 0; i < ndev; i++)
		if (devs[i].touch)
			ioctl(devs[i].fd, EVIOCGRAB, on);
}

/*
 * Touch controller power through the control socket of ts_srv: 'C' closes its UART and cuts the controller supply, 'O' powers it up
 * again (reset, configuration) and reopens the UART. Only Home or Power wakes the screen, so the touch screen is off while the screen is.
 */
static void touch_power(int on)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	char c = on ? 'O' : 'C';

	if (s < 0)
		return;
	snprintf(a.sun_path, sizeof(a.sun_path), "/run/tsdriver");
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) || write(s, &c, 1) != 1)
		tp_log("touch controller %s: ts_srv not reachable", on ? "on" : "off");
	close(s);
}

static void screen_sleep(void)
{
	long v;

	if (st.asleep)
		return;
	if (!read_long(BL, &v) && v > 0)
		st.saved_level = (int)v;
	write_str(BL, "0");
	grab_touch(1);
	touch_power(0);
	st.asleep = 1;
	tp_log("screen off (level %d saved)", st.saved_level);
}

static void screen_wake(void)
{
	char v[16];

	if (!st.asleep)
		return;
	snprintf(v, sizeof(v), "%d", st.saved_level);
	write_str(BL, v);
	st.asleep = 0;
	/* only Home or Power wakes the screen: no finger is down, the touch screen goes back to the page at once */
	touch_power(1);
	grab_touch(0);
	tp_log("screen on (level %d)", st.saved_level);
}

static int known(const char *path)
{
	for (int i = 0; i < ndev; i++)
		if (!strcmp(devs[i].path, path))
			return 1;
	return 0;
}

void input_rescan(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d)) && ndev < MAX_DEV) {
		unsigned long keys[KEY_MAX / BITS_LONG + 1] = { 0 }, abs[ABS_MAX / BITS_LONG + 1] = { 0 };
		struct dev *v = &devs[ndev];
		char name[64] = "";

		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(v->path, sizeof(v->path), "/dev/input/%s", e->d_name);
		if (known(v->path) || (v->fd = open(v->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
			continue;
		ioctl(v->fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		ioctl(v->fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
		ioctl(v->fd, EVIOCGNAME(sizeof(name)), name);
		v->home = TEST_BIT(KEY_HOMEPAGE, keys);
		v->power = TEST_BIT(KEY_POWER, keys);
		v->touch = TEST_BIT(ABS_MT_POSITION_X, abs);
		if (v->touch && st.asleep)
			ioctl(v->fd, EVIOCGRAB, 1);
		tp_log("%s \"%s\"%s%s%s", v->path, name, v->home ? " home" : "", v->power ? " power" : "",
		       v->touch ? " touch" : "");
		ndev++;
	}
	closedir(d);
}

static void drop_device(int i)
{
	tp_log("%s gone", devs[i].path);
	close(devs[i].fd);
	devs[i] = devs[--ndev];
}

void input_init(void)
{
	st.saved_level = DEFAULT_LEVEL;
	screen_off_s = conf_int("screen_off", 300);
	idle_since = now_ms();
}

/* settings changed (command or periodic re-read) */
void input_reload(void)
{
	screen_off_s = conf_int("screen_off", 300);
	if (screen_off_s < 0)
		screen_off_s = 300;
}

int input_fds(struct pollfd *pfd, int max)
{
	int n = 0;

	for (int i = 0; i < ndev && n < max; i++)
		pfd[n++] = (struct pollfd){ .fd = devs[i].fd, .events = POLLIN };
	return n;
}

static void handle(struct dev *v, struct input_event *ev, long long t)
{
	idle_since = t;
	if (v->home && ev->type == EV_KEY && ev->code == KEY_HOMEPAGE && ev->value == 1) {
		if (st.asleep) {
			screen_wake();
			return;
		}
		home_count++;
		home_deadline = t + HOME_GAP_MS;
		return;
	}
	if (v->power && ev->type == EV_KEY && ev->code == KEY_POWER && ev->value == 1) {
		if (st.asleep)
			screen_wake();
		else
			screen_sleep();
		return;
	}
	/* while the screen is off the touch screen is grabbed and its events are dropped here: touching does not wake it */
}

/* pfd[0..n-1] are the input devices, in the order of input_fds() */
void input_handle(struct pollfd *pfd, int n, long long t)
{
	for (int i = n - 1; i >= 0; i--) {
		struct input_event ev[32];
		ssize_t r;

		if (!pfd[i].revents)
			continue;
		while ((r = read(devs[i].fd, ev, sizeof(ev))) > 0)
			for (size_t k = 0; k < r / sizeof(ev[0]); k++)
				handle(&devs[i], &ev[k], t);
		if (r < 0 && errno != EAGAIN)
			drop_device(i);
	}
}

long long input_tick(long long t)
{
	long long next = t + RESCAN_MS;

	if (t >= next_scan) {
		input_rescan();
		next_scan = t + RESCAN_MS;
	}
	if (home_deadline && t >= home_deadline) {
		if (home_count >= 3) {
			tp_log("home x%d: panel", home_count);
			panel_set(access(PANEL_FLAG, F_OK) != 0);
		} else if (home_count == 1) {
			screen_sleep();
		}
		home_count = 0;
		home_deadline = 0;
	}
	if (!st.asleep && screen_off_s && t - idle_since >= screen_off_s * 1000LL)
		screen_sleep();

	if (next_scan < next)
		next = next_scan;
	if (home_deadline && home_deadline < next)
		next = home_deadline;
	if (!st.asleep && screen_off_s && idle_since + screen_off_s * 1000LL < next)
		next = idle_since + screen_off_s * 1000LL;
	return next;
}

/* opens the panel in Cog, or goes back to the start URL (TP_DIR/url, about:blank without it) */
void panel_set(int show)
{
	char url[1024] = "about:blank";
	int fd;

	if (!show && read_line(TP_DIR "/url", url, sizeof(url)))
		snprintf(url, sizeof(url), "about:blank");
	if (run((char *[]){ "cogctl", "open", show ? PANEL_URL : url, NULL }, NULL, 0))
		return;
	if (show) {
		fd = open(PANEL_FLAG, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
		if (fd >= 0)
			close(fd);
	} else {
		unlink(PANEL_FLAG);
	}
}

void cmd_panel(int argc, char **argv, struct buf *out)
{
	const char *a = argc > 1 ? argv[1] : "";
	int shown = access(PANEL_FLAG, F_OK) == 0;

	if (!strcmp(a, "toggle"))
		a = shown ? "close" : "open";
	if (strcmp(a, "open") && strcmp(a, "close"))
		FAIL(out, "usage: panel open|close|toggle");
	panel_set(!strcmp(a, "open"));
	if ((access(PANEL_FLAG, F_OK) == 0) != !strcmp(a, "open"))
		FAIL(out, "cogctl failed");
	buf_ok(out);
}

void cmd_screen(int argc, char **argv, struct buf *out)
{
	const char *a = argc > 1 ? argv[1] : "get";
	int v;

	if (!strcmp(a, "get")) {
		buf_printf(out, "{\"ok\":true,\"seconds\":%d", conf_int("screen_off", 300));
		if (in_daemon)
			buf_printf(out, ",\"on\":%s", st.asleep ? "false" : "true");
		buf_printf(out, "}\n");
	} else if (!strcmp(a, "set")) {
		char s[16];

		if (argc < 3 || int_arg(argv[2], 0, 86400, &v))
			FAIL(out, "delay is 0-86400 s");
		snprintf(s, sizeof(s), "%d", v);
		if (conf_set("screen_off", s))
			FAIL(out, "cannot save the setting");
		if (in_daemon)
			input_reload();
		buf_ok(out);
	} else if (!strcmp(a, "on") || !strcmp(a, "off")) {
		if (!in_daemon)
			FAIL(out, "the tp daemon is not running");
		if (!strcmp(a, "on"))
			screen_wake();
		else
			screen_sleep();
		idle_since = now_ms();
		buf_ok(out);
	} else {
		buf_fail(out, "usage: screen get|set <seconds>|on|off");
	}
}

/* screen off/on from the power policy */
void screen_power(int on)
{
	if (on)
		screen_wake();
	else
		screen_sleep();
	idle_since = now_ms();
}
