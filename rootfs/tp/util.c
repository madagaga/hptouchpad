/* tp: output buffer, settings file, sysfs, external programs, TLMM registers, log */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tp.h"

void buf_add(struct buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 256;

		while (cap < b->len + n + 1)
			cap *= 2;
		b->p = realloc(b->p, cap);
		if (!b->p)
			abort();
		b->cap = cap;
	}
	memcpy(b->p + b->len, s, n);
	b->len += n;
	b->p[b->len] = 0;
}

void buf_printf(struct buf *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n > 0)
		buf_add(b, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

void buf_json_str(struct buf *b, const char *s)
{
	buf_add(b, "\"", 1);
	for (; s && *s; s++) {
		unsigned char c = *s;

		if (c == '"' || c == '\\') {
			char e[2] = { '\\', c };

			buf_add(b, e, 2);
		} else if (c >= 0x20) {
			buf_add(b, (const char *)&c, 1);
		}	/* control characters are dropped */
	}
	buf_add(b, "\"", 1);
}

void buf_ok(struct buf *b)
{
	buf_printf(b, "{\"ok\":true}\n");
}

void buf_fail(struct buf *b, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	b->len = 0;
	buf_printf(b, "{\"ok\":false,\"error\":");
	buf_json_str(b, msg);
	buf_printf(b, "}\n");
}

int conf_get(const char *key, char *val, size_t size)
{
	FILE *f = fopen(TP_CONF, "r");
	size_t k = strlen(key);
	char line[256];
	int found = -1;

	if (!f)
		return -1;
	/* the last line of a key wins, like the shell scripts (sed ... | tail -n1) */
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, k) || line[k] != '=')
			continue;
		line[strcspn(line, "\r\n")] = 0;
		snprintf(val, size, "%s", line + k + 1);
		found = 0;
	}
	fclose(f);
	return found;
}

int conf_int(const char *key, int def)
{
	char v[32], *end;
	long n;

	if (conf_get(key, v, sizeof(v)))
		return def;
	n = strtol(v, &end, 10);
	return *v && !*end ? (int)n : def;
}

/* rewrites the file without the key, then appends key=val; written to a temporary file and renamed */
int conf_set(const char *key, const char *val)
{
	FILE *in = fopen(TP_CONF, "r"), *out = fopen(TP_CONF ".tmp", "w");
	size_t k = strlen(key);
	char line[256];

	if (!out) {
		if (in)
			fclose(in);
		return -1;
	}
	while (in && fgets(line, sizeof(line), in))
		if (strncmp(line, key, k) || line[k] != '=')
			fputs(line, out);
	if (in)
		fclose(in);
	fprintf(out, "%s=%s\n", key, val);
	if (fclose(out) || rename(TP_CONF ".tmp", TP_CONF))
		return -1;
	sync();
	return 0;
}

int read_line(const char *path, char *s, size_t size)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fgets(s, size, f) != NULL;
	fclose(f);
	if (!ok)
		return -1;
	s[strcspn(s, "\r\n")] = 0;
	return 0;
}

int read_long(const char *path, long *v)
{
	char s[32], *end;

	if (read_line(path, s, sizeof(s)))
		return -1;
	*v = strtol(s, &end, 10);
	return *s && !*end ? 0 : -1;
}

int write_str(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC), ok;

	if (fd < 0)
		return -1;
	ok = write(fd, s, strlen(s)) == (ssize_t)strlen(s);
	close(fd);
	return ok ? 0 : -1;
}

int int_arg(const char *s, int min, int max, int *v)
{
	char *end;
	long n;

	if (!s || !*s || *s == '-' || *s == '+')
		return -1;
	n = strtol(s, &end, 10);
	if (*end || n < min || n > max)
		return -1;
	*v = (int)n;
	return 0;
}

int run(char *const argv[], char *out, size_t size)
{
	int p[2], status;
	size_t got = 0;
	pid_t pid;

	if (pipe(p))
		return -1;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		dup2(null, 0);
		dup2(p[1], 1);
		dup2(null, 2);
		close(p[0]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(p[1]);
	for (;;) {
		char tmp[256];
		ssize_t r = read(p[0], tmp, sizeof(tmp));

		if (r <= 0)
			break;
		if (out && got + 1 < size) {
			size_t n = (size_t)r < size - 1 - got ? (size_t)r : size - 1 - got;

			memcpy(out + got, tmp, n);
			got += n;
		}
	}
	close(p[0]);
	if (out && size)
		out[got] = 0;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void spawn(char *const argv[])
{
	pid_t pid = fork();

	if (pid == 0) {
		/* double fork: the program is re-parented to init and never waited for here */
		if (fork() == 0) {
			int null = open("/dev/null", O_RDWR);

			setsid();
			dup2(null, 0);
			dup2(null, 1);
			dup2(null, 2);
			execvp(argv[0], argv);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

/* TLMM GPIO_IN_OUT registers: 0x801004 + 0x10 * gpio, bit 0 = input level, bit 1 = output value */
#define TLMM_PAGE	0x801000
#define TLMM_IN_OUT(g)	(0x004 + 0x10 * (g))

static volatile unsigned int *tlmm(int gpio)
{
	static volatile unsigned char *base;
	int fd;

	if (gpio < 0 || TLMM_IN_OUT(gpio) >= 0x1000 * 4)
		return NULL;
	if (!base) {
		fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
		if (fd < 0)
			return NULL;
		base = mmap(NULL, 0x1000 * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, TLMM_PAGE);
		close(fd);
		if (base == MAP_FAILED) {
			base = NULL;
			return NULL;
		}
	}
	return (volatile unsigned int *)(base + TLMM_IN_OUT(gpio));
}

int tlmm_set(int gpio, int high)
{
	volatile unsigned int *r = tlmm(gpio);

	if (!r)
		return -1;
	*r = high ? 2 : 0;
	return 0;
}

int tlmm_get(int gpio)
{
	volatile unsigned int *r = tlmm(gpio);

	return r ? (int)(*r >> 1 & 1) : -1;
}

void tp_log(const char *fmt, ...)
{
	char msg[256];
	va_list ap;
	int fd;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	fprintf(stderr, "tp: %s\n", msg);
	fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		dprintf(fd, "tp: %s\n", msg);
		close(fd);
	}
}

long long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

#define BL "/sys/class/backlight/backlight"

int backlight_get(void)
{
	long v, max;

	if (read_long(BL "/brightness", &v) || read_long(BL "/max_brightness", &max) || max <= 0)
		return -1;
	return (int)((v * 100 + max / 2) / max);
}

int backlight_percent(int pct)
{
	char v[16];
	long max;

	if (read_long(BL "/max_brightness", &max) || max <= 0)
		return -1;
	snprintf(v, sizeof(v), "%ld", (pct * max + 50) / 100);
	return write_str(BL "/brightness", v);
}
