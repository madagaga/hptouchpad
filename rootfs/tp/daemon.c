/*
 * tp --daemon: one poll() loop over the IPC socket and the input devices, with the power policy every POWER_MS.
 * IPC (TP_SOCK, mode 0600): a request is the argument list as NUL-terminated strings followed by an empty string (at most
 * MAX_ARGS arguments, MAX_REQ bytes); the answer is JSON, then the connection is closed. Native commands only.
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "tp.h"

#define POWER_MS	5000

struct state st;
int in_daemon;

static volatile sig_atomic_t reload;

static void on_hup(int sig)
{
	(void)sig;
	reload = 1;
}

/* splits "a\0b\0\0" into argv; returns argc, -1 when malformed */
int ipc_parse(char *req, size_t len, char **argv)
{
	size_t i = 0;
	int argc = 0;

	while (i < len) {
		size_t n = strnlen(req + i, len - i);

		if (i + n == len)
			return -1;		/* not terminated */
		if (n == 0)
			return i + 1 == len ? argc : -1;
		if (argc == MAX_ARGS)
			return -1;
		argv[argc++] = req + i;
		i += n + 1;
	}
	return -1;
}

static void serve(int ls)
{
	char req[MAX_REQ], *argv[MAX_ARGS + 1];
	struct timeval tv = { .tv_sec = 2 };
	struct buf out = { 0 };
	size_t len = 0;
	int c, argc = -1;

	c = accept4(ls, NULL, NULL, SOCK_CLOEXEC);
	if (c < 0)
		return;
	setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	while (len < sizeof(req)) {
		ssize_t r = read(c, req + len, sizeof(req) - len);

		if (r <= 0)
			break;
		len += r;
		argc = ipc_parse(req, len, argv);
		if (argc >= 0)
			break;
	}
	if (argc < 1)
		buf_fail(&out, "malformed request");
	else if (!cmd_native(argv[0]))
		buf_fail(&out, "not a daemon command: %s", argv[0]);
	else
		cmd_run(argc, argv, &out);
	if (out.len && write(c, out.p, out.len) < 0)
		tp_log("answer not sent: %s", strerror(errno));
	free(out.p);
	close(c);
}

static int listen_socket(void)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (s < 0)
		return -1;
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", TP_SOCK);
	unlink(TP_SOCK);
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) || chmod(TP_SOCK, 0600) || listen(s, 8)) {
		close(s);
		return -1;
	}
	return s;
}

int daemon_main(void)
{
	struct pollfd pfd[1 + 32];
	long long next_power = 0;
	int ls;

	in_daemon = 1;
	/* the helpers are in /usr/local/bin; cogctl talks to Cog on the session bus of /etc/tp.d/S90browser */
	setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/dbus-session", 1);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, on_hup);

	ls = listen_socket();
	if (ls < 0) {
		tp_log("cannot listen on %s: %s", TP_SOCK, strerror(errno));
		return 1;
	}
	power_init();
	cpu_boost_apply();
	sound_init();
	input_init();
	tp_log("daemon started");

	for (;;) {
		long long t = now_ms(), next;
		int n, ni;

		if (reload) {
			reload = 0;
			input_reload();
		}
		if (t >= next_power) {
			power_tick(t);
			input_reload();		/* tpd.conf may have been changed by the shell commands */
			next_power = t + POWER_MS;
		}
		next = input_tick(t);
		if (next_power < next)
			next = next_power;

		pfd[0] = (struct pollfd){ .fd = ls, .events = POLLIN };
		ni = input_fds(pfd + 1, 32);
		n = poll(pfd, 1 + ni, next > t ? (int)(next - t) : 0);
		if (n < 0 && errno != EINTR)
			return 1;
		if (n <= 0)
			continue;
		t = now_ms();
		input_handle(pfd + 1, ni, t);
		if (pfd[0].revents & POLLIN)
			serve(ls);
	}
}
