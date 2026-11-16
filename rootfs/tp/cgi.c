/*
 * tp --cgi (linked as /usr/share/tpd/www/cgi-bin/tp by S95panel): the only CGI of the settings panel.
 * The page POSTs a JSON array of strings, the argument list of a tp command (["backlight","set","40"]). Checks: POST only, Host
 * 127.0.0.1:8080 (no DNS rebinding), header X-TP: 1 (a foreign page cannot send it without a CORS preflight, which httpd never
 * accepts), body <= MAX_REQ, at most MAX_ARGS plain strings without control characters, first word in the list below. Then a native
 * command goes to the daemon (or runs here without it) and any other one is exec'ed as `tp-sh -j <args...>`: request data never
 * reaches a shell parser.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tp.h"

static const char *const allowed[] = {
	"info", "battery", "cpu", "backlight", "screen", "rotation", "url", "browser", "panel", "time", "tz", "ntp", "wifi", "bt", "sound",
	"camera", "reboot", "poweroff", "recovery", NULL,
};

static int fail(const char *status, const char *msg)
{
	printf("Status: %s\r\nContent-Type: application/json\r\n\r\n{\"ok\":false,\"error\":\"%s\"}\n", status, msg);
	return 0;
}

static int hex(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Parses ["a","b",...] in place: the strings are unescaped into the body buffer itself. Only ASCII \u escapes are accepted. */
static int parse(char *p, char **argv)
{
	int n = 0;

	while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
		p++;
	if (*p++ != '[')
		return -1;
	for (;;) {
		char *out;

		while (*p == ' ')
			p++;
		if (*p == ']' && n == 0)
			return 0;
		if (*p++ != '"' || n == MAX_ARGS)
			return -1;
		argv[n++] = out = p;
		while (*p != '"') {
			int c = (unsigned char)*p++;

			if (c < 0x20)
				return -1;
			if (c == '\\') {
				switch (c = *p++) {
				case '"': case '\\': case '/': break;
				case 'u': {
					int a = hex(p[0]), b = hex(p[1]), d = hex(p[2]), e = hex(p[3]);

					if (a < 0 || b < 0 || d < 0 || e < 0 || (c = a << 12 | b << 8 | d << 4 | e) < 0x20 || c > 0x7e)
						return -1;
					p += 4;
					break;
				}
				default: return -1;
				}
			}
			*out++ = c;
		}
		*out = 0;
		p++;
		while (*p == ' ')
			p++;
		if (*p == ']')
			return n;
		if (*p++ != ',')
			return -1;
	}
}

int cgi_main(void)
{
	const char *method = getenv("REQUEST_METHOD"), *host = getenv("HTTP_HOST"), *xtp = getenv("HTTP_X_TP");
	const char *len = getenv("CONTENT_LENGTH");
	char body[MAX_REQ + 1], *args[MAX_ARGS], *argv[MAX_ARGS + 3];
	int size = len ? atoi(len) : 0, got = 0, n, r;
	struct buf out = { 0 };

	if (!method || strcmp(method, "POST"))
		return fail("405 Method Not Allowed", "POST only");
	if (!host || strcmp(host, "127.0.0.1:8080") || !xtp || strcmp(xtp, "1"))
		return fail("403 Forbidden", "forbidden");
	if (size <= 0 || size > MAX_REQ)
		return fail("400 Bad Request", "bad length");
	while (got < size && (r = read(0, body + got, size - got)) > 0)
		got += r;
	body[got] = 0;
	if (got != size || (n = parse(body, args)) < 1)
		return fail("400 Bad Request", "expected a JSON array of strings");
	for (r = 0; allowed[r] && strcmp(allowed[r], args[0]); r++)
		;
	if (!allowed[r])
		return fail("403 Forbidden", "command not allowed");

	printf("Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n");
	fflush(stdout);
	/* tp-sh and tp-panel call tp themselves: without this they would start in CGI mode again */
	unsetenv("GATEWAY_INTERFACE");
	if (cmd_native(args[0])) {
		if (cmd_local(args[0]) || ipc_call(n, args, &out)) {
			out.len = 0;
			power_init();
			cmd_run(n, args, &out);
		}
		fputs(out.p, stdout);
		return 0;
	}
	argv[0] = "tp-sh";
	argv[1] = "-j";
	memcpy(argv + 2, args, n * sizeof(char *));
	argv[n + 2] = NULL;
	execv(TP_SH, argv);
	printf("{\"ok\":false,\"error\":\"cannot run tp-sh\"}\n");
	return 0;
}
