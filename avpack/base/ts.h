/** avpack: .ts format
2024, Simon Zolin */

/*
ts_pkt_read
*/

/* Format:
PKT(PID:0 (PID#INFO))
PKT(PID:#INFO (PID#DATA))
PKT(PID:#DATA, START_FLAG, HDR(POS), DATA)
PKT(PID:#DATA, DATA)...
*/

#pragma once
#include <ffbase/string.h>

struct ts_packet {
	ffuint pid, counter;
	ffuint start;
	ffuint adaptation_len, adaptation_flags;
	ffuint64 pos_msec;
	ffuint64 off;
	ffstr body;
};

enum TS_STREAM {
	TS_STREAM_AUDIO_MP3 = 3,
	TS_STREAM_AUDIO_AAC = 15,
};

static ffuint ts_read8(const ffbyte **d, const ffbyte *end)
{
	if (*d + 1 > end)
		return ~0U;
	ffuint r = **d;
	*d += 1;
	return r;
}

/* Format:
sync			// 0x47
flags_pid[2]	// [1], have_payload_start, [1], [13]
flags_counter 	// [2], have_adaptation, have_payload, [4]
adaptation_len
adaptation_data[]
payload_start
payload[]
*/
static inline int ts_pkt_read(struct ts_packet *p, const void *data, size_t len)
{
	ffuint n;
	const ffbyte *d = (ffbyte*)data, *end = (ffbyte*)data + len;
	if (d[0] != 0x47)
		return -1;
	p->start = !!(d[1] & 0x40);
	p->pid = (ffint_be_cpu32_ptr(d) & 0x1fff00) >> 8;
	unsigned have_adaptation = !!(d[3] & 0x20), have_payload = !!(d[3] & 0x10);
	p->counter = d[3] & 0x0f;
	d += 4;

	p->adaptation_len = 0;
	p->adaptation_flags = 0;
	if (have_adaptation) {
		p->adaptation_len = d[0];
		if (d + 1 + p->adaptation_len > end)
			return 0;
		p->adaptation_flags = d[1];
		d += 1 + p->adaptation_len;
	}

	if (p->start) {
		if (~0U == (n = ts_read8(&d, end)))
			return 0;
		if (n != 0) {
			return -1;
		}
	}
	if (d > end)
		return 0;
	p->body.len = 0;
	if (have_payload)
		ffstr_set(&p->body, d, end - d);

	return len;
}
