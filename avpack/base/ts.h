/** avpack: .ts format
2024, Simon Zolin */

/*
ts_pkt_read
ts_top_read
ts_info_read
ts_data_hdr_read
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
	ffuint64 off;
	ffstr body;
};

enum TS_STREAM {
	TS_STREAM_AUDIO_MP3 = 3,
	TS_STREAM_AUDIO_AAC = 15,
};

enum _TSR_PMT {
	_TSR_PMT_TOP,
	_TSR_PMT_INFO,
	_TSR_PMT_DATA,
};

struct _tsr_pm {
	ffuint pid;
	ffuint type; // enum _TSR_PMT
	ffuint stream_type; // enum TS_STREAM
	ffuint stream_id;
	ffuint pkt_len;
	ffuint64 pos_msec;
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

/* Format:
[8]
sid[2]
pid_info[2]
*/
static inline int ts_top_read(ffstr data)
{
	const ffbyte *d = (ffbyte*)data.ptr;
	d += 8;
	d += 2;

	ffuint pid = ffint_be_cpu16_ptr(d) & 0x1fff;
	d += 2;

	return pid;
}

/* Format:
[1]
[2]
sid[2]
[3]
pid_data[2]
len[2]
	...
stream_type
pid[2]
desc_list_len[2]
tags {
	desc_tag
	len
	data[]
}...
*/
static inline int ts_info_read(ffstr data, ffuint *stream_type)
{
	const ffbyte *d = (ffbyte*)data.ptr;
	d += 1;
	d += 2;
	d += 2;
	d += 3;

	ffuint pid = ffint_be_cpu16_ptr(d) & 0x1fff;
	d += 2;

	ffuint n = ffint_be_cpu16_ptr(d) & 0x0fff;
	if (n != 0)
		return 0; // case not supported
	d += 2;

	*stream_type = *d++;
	return pid;
}

/* Format:
header[2] // 0x0001
stream_id
packet_len[2]
[1]
flags
pos_len
pos[]
*/
static int ts_data_hdr_read(struct _tsr_pm *pm, ffstr data)
{
	const ffbyte *d = (ffbyte*)data.ptr, *end = (ffbyte*)data.ptr + data.len;
	if (ffint_be_cpu16_ptr(d) != 0x0001)
		return 0;
	d += 2;

	pm->stream_id = *d++;

	pm->pkt_len = ffint_be_cpu16_ptr(d);
	d += 2;

	d++;
	d++;

	ffuint n = *d++;
	if (d + n > end || n < 5)
		return 0;
	// HI[8] MID[16] LO[16] -> HI[3]MID[15]LO[15]
	// ....hhh. mmmmmmmmmmmmmmm. lllllllllllllll. -> hhhmmmmmmmmmmmmmmmlllllllllllllll
	ffuint64 pos = ((ffuint64)(*d & 0x0e) << 29)
		| ((ffint_be_cpu16_ptr(d + 1) & 0xfffe) << 14)
		| (ffint_be_cpu16_ptr(d + 3) >> 1);
	pm->pos_msec = pos / 90;
	d += n;

	return d - (ffbyte*)data.ptr;
}
