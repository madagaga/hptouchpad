/*
 * tp: front camera snapshot for the settings panel (kernel driver msm8660_vfe.c: NV12 640x480, read() I/O).
 *   camera get    is the camera there
 *   camera snap   reads a few frames (the first ones are dropped: exposure settles, stale buffers), converts the last one to
 *                 a 24-bit BMP and writes CAM_FILE; the panel shows it as /cam.bmp (link made by S95panel)
 *   camera rotate one more quarter turn clockwise for the pictures (tpd.conf camera_turns, 0-3): the sensor is not mounted
 *                 upright and the panel itself can be rotated
 * Runs in the calling process (CGI or client), never in the daemon: a capture blocks for a few hundred milliseconds.
 * The camera can be opened by one process at a time: a snapshot fails while another program streams from it.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tp.h"

#define CAM_NAME	"msm8660-vfe"		/* start of /sys/class/video4linux/videoN/name */
#define CAM_FILE	"/tmp/tp-cam.bmp"
#define W		640
#define H		480
#define FRAME		(W * H * 3 / 2)
#define SKIP		3			/* frames dropped before the one that is kept */
#define FRAME_MS	3000			/* longest wait for one frame */

static int cam_path(char *path, size_t size)
{
	DIR *d = opendir("/sys/class/video4linux");
	struct dirent *e;
	int found = -1;

	if (!d)
		return -1;
	while (found && (e = readdir(d))) {
		char p[320], name[64];

		if (strncmp(e->d_name, "video", 5))
			continue;
		snprintf(p, sizeof(p), "/sys/class/video4linux/%s/name", e->d_name);
		if (!read_line(p, name, sizeof(name)) && !strncmp(name, CAM_NAME, strlen(CAM_NAME))) {
			snprintf(path, size, "/dev/%s", e->d_name);
			found = 0;
		}
	}
	closedir(d);
	return found;
}

static int read_frame(int fd, unsigned char *buf)
{
	size_t got = 0;

	while (got < FRAME) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		ssize_t r;

		if (poll(&p, 1, FRAME_MS) <= 0)
			return -1;
		r = read(fd, buf + got, FRAME - got);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			return -1;
		got += r;
	}
	return 0;
}

static unsigned char clamp(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

static void put32(unsigned char *p, unsigned int v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* NV12 (BT.601 limited range) -> bottom-up BGR BMP, turned by `turns` quarter turns clockwise */
static int write_bmp(const unsigned char *nv12, int turns, int *ow, int *oh)
{
	int w = turns & 1 ? H : W, h = turns & 1 ? W : H, row = (w * 3 + 3) & ~3;
	unsigned char hdr[54] = { 'B', 'M' }, *line = malloc(row);
	FILE *f = fopen(CAM_FILE ".tmp", "wb");

	if (!f || !line) {
		if (f)
			fclose(f);
		free(line);
		return -1;
	}
	put32(hdr + 2, 54 + row * h);
	put32(hdr + 10, 54);
	put32(hdr + 14, 40);
	put32(hdr + 18, w);
	put32(hdr + 22, h);
	hdr[26] = 1;
	hdr[28] = 24;
	put32(hdr + 34, row * h);
	fwrite(hdr, 1, sizeof(hdr), f);
	memset(line, 0, row);
	for (int y = h - 1; y >= 0; y--) {
		for (int x = 0; x < w; x++) {
			int sx, sy, c, d, e;
			const unsigned char *uv;

			switch (turns & 3) {	/* source pixel shown at (x, y) */
			case 1: sx = y; sy = H - 1 - x; break;
			case 2: sx = W - 1 - x; sy = H - 1 - y; break;
			case 3: sx = W - 1 - y; sy = x; break;
			default: sx = x; sy = y; break;
			}
			uv = nv12 + W * H + (sy / 2) * W + (sx & ~1);
			c = 298 * (nv12[sy * W + sx] - 16);
			d = uv[0] - 128;
			e = uv[1] - 128;
			line[x * 3] = clamp((c + 516 * d + 128) >> 8);
			line[x * 3 + 1] = clamp((c - 100 * d - 208 * e + 128) >> 8);
			line[x * 3 + 2] = clamp((c + 409 * e + 128) >> 8);
		}
		fwrite(line, 1, row, f);
	}
	free(line);
	if (fclose(f) || rename(CAM_FILE ".tmp", CAM_FILE))
		return -1;
	*ow = w;
	*oh = h;
	return 0;
}

void cmd_camera(int argc, char **argv, struct buf *out)
{
	const char *sub = argc > 1 ? argv[1] : "get";
	char path[32];
	int present = !cam_path(path, sizeof(path)), turns = conf_int("camera_turns", 0) & 3;

	if (!strcmp(sub, "get")) {
		buf_printf(out, "{\"ok\":true,\"present\":%s,\"width\":%d,\"height\":%d,\"turns\":%d}\n", present ? "true" : "false", W, H,
			   turns);
	} else if (!strcmp(sub, "snap")) {
		long long t0 = now_ms();
		unsigned char *buf;
		int fd, w, h, bad = 0;

		if (!present)
			FAIL(out, "no camera");
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			FAIL(out, errno == EBUSY ? "the camera is used by another program" : "cannot open the camera");
		buf = malloc(FRAME);
		for (int i = 0; buf && !bad && i <= SKIP; i++)
			bad = read_frame(fd, buf);
		close(fd);
		if (!buf || bad) {
			free(buf);
			FAIL(out, "no picture from the camera");
		}
		bad = write_bmp(buf, turns, &w, &h);
		free(buf);
		if (bad)
			FAIL(out, "cannot write the picture");
		buf_printf(out, "{\"ok\":true,\"width\":%d,\"height\":%d,\"ms\":%lld}\n", w, h, now_ms() - t0);
	} else if (!strcmp(sub, "rotate")) {
		char s[4];

		turns = (turns + 1) & 3;
		snprintf(s, sizeof(s), "%d", turns);
		if (conf_set("camera_turns", s))
			FAIL(out, "cannot save the setting");
		buf_printf(out, "{\"ok\":true,\"turns\":%d}\n", turns);
	} else {
		FAIL(out, "usage: camera get|snap|rotate");
	}
}
