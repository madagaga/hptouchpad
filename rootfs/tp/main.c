/*
 * tp: mode dispatch and the client.
 * Client: a native command goes to the daemon (TP_SOCK); without a daemon it runs here (commands that need the daemon say so).
 * Any other command is run by the shell tp (TP_SH) with the same arguments, until it is ported.
 * Output: the JSON answer with -j, otherwise "key: value" lines; exit status 1 when the answer is an error.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "tp.h"

/* sends argv to the daemon; 0 with the answer in out, -1 when there is no daemon */
int ipc_call(int argc, char **argv, struct buf *out)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	struct buf req = { 0 };
	int s;

	s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s < 0)
		return -1;
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", TP_SOCK);
	if (connect(s, (struct sockaddr *)&a, sizeof(a))) {
		close(s);
		return -1;
	}
	for (int i = 0; i < argc; i++)
		buf_add(&req, argv[i], strlen(argv[i]) + 1);
	buf_add(&req, "", 1);
	if (write(s, req.p, req.len) != (ssize_t)req.len) {
		free(req.p);
		close(s);
		return -1;
	}
	free(req.p);
	for (;;) {
		char tmp[512];
		ssize_t r = read(s, tmp, sizeof(tmp));

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		buf_add(out, tmp, r);
	}
	close(s);
	return out->len ? 0 : -1;
}

/*
 * Human output of a flat JSON object: one "key: value" line per member, strings unquoted. Nested values (arrays, objects)
 * are printed as they are. Good enough for the answers of the native commands.
 */
static void print_human(const char *j)
{
	const char *p = j;
	int depth = 0;

	if (*p != '{') {
		fputs(j, stdout);
		return;
	}
	for (p++; *p && !(depth == 0 && *p == '}'); p++) {
		const char *k, *v;

		while (*p == ',' || *p == ' ')
			p++;
		if (*p != '"')
			break;
		k = ++p;
		while (*p && *p != '"')
			p++;
		if (*p != '"' || p[1] != ':')
			break;
		if (!strncmp(k, "ok\"", 3)) {	/* "ok" is the exit status */
			p += 2;
			while (*p && *p != ',' && *p != '}')
				p++;
			p--;
			continue;
		}
		printf("%.*s: ", (int)(p - k), k);
		v = p += 2;
		if (*p == '"') {
			for (p++; *p && *p != '"'; p++)
				if (*p != '\\' || p[1] == 0)
					putchar(*p);
				else
					putchar(*++p);
		} else if (*p == '[' || *p == '{') {
			for (depth = 0; *p; p++) {
				if (*p == '"')
					for (p++; *p && *p != '"'; p++)
						if (*p == '\\' && p[1])
							p++;
				if (*p == '[' || *p == '{')
					depth++;
				if ((*p == ']' || *p == '}') && --depth == 0)
					break;
			}
			printf("%.*s", (int)(p - v + 1), v);
			depth = 0;
		} else {
			while (*p && *p != ',' && *p != '}')
				putchar(*p++);
			p--;
		}
		putchar('\n');
	}
}

static int client(int json, int argc, char **argv)
{
	struct buf out = { 0 };

	if (!cmd_native(argv[0])) {
		char *sh[MAX_ARGS + 3];
		int n = 0;

		sh[n++] = "tp-sh";
		if (json)
			sh[n++] = "-j";
		for (int i = 0; i < argc && n < MAX_ARGS + 2; i++)
			sh[n++] = argv[i];
		sh[n] = NULL;
		execv(TP_SH, sh);
		fprintf(stderr, "tp: cannot run %s: %s\n", TP_SH, strerror(errno));
		return 1;
	}
	if (cmd_local(argv[0]) || ipc_call(argc, argv, &out)) {
		out.len = 0;
		cmd_run(argc, argv, &out);	/* no daemon: run it here */
	}
	if (json)
		fputs(out.p, stdout);
	else if (strstr(out.p, "\"ok\":false"))
		fprintf(stderr, "tp: %s", out.p);
	else
		print_human(out.p);
	return strstr(out.p, "\"ok\":false") ? 1 : 0;
}

int main(int argc, char **argv)
{
	const char *base = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
	int json = 0;

	if (!strcmp(base, "devmem"))
		return devmem_main(argc, argv);
	/* httpd runs the CGI as cgi-bin/tp and always sets GATEWAY_INTERFACE */
	if (getenv("GATEWAY_INTERFACE") || !strcmp(base, "tp-cgi") || (argc > 1 && !strcmp(argv[1], "--cgi")))
		return cgi_main();
	if (argc > 1 && !strcmp(argv[1], "--daemon"))
		return daemon_main();
	argv++;
	argc--;
	if (argc > 0 && !strcmp(argv[0], "-j")) {
		json = 1;
		argv++;
		argc--;
	}
	if (argc > MAX_ARGS) {
		fprintf(stderr, "tp: too many arguments\n");
		return 1;
	}
	if (argc == 0) {
		execl(TP_SH, "tp-sh", "help", (char *)NULL);
		return 1;
	}
	power_init();	/* local runs: no reading yet */
	return client(json, argc, argv);
}
