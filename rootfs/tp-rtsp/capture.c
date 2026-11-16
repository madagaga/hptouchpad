/*
 * tp-rtsp: front camera (kernel driver msm8660_vfe.c): single-planar capture, NV12 640x480 only, MMAP buffers.
 * One process at a time can use the camera. Buffer timestamps are CLOCK_MONOTONIC.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include "tp-rtsp.h"

#define NBUF		4
#define FRAME_BYTES	(VID_W * VID_H * 3 / 2)

static struct {
	int fd, n, cur;
	void *map[NBUF];
	size_t len[NBUF];
	int streaming;
} C = { .fd = -1, .cur = -1 };

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do
		r = ioctl(fd, req, arg);
	while (r < 0 && errno == EINTR);
	return r;
}

/* first /dev/videoN whose sysfs name starts with name_prefix */
int find_video(const char *name_prefix, char *path, size_t size)
{
	DIR *d = opendir("/sys/class/video4linux");
	struct dirent *e;
	int found = -1;

	if (!d)
		return -1;
	while (found && (e = readdir(d))) {
		char p[320], name[64] = "";
		FILE *f;

		if (strncmp(e->d_name, "video", 5))
			continue;
		snprintf(p, sizeof(p), "/sys/class/video4linux/%s/name", e->d_name);
		f = fopen(p, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f) && !strncmp(name, name_prefix, strlen(name_prefix))) {
			snprintf(path, size, "/dev/%s", e->d_name);
			found = 0;
		}
		fclose(f);
	}
	closedir(d);
	return found;
}

void cap_close(void)
{
	int i;

	if (C.fd >= 0) {
		enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (C.streaming)
			xioctl(C.fd, VIDIOC_STREAMOFF, &t);
		for (i = 0; i < C.n; i++)
			if (C.map[i])
				munmap(C.map[i], C.len[i]);
		close(C.fd);
	}
	memset(&C, 0, sizeof(C));
	C.fd = -1;
	C.cur = -1;
}

int cap_open(const char *path)
{
	struct v4l2_format f;
	struct v4l2_requestbuffers rb;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int i;

	C.fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (C.fd < 0) {
		say("camera: open %s: %s", path, strerror(errno));
		C.fd = -1;
		return -1;
	}
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f.fmt.pix.width = VID_W;
	f.fmt.pix.height = VID_H;
	f.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	f.fmt.pix.field = V4L2_FIELD_NONE;
	if (xioctl(C.fd, VIDIOC_S_FMT, &f) < 0 || f.fmt.pix.width != VID_W || f.fmt.pix.height != VID_H ||
	    f.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12 || f.fmt.pix.sizeimage != FRAME_BYTES) {
		say("camera: format NV12 %dx%d refused (%s)", VID_W, VID_H, strerror(errno));
		goto fail;
	}
	memset(&rb, 0, sizeof(rb));
	rb.count = NBUF;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rb.memory = V4L2_MEMORY_MMAP;
	if (xioctl(C.fd, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 2) {
		say("camera: REQBUFS: %s", strerror(errno));
		goto fail;
	}
	C.n = rb.count > NBUF ? NBUF : (int)rb.count;
	for (i = 0; i < C.n; i++) {
		struct v4l2_buffer b;

		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		if (xioctl(C.fd, VIDIOC_QUERYBUF, &b) < 0) {
			say("camera: QUERYBUF: %s", strerror(errno));
			goto fail;
		}
		C.len[i] = b.length;
		C.map[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, C.fd, b.m.offset);
		if (C.map[i] == MAP_FAILED) {
			C.map[i] = NULL;
			say("camera: mmap: %s", strerror(errno));
			goto fail;
		}
	}
	for (i = 0; i < C.n; i++) {
		struct v4l2_buffer b;

		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		if (xioctl(C.fd, VIDIOC_QBUF, &b) < 0) {
			say("camera: QBUF: %s", strerror(errno));
			goto fail;
		}
	}
	if (xioctl(C.fd, VIDIOC_STREAMON, &type) < 0) {
		say("camera: STREAMON: %s", strerror(errno));
		goto fail;
	}
	C.streaming = 1;
	return 0;
fail:
	cap_close();
	return -1;
}

int cap_fd(void)
{
	return C.fd;
}

int cap_get(const uint8_t **data, uint64_t *ts_ns)
{
	struct v4l2_buffer b;

	if (C.fd < 0 || C.cur >= 0)
		return -1;
	memset(&b, 0, sizeof(b));
	b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b.memory = V4L2_MEMORY_MMAP;
	if (xioctl(C.fd, VIDIOC_DQBUF, &b) < 0)
		return errno == EAGAIN ? 0 : -1;
	if (b.index >= (unsigned)C.n)
		return -1;
	C.cur = b.index;
	if (b.flags & V4L2_BUF_FLAG_ERROR) {
		cap_done();
		return 0;
	}
	*data = C.map[b.index];
	*ts_ns = (uint64_t)b.timestamp.tv_sec * 1000000000ull + (uint64_t)b.timestamp.tv_usec * 1000ull;
	return 1;
}

void cap_done(void)
{
	struct v4l2_buffer b;

	if (C.cur < 0)
		return;
	memset(&b, 0, sizeof(b));
	b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b.memory = V4L2_MEMORY_MMAP;
	b.index = C.cur;
	C.cur = -1;
	if (xioctl(C.fd, VIDIOC_QBUF, &b) < 0)
		say("camera: QBUF: %s", strerror(errno));
}
