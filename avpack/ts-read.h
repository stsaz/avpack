/** avpack: .ts reader
2024, Simon Zolin */

/*
tsread_open
tsread_close
tsread_process
tsread_info
tsread_pos_msec
tsread_offset
tsread_error
*/

/* Format:
PKT(PID:0 (PID#INFO))
PKT(PID:#INFO (PID#DATA))
PKT(PID:#DATA, START_FLAG, HDR(POS), DATA)
PKT(PID:#DATA, DATA)...
*/

#pragma once
#include <ffbase/stream.h>
#include <ffbase/map.h>

struct ts_packet {
	ffuint pid, counter;
	ffuint start;
	ffuint64 pos_msec;
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
	ffuint pkt_len;
	ffuint64 pos_msec;
};

typedef void (*ts_log_t)(void *udata, const char *fmt, va_list va);

typedef struct tsread {
	ffuint gather;
	ffuint n_pkt;
	ffuint64 off;
	ffmap pms; // pid => struct _tsr_pm*
	struct _tsr_pm *cur;
	ffstream stream;
	struct ts_packet pkt;
	const char *error;

	ts_log_t log;
	void *udata;
} tsread;

static inline void _tsr_log(tsread *t, const char *fmt, ...)
{
	if (t->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	t->log(t->udata, fmt, va);
	va_end(va);
}

static int _tsr_pids_keyeq(void *opaque, const void *key, ffsize keylen, void *val)
{
	const struct _tsr_pm *pm = val;
	ffuint pid = (ffsize)key;
	return (pid == pm->pid);
}

static struct _tsr_pm* _tsr_pm_add_new(tsread *t, ffuint pid)
{
	struct _tsr_pm *pm = ffmem_new(struct _tsr_pm);
	pm->pid = pid;
	ffmap_add_hash(&t->pms, pid, pm);
	return pm;
}

static struct _tsr_pm* _tsr_pm_add(tsread *t, ffuint pid)
{
	struct _tsr_pm *pm = ffmap_find_hash(&t->pms, pid, (void*)(ffsize)pid, 4, NULL);
	if (!pm)
		pm = _tsr_pm_add_new(t, pid);
	return pm;
}

static inline void tsread_open(tsread *t, ffuint64 total_size)
{
	t->gather = 188;
	ffstream_realloc(&t->stream, 188);

	ffmap_init(&t->pms, _tsr_pids_keyeq);
	_tsr_pm_add_new(t, 0);
}

static inline void tsread_close(tsread *t)
{
	struct _ffmap_item *it;
	FFMAP_WALK(&t->pms, it) {
		if (_ffmap_item_occupied(it))
			continue;
		ffmem_free(it->val);
	}
	ffmap_free(&t->pms);
	ffstream_free(&t->stream);
}

enum TSREAD_R {
	TSREAD_ERROR,
	TSREAD_WARN,
	TSREAD_MORE,
	TSREAD_DATA,
};

#define _TSR_ERR(t, e) \
	(t)->error = e,  TSREAD_ERROR

static inline const char* tsread_error(tsread *t)
{
	return t->error;
}

static ffuint _tsr_read8(const ffbyte **d, const ffbyte *end)
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
static int _tsr_pkt_read(tsread *t, ffstr data, struct ts_packet *p)
{
	ffmem_zero_obj(p);
	p->off = t->off - ffstream_used(&t->stream);

	ffuint n;
	const ffbyte *d = (ffbyte*)data.ptr, *end = (ffbyte*)data.ptr + data.len;
	if (d[0] != 0x47)
		goto bad;
	p->start = !!(d[1] & 0x40);
	p->pid = (ffint_be_cpu32_ptr(d) & 0x1fff00) >> 8;
	unsigned have_adaptation = !!(d[3] & 0x20), have_payload = !!(d[3] & 0x10);
	p->counter = d[3] & 0x0f;
	d += 4;

	unsigned ada_len = 0, ada_flags = 0;
	if (have_adaptation) {
		ada_len = d[0];
		if (d + 1 + ada_len > end)
			goto bad;
		ada_flags = d[1];
		d += 1 + ada_len;
	}

	if (p->start) {
		if (~0U == (n = _tsr_read8(&d, end)))
			goto bad;
		if (n != 0) {
			t->error = "case not supported";
			return -1;
		}
	}
	if (d > end)
		goto bad;
	if (have_payload)
		ffstr_set(&p->body, d, end - d);

	_tsr_log(t, "packet #%u: pid:%xu  counter:%u  adaptation:%u(%xu)  start:%u  payload:%u  offset:%xU"
		, t->n_pkt++, p->pid, p->counter
		, ada_len, ada_flags
		, p->start, (int)p->body.len, p->off);

	return 0;

bad:
	t->error = "bad header";
	return -1;
}

/* Format:
[8]
sid[2]
pid_info[2]
*/
static int _tsr_top_read(tsread *t, ffstr data)
{
	const ffbyte *d = (ffbyte*)data.ptr;
	d += 8;
	d += 2;

	ffuint pid = ffint_be_cpu16_ptr(d) & 0x1fff;
	d += 2;
	struct _tsr_pm *pm = _tsr_pm_add(t, pid);
	pm->type = _TSR_PMT_INFO;

	_tsr_log(t, " pid_info:%xu"
		, pid);
	return 0;
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
static int _tsr_info_read(tsread *t, ffstr data)
{
	const ffbyte *d = (ffbyte*)data.ptr, *end = (ffbyte*)data.ptr + data.len;
	d += 1;
	d += 2;
	d += 2;
	d += 3;

	ffuint pid = ffint_be_cpu16_ptr(d) & 0x1fff;
	d += 2;
	struct _tsr_pm *pm = _tsr_pm_add(t, pid);
	pm->type = _TSR_PMT_DATA;

	ffuint n = ffint_be_cpu16_ptr(d) & 0x0fff;
	d += 2;
	if (n != 0) {
		t->error = "case not supported";
		return -1;
	}

	if (~0U == (pm->stream_type = _tsr_read8(&d, end)))
		goto bad;

	_tsr_log(t, " pid_data:%xu  stream_type:%xu"
		, pm->pid, pm->stream_type);
	return 0;

bad:
	t->error = "bad data";
	return -1;
}

/* Format:
header[2] // 0x0001
stream_id
packet_len[2]_tsr_data_hdr_read
[1]
flags
pos_len
pos[]
*/
static int _tsr_data_hdr_read(tsread *t, struct _tsr_pm *pm, ffstr *data)
{
	const ffbyte *d = (ffbyte*)data->ptr, *end = (ffbyte*)data->ptr + data->len;
	if (ffint_be_cpu16_ptr(d) != 0x0001)
		goto bad;
	d += 2;

	ffuint stream_id = *d++;

	pm->pkt_len = ffint_be_cpu16_ptr(d);
	d += 2;

	d++;
	d++;

	ffuint n = *d++;
	if (d + n > end
		|| n < 5)
		goto bad;
	// HI[8] MID[16] LO[16] -> HI[3]MID[15]LO[15]
	ffuint64 pos = (((ffuint64)(*d & 0x0e) >> 1) << 30)
		| ((ffint_be_cpu16_ptr(d + 1) >> 1) << 15)
		| (ffint_be_cpu16_ptr(d + 3) >> 1);
	pm->pos_msec = pos / 90;
	d += n;

	ffstr_set(data, d, end - d);

	_tsr_log(t, " stream_id:%xu  packet:%u  pos:%U"
		, stream_id, pm->pkt_len, pm->pos_msec);
	return 0;

bad:
	t->error = "bad data";
	return -1;
}

/** Process .ts data chunk.
Return enum TSREAD_R */
static inline int tsread_process(tsread *t, ffstr *input, ffstr *output)
{
	int r;
	ffstr chunk;

	for (;;) {
		r = ffstream_gather(&t->stream, *input, t->gather, &chunk);
		ffstr_shift(input, r);
		t->off += r;
		if (chunk.len < t->gather)
			return TSREAD_MORE;
		chunk.len = t->gather;

		r = _tsr_pkt_read(t, chunk, &t->pkt);
		ffstream_consume(&t->stream, t->gather);
		if (r)
			return TSREAD_WARN;

		struct _tsr_pm *pm = ffmap_find_hash(&t->pms, t->pkt.pid, (void*)(ffsize)t->pkt.pid, 4, NULL);
		if (!pm) {
			continue;
		}

		switch (pm->type) {
		case _TSR_PMT_TOP:
			if (_tsr_top_read(t, t->pkt.body))
				return TSREAD_ERROR;
			break;

		case _TSR_PMT_INFO:
			if (_tsr_info_read(t, t->pkt.body))
				return TSREAD_ERROR;
			break;

		case _TSR_PMT_DATA:
			if (t->pkt.start) {
				if (_tsr_data_hdr_read(t, pm, &t->pkt.body))
					return TSREAD_ERROR;
				pm->pos_msec = t->pkt.pos_msec;
			}

			*output = t->pkt.body;
			t->cur = pm;
			return TSREAD_DATA;
		}
	}
}

static inline const struct _tsr_pm* tsread_info(tsread *t)
{
	return t->cur;
}

#define tsread_pos_msec(t)  ((t)->cur->pos_msec)

#define tsread_offset(t)  ((t)->pkt.off)
