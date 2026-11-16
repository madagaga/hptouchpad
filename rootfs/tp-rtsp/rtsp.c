/*
 * tp-rtsp: RTSP server (RFC 2326), RTP over the RTSP TCP connection only (interleaved). No authentication, no UDP.
 * Methods: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER/SET_PARAMETER (keep-alive).
 * URLs: /cam (video + audio), /video, /mic. Track 0 = video (H.264), track 1 = audio (PCMA).
 * A slow client never blocks the others: sockets are non-blocking, each client has a bounded output queue, a frame that
 * does not fit is dropped whole and that client's video waits for the next IDR.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tp-rtsp.h"

#define MAX_CLIENTS	8
#define IN_MAX		4096
#define OUT_CAP		(384 * 1024)
#define IDLE_NS		(90ull * 1000000000)	/* nothing received for that long: the client is gone */
#define STALL_NS	(15ull * 1000000000)	/* queue not draining for that long: the client is gone */
#define SR_NS		(5ull * 1000000000)

enum { ST_INIT, ST_READY, ST_PLAYING };
enum { P_CAM = 1, P_VIDEO, P_MIC };

struct client {
	int fd, st, closing, path;
	uint8_t in[IN_MAX];
	size_t inlen, skip;
	struct outq q;
	char session[9];
	struct rtp_track trk[2];
	int need_idr;
	uint64_t last_rx, last_progress, idr_req, next_sr;
};

static struct client cl[MAX_CLIENTS];
static int lfd = -1;

/* ---- small helpers ---- */

static uint32_t rnd32(void)
{
	uint32_t v = 0;
	int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);

	if (f >= 0) {
		if (read(f, &v, sizeof(v)) != sizeof(v))
			v = (uint32_t)now_ns();
		close(f);
	} else {
		v = (uint32_t)now_ns();
	}
	return v;
}

static void client_close(struct client *c)
{
	if (c->fd < 0)
		return;
	dbg("rtsp: client %s closed", c->session);
	close(c->fd);
	free(c->q.buf);
	memset(c, 0, sizeof(*c));
	c->fd = -1;
}

static void flush(struct client *c)
{
	while (c->fd >= 0 && c->q.head < c->q.tail) {
		ssize_t r = send(c->fd, c->q.buf + c->q.head, c->q.tail - c->q.head, MSG_NOSIGNAL | MSG_DONTWAIT);

		if (r > 0) {
			c->q.head += r;
			c->last_progress = now_ns();
		} else if (r < 0 && errno == EINTR) {
			continue;
		} else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return;
		} else {
			client_close(c);
			return;
		}
	}
	if (c->fd >= 0 && c->closing && c->q.head == c->q.tail)
		client_close(c);
}

static int put(struct client *c, const void *data, size_t n)
{
	uint8_t *p;

	if (c->q.head == c->q.tail)
		c->last_progress = now_ns();
	p = outq_put(&c->q, n);
	if (!p) {
		say("rtsp: client %s: output queue full, closing", c->session);
		client_close(c);
		return -1;
	}
	memcpy(p, data, n);
	return 0;
}

static const char *reason(int code)
{
	switch (code) {
	case 200: return "OK";
	case 400: return "Bad Request";
	case 404: return "Not Found";
	case 454: return "Session Not Found";
	case 455: return "Method Not Valid in This State";
	case 461: return "Unsupported Transport";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	default: return "Error";
	}
}

static void reply(struct client *c, int cseq, int code, const char *extra, const char *body)
{
	char h[1400];
	size_t blen = body ? strlen(body) : 0;
	int n = snprintf(h, sizeof(h), "RTSP/1.0 %d %s\r\nCSeq: %d\r\nServer: tp-rtsp\r\n%s", code, reason(code), cseq,
			 extra ? extra : "");

	if (n > 0 && (size_t)n + 64 < sizeof(h) && blen)
		n += snprintf(h + n, sizeof(h) - n, "Content-Length: %zu\r\n", blen);
	if (n < 0 || (size_t)n + 3 > sizeof(h)) {
		client_close(c);
		return;
	}
	h[n++] = '\r';
	h[n++] = '\n';
	if (put(c, h, n) == 0 && blen)
		put(c, body, blen);
}

static int b64(const uint8_t *s, size_t n, char *out, size_t cap)
{
	static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i, o = 0;

	if (cap < (n + 2) / 3 * 4 + 1)
		return -1;
	for (i = 0; i < n; i += 3) {
		uint32_t v = s[i] << 16 | (i + 1 < n ? s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);

		out[o++] = tab[v >> 18];
		out[o++] = tab[(v >> 12) & 63];
		out[o++] = i + 1 < n ? tab[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < n ? tab[v & 63] : '=';
	}
	out[o] = 0;
	return 0;
}

/* ---- request handling ---- */

/* "rtsp://host:port/cam/trackID=0" -> path id and track (-1 = none); 0 when the URL is not ours */
static int parse_url(const char *uri, int *track)
{
	const char *p = uri;
	int id = 0;
	size_t l;

	*track = -1;
	if (!strncasecmp(p, "rtsp://", 7)) {
		p = strchr(p + 7, '/');
		if (!p)
			return 0;
	}
	if (!strncmp(p, "/cam", 4)) {
		id = P_CAM;
		l = 4;
	} else if (!strncmp(p, "/video", 6)) {
		id = P_VIDEO;
		l = 6;
	} else if (!strncmp(p, "/mic", 4)) {
		id = P_MIC;
		l = 4;
	} else {
		return 0;
	}
	p += l;
	if (*p == '/')
		p++;
	if (!*p)
		return id;
	if (!strncmp(p, "trackID=", 8) && (p[8] == '0' || p[8] == '1') && !p[9]) {
		*track = p[8] - '0';
		return id;
	}
	return 0;
}

static int path_tracks(int path)
{
	return path == P_CAM ? 3 : path == P_VIDEO ? 1 : 2;	/* bit 0 = video, bit 1 = audio */
}

static void local_addr(struct client *c, char *out, size_t n)
{
	struct sockaddr_in a;
	socklen_t l = sizeof(a);

	snprintf(out, n, "0.0.0.0");
	if (!getsockname(c->fd, (struct sockaddr *)&a, &l) && a.sin_family == AF_INET)
		inet_ntop(AF_INET, &a.sin_addr, out, n);
}

static void do_describe(struct client *c, int cseq, const char *uri)
{
	char sdp[1400], sps[160], pps[160], base[300], addr[48], hdr[400];
	int track, path = parse_url(uri, &track), n, tracks;
	size_t ul = strlen(uri);

	if (!path || track >= 0) {
		reply(c, cseq, 404, NULL, NULL);
		return;
	}
	tracks = path_tracks(path);
	if ((tracks & 1) && (!g_ps.sps_len || !g_ps.pps_len) && pipeline_probe_params()) {
		reply(c, cseq, 503, NULL, NULL);
		return;
	}
	local_addr(c, addr, sizeof(addr));
	n = snprintf(sdp, sizeof(sdp), "v=0\r\no=- %u 1 IN IP4 %s\r\ns=tp-rtsp\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\n", (unsigned)(t_origin_ns / 1000000000),
		     addr);
	if (tracks & 1) {
		if (b64(g_ps.sps, g_ps.sps_len, sps, sizeof(sps)) || b64(g_ps.pps, g_ps.pps_len, pps, sizeof(pps))) {
			reply(c, cseq, 503, NULL, NULL);
			return;
		}
		n += snprintf(sdp + n, sizeof(sdp) - n,
			      "m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
			      "a=fmtp:96 packetization-mode=1;profile-level-id=%02X%02X%02X;sprop-parameter-sets=%s,%s\r\n"
			      "a=framerate:15\r\na=control:trackID=0\r\n",
			      g_ps.sps[1], g_ps.sps[2], g_ps.sps[3], sps, pps);
	}
	if (tracks & 2)
		n += snprintf(sdp + n, sizeof(sdp) - n, "m=audio 0 RTP/AVP 8\r\na=rtpmap:8 PCMA/8000\r\na=control:trackID=1\r\n");
	if (n < 0 || (size_t)n >= sizeof(sdp)) {
		reply(c, cseq, 503, NULL, NULL);
		return;
	}
	snprintf(base, sizeof(base), "%s%s", uri, ul && uri[ul - 1] == '/' ? "" : "/");
	snprintf(hdr, sizeof(hdr), "Content-Base: %s\r\nContent-Type: application/sdp\r\n", base);
	reply(c, cseq, 200, hdr, sdp);
}

static void do_setup(struct client *c, int cseq, const char *uri, const char *transport)
{
	int track, path = parse_url(uri, &track), ch = -1;
	char hdr[300];
	const char *p;

	if (!path || track < 0 || !(path_tracks(path) & (1 << track))) {
		reply(c, cseq, 404, NULL, NULL);
		return;
	}
	if (c->st == ST_PLAYING || (c->path && c->path != path)) {
		reply(c, cseq, 455, NULL, NULL);
		return;
	}
	if (!transport || !strstr(transport, "RTP/AVP/TCP") || !(p = strstr(transport, "interleaved="))) {
		reply(c, cseq, 461, NULL, NULL);
		return;
	}
	ch = atoi(p + 12);
	if (ch < 0 || ch > 253) {
		reply(c, cseq, 400, NULL, NULL);
		return;
	}
	for (int i = 0; i < 2; i++)
		if (i != track && c->trk[i].setup && abs(c->trk[i].ch - ch) < 2) {
			reply(c, cseq, 400, NULL, NULL);	/* channels overlap */
			return;
		}
	c->path = path;
	memset(&c->trk[track], 0, sizeof(c->trk[track]));
	c->trk[track].setup = 1;
	c->trk[track].ch = ch;
	c->trk[track].pt = track ? 8 : 96;
	c->trk[track].seq = rnd32();
	c->trk[track].ssrc = rnd32();
	c->trk[track].ts_off = rnd32();
	c->st = ST_READY;
	snprintf(hdr, sizeof(hdr), "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;ssrc=%08X\r\nSession: %s;timeout=60\r\n", ch, ch + 1,
		 c->trk[track].ssrc, c->session);
	reply(c, cseq, 200, hdr, NULL);
}

static void do_play(struct client *c, int cseq, const char *uri)
{
	char hdr[1000], info[700] = "", base[300];
	size_t ul = strlen(uri);
	uint64_t now = now_ns();
	int n = 0;

	if (c->st == ST_INIT || (!c->trk[0].setup && !c->trk[1].setup)) {
		reply(c, cseq, 455, NULL, NULL);
		return;
	}
	if (strlen(uri) > 200) {
		reply(c, cseq, 400, NULL, NULL);
		return;
	}
	snprintf(base, sizeof(base), "%s%s", uri, ul && uri[ul - 1] == '/' ? "" : "/");
	for (int i = 0; i < 2; i++) {
		if (!c->trk[i].setup)
			continue;
		n += snprintf(info + n, sizeof(info) - n, "%surl=%strackID=%d;seq=%u;rtptime=%u", n ? "," : "", base, i, c->trk[i].seq,
			      (i ? ts_audio(now) : ts_video(now)) + c->trk[i].ts_off);
	}
	snprintf(hdr, sizeof(hdr), "Range: npt=0.000-\r\nRTP-Info: %s\r\nSession: %s;timeout=60\r\n", info, c->session);
	c->st = ST_PLAYING;
	c->need_idr = c->trk[0].setup;
	c->next_sr = now + SR_NS;
	for (int i = 0; i < 2; i++)
		c->trk[i].sr_sent = 0;
	reply(c, cseq, 200, hdr, NULL);
	if (c->trk[0].setup)
		pipeline_request_idr();
	dbg("rtsp: client %s playing %s", c->session, uri);
}

static void handle(struct client *c, char *req)
{
	char method[16], uri[256], ver[16], *line, *save;
	const char *transport = NULL, *session = NULL;
	int cseq = -1;

	if (sscanf(req, "%15s %255s %15s", method, uri, ver) != 3 || strncmp(ver, "RTSP/1.", 7)) {
		client_close(c);
		return;
	}
	line = strchr(req, '\n');
	for (line = line ? strtok_r(line, "\r\n", &save) : NULL; line; line = strtok_r(NULL, "\r\n", &save)) {
		if (!strncasecmp(line, "CSeq:", 5))
			cseq = atoi(line + 5);
		else if (!strncasecmp(line, "Transport:", 10))
			transport = line + 10;
		else if (!strncasecmp(line, "Session:", 8))
			session = line + 8;
	}
	if (cseq < 0) {
		reply(c, 0, 400, NULL, NULL);
		return;
	}
	if (session) {
		while (*session == ' ')
			session++;
		if (strncmp(session, c->session, 8) && strcasecmp(method, "SETUP")) {
			reply(c, cseq, 454, NULL, NULL);
			return;
		}
	}
	dbg("rtsp: %s %s", method, uri);
	if (!strcasecmp(method, "OPTIONS")) {
		reply(c, cseq, 200, "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER, SET_PARAMETER\r\n", NULL);
	} else if (!strcasecmp(method, "DESCRIBE")) {
		do_describe(c, cseq, uri);
	} else if (!strcasecmp(method, "SETUP")) {
		do_setup(c, cseq, uri, transport);
	} else if (!strcasecmp(method, "PLAY")) {
		do_play(c, cseq, uri);
	} else if (!strcasecmp(method, "PAUSE")) {
		char hdr[64];

		if (c->st == ST_PLAYING)
			c->st = ST_READY;
		snprintf(hdr, sizeof(hdr), "Session: %s;timeout=60\r\n", c->session);
		reply(c, cseq, 200, hdr, NULL);
	} else if (!strcasecmp(method, "GET_PARAMETER") || !strcasecmp(method, "SET_PARAMETER")) {
		char hdr[64];

		snprintf(hdr, sizeof(hdr), "Session: %s;timeout=60\r\n", c->session);
		reply(c, cseq, 200, hdr, NULL);
	} else if (!strcasecmp(method, "TEARDOWN")) {
		reply(c, cseq, 200, NULL, NULL);
		c->closing = 1;
		c->st = ST_INIT;
	} else {
		reply(c, cseq, 501, NULL, NULL);
	}
}

/* consume what is in the input buffer: interleaved frames from the client (RTCP receiver reports) are skipped */
static void parse_input(struct client *c)
{
	while (c->fd >= 0 && !c->closing && c->inlen) {
		size_t used;

		if (c->skip) {
			size_t k = c->skip < c->inlen ? c->skip : c->inlen;

			c->skip -= k;
			memmove(c->in, c->in + k, c->inlen - k);
			c->inlen -= k;
			continue;
		}
		if (c->in[0] == '$') {
			size_t len;

			if (c->inlen < 4)
				return;
			len = 4 + (c->in[2] << 8 | c->in[3]);
			if (len > c->inlen) {
				c->skip = len - c->inlen;
				c->inlen = 0;
				return;
			}
			used = len;
		} else {
			char *end;
			size_t hl, cl = 0;
			char *cp;

			c->in[c->inlen] = 0;		/* recv() leaves one spare byte */
			end = memmem(c->in, c->inlen, "\r\n\r\n", 4);
			if (!end) {
				if (c->inlen >= IN_MAX - 1) {
					say("rtsp: request too large");
					client_close(c);
				}
				return;
			}
			hl = end + 4 - (char *)c->in;
			*end = 0;
			cp = strcasestr((char *)c->in, "\nContent-Length:");
			if (cp)
				cl = strtoul(cp + 16, NULL, 10);
			if (cl > IN_MAX - 1 - hl) {
				client_close(c);
				return;
			}
			if (hl + cl > c->inlen) {
				*end = '\r';
				return;
			}
			handle(c, (char *)c->in);
			used = hl + cl;
		}
		if (c->fd < 0)
			return;
		memmove(c->in, c->in + used, c->inlen - used);
		c->inlen -= used;
	}
}

static void client_read(struct client *c)
{
	ssize_t r = recv(c->fd, c->in + c->inlen, IN_MAX - 1 - c->inlen, MSG_DONTWAIT);

	if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)) {
		client_close(c);
		return;
	}
	if (r < 0)
		return;
	c->inlen += r;
	c->last_rx = now_ns();
	parse_input(c);
}

/* ---- listener and event loop hooks ---- */

int rtsp_listen(int port)
{
	struct sockaddr_in a;
	int one = 1, i;

	for (i = 0; i < MAX_CLIENTS; i++)
		cl[i].fd = -1;
	lfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (lfd < 0)
		return -1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(lfd, (struct sockaddr *)&a, sizeof(a)) || listen(lfd, 8)) {
		say("rtsp: cannot listen on port %d: %s", port, strerror(errno));
		close(lfd);
		lfd = -1;
		return -1;
	}
	return 0;
}

static void accept_client(void)
{
	int fd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK), i, one = 1, idle = 10, intvl = 5, cnt = 3;
	struct client *c = NULL;

	if (fd < 0)
		return;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (cl[i].fd < 0) {
			c = &cl[i];
			break;
		}
	if (!c) {
		say("rtsp: too many clients");
		close(fd);
		return;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
	memset(c, 0, sizeof(*c));
	c->q.buf = malloc(OUT_CAP);
	if (!c->q.buf) {
		close(fd);
		c->fd = -1;
		return;
	}
	c->q.cap = OUT_CAP;
	c->fd = fd;
	c->last_rx = c->last_progress = now_ns();
	snprintf(c->session, sizeof(c->session), "%08X", rnd32());
	dbg("rtsp: client %s connected", c->session);
}

int rtsp_fill(struct pollfd *p, int max)
{
	int n = 0, i;

	if (lfd >= 0 && n < max) {
		p[n].fd = lfd;
		p[n].events = POLLIN;
		p[n++].revents = 0;
	}
	for (i = 0; i < MAX_CLIENTS && n < max; i++) {
		if (cl[i].fd < 0)
			continue;
		p[n].fd = cl[i].fd;
		p[n].events = POLLIN | (cl[i].q.head < cl[i].q.tail ? POLLOUT : 0);
		p[n++].revents = 0;
	}
	return n;
}

void rtsp_events(const struct pollfd *p, int n)
{
	int i, j;

	for (i = 0; i < n; i++) {
		if (!p[i].revents)
			continue;
		if (p[i].fd == lfd) {
			accept_client();
			continue;
		}
		for (j = 0; j < MAX_CLIENTS; j++) {
			struct client *c = &cl[j];

			if (c->fd != p[i].fd)
				continue;
			if (p[i].revents & POLLIN)
				client_read(c);
			if (c->fd >= 0 && (p[i].revents & POLLOUT))
				flush(c);
			if (c->fd >= 0 && (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) && !(p[i].revents & POLLIN))
				client_close(c);
			break;
		}
	}
	for (j = 0; j < MAX_CLIENTS; j++)
		if (cl[j].fd >= 0)
			flush(&cl[j]);	/* replies written above */
}

static void send_sr(struct client *c, int i, uint64_t now)
{
	struct rtp_track *t = &c->trk[i];

	if (!t->setup)
		return;
	if (rtp_sr(&c->q, t, i ? ts_audio(now) : ts_video(now)))
		return;
	t->sr_sent = 1;
}

void rtsp_tick(void)
{
	uint64_t now = now_ns();
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		struct client *c = &cl[i];

		if (c->fd < 0)
			continue;
		if (now - c->last_rx > IDLE_NS) {
			say("rtsp: client %s idle, closing", c->session);
			client_close(c);
			continue;
		}
		if (c->q.head < c->q.tail && now - c->last_progress > STALL_NS) {
			say("rtsp: client %s stalled, closing", c->session);
			client_close(c);
			continue;
		}
		if (c->st == ST_PLAYING && now >= c->next_sr) {
			c->next_sr = now + SR_NS;
			send_sr(c, 0, now);
			send_sr(c, 1, now);
			flush(c);
		}
	}
}

void rtsp_demand(int *video, int *audio)
{
	int i;

	*video = *audio = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (cl[i].fd >= 0 && cl[i].st == ST_PLAYING) {
			*video |= cl[i].trk[0].setup;
			*audio |= cl[i].trk[1].setup;
		}
}

int rtsp_clients(void)
{
	int i, n = 0;

	for (i = 0; i < MAX_CLIENTS; i++)
		n += cl[i].fd >= 0;
	return n;
}

void rtsp_video(const uint8_t *au, size_t len, uint64_t ts_ns, int idr)
{
	uint32_t ts = ts_video(ts_ns);
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		struct client *c = &cl[i];
		struct rtp_track *t = &c->trk[0];

		if (c->fd < 0 || c->st != ST_PLAYING || !t->setup)
			continue;
		if (c->need_idr && !idr)
			continue;
		if (outq_space(&c->q) < rtp_h264_bound(len)) {
			uint64_t now = now_ns();

			c->need_idr = 1;
			if (now - c->idr_req > 500000000ull) {
				c->idr_req = now;
				pipeline_request_idr();
			}
			dbg("rtsp: client %s too slow, frame dropped", c->session);
			continue;
		}
		if (c->q.head == c->q.tail)
			c->last_progress = now_ns();
		if (!t->sr_sent)
			send_sr(c, 0, now_ns());
		if (rtp_h264(&c->q, t, au, len, ts, idr)) {
			client_close(c);
			continue;
		}
		if (c->need_idr)
			dbg("rtsp: client %s: first IDR sent", c->session);
		c->need_idr = 0;
		flush(c);
	}
}

void rtsp_audio(const uint8_t *pcma, size_t n, uint32_t ts8k)
{
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		struct client *c = &cl[i];
		struct rtp_track *t = &c->trk[1];

		if (c->fd < 0 || c->st != ST_PLAYING || !t->setup)
			continue;
		if (outq_space(&c->q) < n + 16 + 44)
			continue;			/* audio is dropped when the client is behind */
		if (c->q.head == c->q.tail)
			c->last_progress = now_ns();
		if (!t->sr_sent)
			send_sr(c, 1, now_ns());
		rtp_pcma(&c->q, t, pcma, n, ts8k);
		flush(c);
	}
}
