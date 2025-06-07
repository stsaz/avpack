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

#pragma once
#include <avpack/decl.h>
#include <avpack/base/ts.h>
#include <ffbase/stream.h>
#include <ffbase/map.h>

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
	ffuint sync :1;

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
	(void)opaque;
	(void)keylen;
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
	(void)total_size;
	t->gather = 188;
	ffstream_realloc(&t->stream, 188);

	ffmap_init(&t->pms, _tsr_pids_keyeq);
	_tsr_pm_add_new(t, 0);
}

static inline void tsread_open2(tsread *t, struct avpk_reader_conf *conf)
{
	tsread_open(t, conf->total_size);
	t->log = conf->log;
	t->udata = conf->opaque;
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
	TSREAD_ERROR = AVPK_ERROR,
	TSREAD_WARN = AVPK_WARNING,
	TSREAD_MORE = AVPK_MORE,
	TSREAD_DATA = AVPK_DATA,
};

#define _TSR_ERR(t, e) \
	(t)->error = e,  TSREAD_ERROR

static inline const char* tsread_error(tsread *t)
{
	return t->error;
}

/** Process .ts data chunk.
Return enum TSREAD_R */
static inline int tsread_process(tsread *t, ffstr *input, ffstr *output)
{
	int r;
	ffstr chunk = {};

	for (;;) {
		r = ffstream_gather(&t->stream, *input, t->gather, &chunk);
		ffstr_shift(input, r);
		t->off += r;
		if (chunk.len < t->gather)
			return TSREAD_MORE;

		if (!t->sync) {
			r = ffstr_findchar(&chunk, 0x47);
			if (r < 0) {
				ffstream_reset(&t->stream);
				continue;
			} else if (r > 0) {
				ffstream_consume(&t->stream, r);
				continue;
			}
		}
		chunk.len = t->gather;

		ffmem_zero_obj(&t->pkt);
		r = ts_pkt_read(&t->pkt, chunk.ptr, chunk.len);
		if (r <= 0) {
			ffstream_consume(&t->stream, 1);
			if (!t->sync)
				continue;
			t->sync = 0;
			t->error = "lost synchronization";
			return TSREAD_WARN;
		}
		t->sync = 1;
		t->pkt.off = t->off - ffstream_used(&t->stream);
		ffstream_consume(&t->stream, t->gather);

		const struct ts_packet *p = &t->pkt;
		_tsr_log(t, "packet #%u: pid:%xu  counter:%u  adaptation:%u(%xu)  start:%u  payload:%u  offset:%xU"
			, t->n_pkt++, p->pid, p->counter
			, p->adaptation_len, p->adaptation_flags
			, p->start, (int)p->body.len, p->off);

		struct _tsr_pm *pm = ffmap_find_hash(&t->pms, t->pkt.pid, (void*)(ffsize)t->pkt.pid, 4, NULL);
		if (!pm) {
			continue;
		}

		switch (pm->type) {
		case _TSR_PMT_TOP:
			if ((r = ts_top_read(t->pkt.body)) <= 0)
				return TSREAD_ERROR;
			pm = _tsr_pm_add(t, r);
			pm->type = _TSR_PMT_INFO;
			_tsr_log(t, " pid_info:%xu"
				, pm->pid);
			break;

		case _TSR_PMT_INFO: {
			ffuint stream_type;
			if ((r = ts_info_read(t->pkt.body, &stream_type)) <= 0) {
				t->error = "bad info block";
				return TSREAD_ERROR;
			}
			pm = _tsr_pm_add(t, r);
			pm->type = _TSR_PMT_DATA;
			pm->stream_type = stream_type;
			_tsr_log(t, " pid_data:%xu  stream_type:%xu"
				, pm->pid, pm->stream_type);
			break;
		}

		case _TSR_PMT_DATA:
			if (t->pkt.start) {
				if (0 == (r = ts_data_hdr_read(pm, t->pkt.body)))
					return TSREAD_ERROR;
				_tsr_log(t, " stream_id:%xu  packet:%u  pos:%U"
					, pm->stream_id, pm->pkt_len, pm->pos_msec);
				ffstr_shift(&t->pkt.body, r);
			}

			*output = t->pkt.body;
			t->cur = pm;
			return TSREAD_DATA;
		}
	}
}

static inline int tsread_process2(tsread *t, ffstr *input, union avpk_read_result *res)
{
	int r = tsread_process(t, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_DATA:
		res->frame.pos = t->cur->pos_msec;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		break;

	case AVPK_SEEK:
		res->seek_offset = t->pkt.off;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = t->error;
		res->error.offset = t->pkt.off;
		break;
	}
	return r;
}

static inline const struct _tsr_pm* tsread_info(tsread *t)
{
	return t->cur;
}

#define tsread_pos_msec(t)  ((t)->cur->pos_msec)

#define tsread_offset(t)  ((t)->pkt.off)

AVPKR_IF_INIT(avpk_ts, "ts", AVPKF_TS, tsread, tsread_open2, tsread_process2, NULL, tsread_close);
