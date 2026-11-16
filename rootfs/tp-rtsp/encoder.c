/*
 * tp-rtsp: hardware H.264 encoder (kernel driver vidc_enc.c, "qcom-vidc-enc"): V4L2 mem2mem, multi-planar.
 * OUTPUT queue = raw NV12 (linear), CAPTURE queue = H.264 Annex B. The driver copies the OUTPUT timestamp to the CAPTURE
 * buffer, and prepends SPS/PPS to the first frame only: they are cached here and sent again before every IDR.
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include "tp-rtsp.h"

#define NBUF	4
#define MAXBUF	8

struct ebuf {
	void *map;
	size_t len;
	int busy;			/* OUTPUT: queued in the driver */
};

static struct {
	int fd, nout, ncap, inflight, streaming;
	struct ebuf out[MAXBUF], cap[MAXBUF];
	unsigned stride, uv_off, out_bytes;
} E = { .fd = -1 };

struct h264_ps g_ps;

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do
		r = ioctl(fd, req, arg);
	while (r < 0 && errno == EINTR);
	return r;
}

static int set_ctrl(unsigned id, int val)
{
	struct v4l2_control c = { .id = id, .value = val };

	return xioctl(E.fd, VIDIOC_S_CTRL, &c);
}

/* ---- H.264 Annex B helpers ---- */

/* next NAL unit (without start code, trailing zero bytes removed) at or after *pos; 0 at the end */
int h264_next_nal(const uint8_t *d, size_t n, size_t *pos, const uint8_t **nal, size_t *len)
{
	size_t i = *pos;

	for (;;) {
		size_t s, j, e;

		while (i + 3 <= n && !(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1))
			i++;
		if (i + 3 >= n) {
			*pos = n;
			return 0;
		}
		s = i + 3;
		j = s;
		while (j + 3 <= n && !(d[j] == 0 && d[j + 1] == 0 && d[j + 2] == 1))
			j++;
		e = j + 3 <= n ? j : n;
		*pos = e;
		while (e > s && d[e - 1] == 0)
			e--;
		if (e > s) {
			*nal = d + s;
			*len = e - s;
			return 1;
		}
		i = *pos;
	}
}

/* remember SPS/PPS, tell whether the access unit holds an IDR slice */
static int scan_au(const uint8_t *d, size_t n)
{
	size_t pos = 0, len;
	const uint8_t *nal;
	int idr = 0;

	while (h264_next_nal(d, n, &pos, &nal, &len)) {
		int type = nal[0] & 31;

		if (type == 5) {
			idr = 1;
		} else if (type == 7 && len <= sizeof(g_ps.sps)) {
			memcpy(g_ps.sps, nal, len);
			g_ps.sps_len = len;
		} else if (type == 8 && len <= sizeof(g_ps.pps)) {
			memcpy(g_ps.pps, nal, len);
			g_ps.pps_len = len;
		}
	}
	return idr;
}

/* Annex B with 4-byte start codes, the cached SPS/PPS in front of the first IDR slice, no AUD (used for files) */
size_t h264_normalise(const uint8_t *au, size_t n, int idr, uint8_t *out, size_t cap)
{
	static const uint8_t sc[4] = { 0, 0, 0, 1 };
	size_t pos = 0, len, o = 0;
	const uint8_t *nal;
	int ps_done = 0;

	while (h264_next_nal(au, n, &pos, &nal, &len)) {
		int type = nal[0] & 31;

		if (type == 7 || type == 8 || type == 9)
			continue;
		if (type == 5 && idr && !ps_done) {
			ps_done = 1;
			if (g_ps.sps_len && g_ps.pps_len && o + 8 + g_ps.sps_len + g_ps.pps_len <= cap) {
				memcpy(out + o, sc, 4);
				memcpy(out + o + 4, g_ps.sps, g_ps.sps_len);
				o += 4 + g_ps.sps_len;
				memcpy(out + o, sc, 4);
				memcpy(out + o + 4, g_ps.pps, g_ps.pps_len);
				o += 4 + g_ps.pps_len;
			}
		}
		if (o + 4 + len > cap)
			break;
		memcpy(out + o, sc, 4);
		memcpy(out + o + 4, nal, len);
		o += 4 + len;
	}
	return o;
}

/* ---- the device ---- */

int enc_fd(void)
{
	return E.fd;
}

int enc_inflight(void)
{
	return E.inflight;
}

void enc_close(void)
{
	int i;

	if (E.fd >= 0) {
		enum v4l2_buf_type t;

		if (E.streaming) {
			t = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			xioctl(E.fd, VIDIOC_STREAMOFF, &t);
			t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			xioctl(E.fd, VIDIOC_STREAMOFF, &t);
		}
		for (i = 0; i < E.nout; i++)
			if (E.out[i].map)
				munmap(E.out[i].map, E.out[i].len);
		for (i = 0; i < E.ncap; i++)
			if (E.cap[i].map)
				munmap(E.cap[i].map, E.cap[i].len);
		close(E.fd);
	}
	memset(&E, 0, sizeof(E));
	E.fd = -1;
}

static int map_queue(enum v4l2_buf_type type, struct ebuf *bufs, int *count)
{
	struct v4l2_requestbuffers rb;
	int i;

	memset(&rb, 0, sizeof(rb));
	rb.count = NBUF;
	rb.type = type;
	rb.memory = V4L2_MEMORY_MMAP;
	if (xioctl(E.fd, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 1) {
		say("encoder: REQBUFS type %d: %s", type, strerror(errno));
		return -1;
	}
	*count = rb.count > MAXBUF ? MAXBUF : (int)rb.count;
	for (i = 0; i < *count; i++) {
		struct v4l2_buffer b;
		struct v4l2_plane pl;

		memset(&b, 0, sizeof(b));
		memset(&pl, 0, sizeof(pl));
		b.type = type;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		b.length = 1;
		b.m.planes = &pl;
		if (xioctl(E.fd, VIDIOC_QUERYBUF, &b) < 0) {
			say("encoder: QUERYBUF: %s", strerror(errno));
			return -1;
		}
		bufs[i].len = pl.length;
		bufs[i].map = mmap(NULL, pl.length, PROT_READ | PROT_WRITE, MAP_SHARED, E.fd, pl.m.mem_offset);
		if (bufs[i].map == MAP_FAILED) {
			bufs[i].map = NULL;
			say("encoder: mmap: %s", strerror(errno));
			return -1;
		}
	}
	return 0;
}

int enc_open(const char *path, int bitrate, int gop, int fps)
{
	char found[32];
	struct v4l2_format f;
	struct v4l2_streamparm sp;

	if (!path) {
		if (find_video("qcom-vidc-enc", found, sizeof(found))) {
			say("encoder: no qcom-vidc-enc device (kernel without vidc or firmware missing?)");
			return -1;
		}
		path = found;
	}
	E.fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (E.fd < 0) {
		say("encoder: open %s: %s", path, strerror(errno));
		E.fd = -1;
		return -1;
	}
	/* raw input: linear NV12; the CbCr plane starts 2048-byte aligned (stride * height is a multiple of 2048 at 640x480) */
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f.fmt.pix_mp.width = VID_W;
	f.fmt.pix_mp.height = VID_H;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	f.fmt.pix_mp.num_planes = 1;
	if (xioctl(E.fd, VIDIOC_S_FMT, &f) < 0 || f.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 ||
	    f.fmt.pix_mp.width != VID_W || f.fmt.pix_mp.height != VID_H) {
		say("encoder: OUTPUT format NV12 %dx%d refused (%s)", VID_W, VID_H, strerror(errno));
		goto fail;
	}
	E.stride = f.fmt.pix_mp.plane_fmt[0].bytesperline;
	E.out_bytes = f.fmt.pix_mp.plane_fmt[0].sizeimage;
	E.uv_off = (E.stride * VID_H + 2047) & ~2047u;
	if (E.stride < VID_W || E.uv_off + E.stride * (VID_H / 2) > E.out_bytes) {
		say("encoder: unexpected OUTPUT layout (stride %u, size %u)", E.stride, E.out_bytes);
		goto fail;
	}
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.width = VID_W;
	f.fmt.pix_mp.height = VID_H;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	f.fmt.pix_mp.num_planes = 1;
	if (xioctl(E.fd, VIDIOC_S_FMT, &f) < 0 || f.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_H264) {
		say("encoder: CAPTURE format H264 refused (%s)", strerror(errno));
		goto fail;
	}
	memset(&sp, 0, sizeof(sp));
	sp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	sp.parm.output.timeperframe.numerator = 1;
	sp.parm.output.timeperframe.denominator = fps;
	if (xioctl(E.fd, VIDIOC_S_PARM, &sp) < 0)
		say("encoder: S_PARM: %s (continuing)", strerror(errno));
	if (set_ctrl(V4L2_CID_MPEG_VIDEO_BITRATE, bitrate) < 0)
		say("encoder: bit rate control: %s (continuing)", strerror(errno));
	if (set_ctrl(V4L2_CID_MPEG_VIDEO_GOP_SIZE, gop) < 0)
		say("encoder: GOP control: %s (continuing)", strerror(errno));
	set_ctrl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
	set_ctrl(V4L2_CID_MPEG_VIDEO_HEADER_MODE, V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
	if (map_queue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, E.out, &E.nout) ||
	    map_queue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, E.cap, &E.ncap))
		goto fail;
	return 0;
fail:
	enc_close();
	return -1;
}

static int qbuf(enum v4l2_buf_type type, int index, unsigned bytesused, const struct timeval *tv)
{
	struct v4l2_buffer b;
	struct v4l2_plane pl;

	memset(&b, 0, sizeof(b));
	memset(&pl, 0, sizeof(pl));
	b.type = type;
	b.memory = V4L2_MEMORY_MMAP;
	b.index = index;
	b.length = 1;
	b.m.planes = &pl;
	pl.bytesused = bytesused;
	if (tv)
		b.timestamp = *tv;
	return xioctl(E.fd, VIDIOC_QBUF, &b);
}

int enc_start(void)
{
	enum v4l2_buf_type t;
	int i;

	for (i = 0; i < E.ncap; i++)
		if (qbuf(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, i, 0, NULL) < 0) {
			say("encoder: QBUF capture: %s", strerror(errno));
			return -1;
		}
	t = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (xioctl(E.fd, VIDIOC_STREAMON, &t) < 0) {
		say("encoder: STREAMON output: %s", strerror(errno));
		return -1;
	}
	E.streaming = 1;
	t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (xioctl(E.fd, VIDIOC_STREAMON, &t) < 0) {
		say("encoder: STREAMON capture: %s", strerror(errno));
		return -1;
	}
	return 0;
}

void enc_force_idr(void)
{
	if (E.fd >= 0 && E.streaming)
		set_ctrl(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);
}

/* give back the OUTPUT buffers the encoder has finished with */
static void reclaim(void)
{
	for (;;) {
		struct v4l2_buffer b;
		struct v4l2_plane pl;

		memset(&b, 0, sizeof(b));
		memset(&pl, 0, sizeof(pl));
		b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		b.memory = V4L2_MEMORY_MMAP;
		b.length = 1;
		b.m.planes = &pl;
		if (xioctl(E.fd, VIDIOC_DQBUF, &b) < 0)
			return;
		if (b.index < (unsigned)E.nout && E.out[b.index].busy) {
			E.out[b.index].busy = 0;
			E.inflight--;
		}
	}
}

int enc_push(const uint8_t *nv12, uint64_t ts_ns)
{
	struct timeval tv;
	uint8_t *dst;
	int i, y;

	if (E.fd < 0)
		return -1;
	reclaim();
	for (i = 0; i < E.nout && E.out[i].busy; i++)
		;
	if (i == E.nout)
		return 1;
	dst = E.out[i].map;
	if (E.stride == VID_W && E.uv_off == VID_W * VID_H) {
		memcpy(dst, nv12, VID_W * VID_H * 3 / 2);
	} else {
		for (y = 0; y < VID_H; y++)
			memcpy(dst + (size_t)y * E.stride, nv12 + (size_t)y * VID_W, VID_W);
		for (y = 0; y < VID_H / 2; y++)
			memcpy(dst + E.uv_off + (size_t)y * E.stride, nv12 + (size_t)VID_W * VID_H + (size_t)y * VID_W, VID_W);
	}
	tv.tv_sec = ts_ns / 1000000000ull;
	tv.tv_usec = ts_ns % 1000000000ull / 1000;
	if (qbuf(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, i, E.uv_off + E.stride * (VID_H / 2), &tv) < 0) {
		say("encoder: QBUF output: %s", strerror(errno));
		return -1;
	}
	E.out[i].busy = 1;
	E.inflight++;
	return 0;
}

int enc_service(au_cb cb)
{
	int n = 0;

	if (E.fd < 0)
		return -1;
	reclaim();
	for (;;) {
		struct v4l2_buffer b;
		struct v4l2_plane pl;
		int idr;

		memset(&b, 0, sizeof(b));
		memset(&pl, 0, sizeof(pl));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		b.memory = V4L2_MEMORY_MMAP;
		b.length = 1;
		b.m.planes = &pl;
		if (xioctl(E.fd, VIDIOC_DQBUF, &b) < 0) {
			if (errno == EAGAIN)
				break;
			say("encoder: DQBUF capture: %s", strerror(errno));
			return -1;
		}
		if (b.index >= (unsigned)E.ncap)
			return -1;
		if (!(b.flags & V4L2_BUF_FLAG_ERROR) && pl.bytesused > 0 && pl.bytesused <= E.cap[b.index].len) {
			const uint8_t *d = E.cap[b.index].map;

			idr = scan_au(d, pl.bytesused);
			cb(d, pl.bytesused, (uint64_t)b.timestamp.tv_sec * 1000000000ull + (uint64_t)b.timestamp.tv_usec * 1000ull, idr);
			n++;
		} else {
			say("encoder: bad capture buffer (flags 0x%x, %u bytes)", b.flags, pl.bytesused);
		}
		if (qbuf(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, b.index, 0, NULL) < 0) {
			say("encoder: QBUF capture: %s", strerror(errno));
			return -1;
		}
	}
	return n;
}
