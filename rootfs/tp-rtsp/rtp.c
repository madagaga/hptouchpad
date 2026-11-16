/*
 * tp-rtsp: RTP packetisation, interleaved in the RTSP TCP stream (RFC 2326 10.12: '$', channel, 16-bit length, packet).
 * H.264 per RFC 6184 (single NAL unit and FU-A, packetization-mode 1), PCMA per RFC 3551, RTCP sender reports (RFC 3550).
 */
#include <string.h>
#include <time.h>

#include "tp-rtsp.h"

#define MAX_NALS	64

/* ---- output queue of one client ---- */

size_t outq_space(const struct outq *q)
{
	return q->cap - (q->tail - q->head);
}

/* reserve n bytes at the tail; NULL when they do not fit */
uint8_t *outq_put(struct outq *q, size_t n)
{
	uint8_t *p;

	if (q->head == q->tail)
		q->head = q->tail = 0;
	if (q->cap - q->tail < n) {
		if (q->head == 0 || outq_space(q) < n)
			return NULL;
		memmove(q->buf, q->buf + q->head, q->tail - q->head);
		q->tail -= q->head;
		q->head = 0;
	}
	p = q->buf + q->tail;
	q->tail += n;
	return p;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

/* '$' header + RTP header for a payload of n bytes; returns the payload position, NULL when the queue is full */
static uint8_t *pkt_begin(struct outq *q, struct rtp_track *t, size_t n, int marker, uint32_t ts)
{
	uint8_t *p = outq_put(q, 16 + n);

	if (!p)
		return NULL;
	p[0] = '$';
	p[1] = t->ch;
	p[2] = (12 + n) >> 8;
	p[3] = (12 + n) & 0xff;
	p[4] = 0x80;
	p[5] = (marker ? 0x80 : 0) | t->pt;
	p[6] = t->seq >> 8;
	p[7] = t->seq & 0xff;
	t->seq++;
	put32(p + 8, ts + t->ts_off);
	put32(p + 12, t->ssrc);
	t->pkts++;
	t->octets += n;
	return p + 16;
}

/* ---- H.264 ---- */

struct nalref {
	const uint8_t *p;
	size_t n;
};

static int send_nal(struct outq *q, struct rtp_track *t, const uint8_t *nal, size_t n, uint32_t ts, int last)
{
	uint8_t *p;

	if (n <= RTP_MTU) {
		p = pkt_begin(q, t, n, last, ts);
		if (!p)
			return -1;
		memcpy(p, nal, n);
		return 0;
	}
	/* FU-A: the NAL header byte is split into the FU indicator and the FU header */
	{
		uint8_t ind = (nal[0] & 0xe0) | 28, type = nal[0] & 0x1f;
		const uint8_t *d = nal + 1;
		size_t left = n - 1, chunk = RTP_MTU - 2;
		int first = 1;

		while (left) {
			size_t c = left < chunk ? left : chunk;
			int end = c == left;

			p = pkt_begin(q, t, c + 2, end && last, ts);
			if (!p)
				return -1;
			p[0] = ind;
			p[1] = type | (first ? 0x80 : 0) | (end ? 0x40 : 0);
			memcpy(p + 2, d, c);
			d += c;
			left -= c;
			first = 0;
		}
	}
	return 0;
}

/* upper bound of the bytes rtp_h264 writes for an access unit, so that a frame is queued whole or not at all */
size_t rtp_h264_bound(size_t au_len)
{
	size_t total = au_len + g_ps.sps_len + g_ps.pps_len;

	return total + (total / (RTP_MTU - 2) + MAX_NALS + 2) * 18 + 64;
}

int rtp_h264(struct outq *q, struct rtp_track *t, const uint8_t *au, size_t len, uint32_t ts, int idr)
{
	struct nalref v[MAX_NALS];
	const uint8_t *nal;
	size_t pos = 0, nl;
	int n = 0, i, ps_done = 0;

	while (h264_next_nal(au, len, &pos, &nal, &nl)) {
		int type = nal[0] & 31;

		if (type == 7 || type == 8 || type == 9)
			continue;
		if (type == 5 && idr && !ps_done) {
			ps_done = 1;
			if (g_ps.sps_len && g_ps.pps_len && n + 2 < MAX_NALS) {
				v[n].p = g_ps.sps;
				v[n++].n = g_ps.sps_len;
				v[n].p = g_ps.pps;
				v[n++].n = g_ps.pps_len;
			}
		}
		if (n < MAX_NALS) {
			v[n].p = nal;
			v[n++].n = nl;
		}
	}
	for (i = 0; i < n; i++)
		if (send_nal(q, t, v[i].p, v[i].n, ts, i == n - 1) < 0)
			return -1;
	return 0;
}

/* ---- audio ---- */

int rtp_pcma(struct outq *q, struct rtp_track *t, const uint8_t *data, size_t n, uint32_t ts)
{
	uint8_t *p = pkt_begin(q, t, n, 0, ts);

	if (!p)
		return -1;
	memcpy(p, data, n);
	return 0;
}

/* ---- RTCP sender report + SDES, so that the client can line up audio and video ---- */

int rtp_sr(struct outq *q, struct rtp_track *t, uint32_t rtp_ts)
{
	struct timespec ts;
	uint8_t *p = outq_put(q, 4 + 28 + 16);
	uint64_t frac;

	if (!p)
		return -1;
	clock_gettime(CLOCK_REALTIME, &ts);
	frac = ((uint64_t)ts.tv_nsec << 32) / 1000000000ull;
	p[0] = '$';
	p[1] = t->ch + 1;
	p[2] = 0;
	p[3] = 44;
	p += 4;
	p[0] = 0x80;			/* V=2, no report blocks */
	p[1] = 200;			/* SR */
	p[2] = 0;
	p[3] = 6;
	put32(p + 4, t->ssrc);
	put32(p + 8, (uint32_t)(ts.tv_sec + 2208988800ull));
	put32(p + 12, (uint32_t)frac);
	put32(p + 16, rtp_ts + t->ts_off);
	put32(p + 20, t->pkts);
	put32(p + 24, t->octets);
	p += 28;
	p[0] = 0x81;			/* SDES, one chunk */
	p[1] = 202;
	p[2] = 0;
	p[3] = 3;
	put32(p + 4, t->ssrc);
	p[8] = 1;			/* CNAME */
	p[9] = 2;
	p[10] = 't';
	p[11] = 'p';
	p[12] = p[13] = p[14] = p[15] = 0;
	return 0;
}
