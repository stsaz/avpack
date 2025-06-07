/** avpack: .wv reader
2021, Simon Zolin
*/

/*
wvread_open wvread_close
wvread_process
wvread_info
wvread_seek
wvread_error
wvread_offset
wvread_cursample
wvread_tag
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/wv.h>
#include <avpack/id3v1.h>
#include <avpack/apetag.h>
#include <avpack/shared.h>
#include <ffbase/vector.h>

struct wvread_info {
	ffuint total_samples;
	ffuint bits;
	ffuint flt;
	ffuint channels;
	ffuint sample_rate;
};

struct wv_seekpoint {
	ffuint64 pos;
	ffuint64 off;
};

typedef void (*wv_log_t)(void *udata, const char *fmt, va_list va);

typedef struct wvread {
	ffuint state;
	ffuint nextstate;
	const char *error;
	ffvec buf;
	ffstr chunk;
	ffuint gather_size;
	struct wvread_info info;
	ffuint64 total_size;
	ffuint64 off;

	ffuint block_index, block_samples;
	ffuint hdr_ok, hdr_state;

	struct wv_seekpoint seekpt[2];
	ffuint64 last_seek_off;
	ffuint64 seek_sample;
	ffuint eof :1;

	struct id3v1read id3v1;
	struct apetagread apetag;
	ffstr tagname, tagval;
	int tag;

	wv_log_t log;
	void *udata;
} wvread;

enum WVREAD_R {
	/** New tag is read: use wvread_tag() */
	WVREAD_ID31 = AVPK_META,
	WVREAD_APETAG = AVPK_META,

	/** Need more input data */
	WVREAD_MORE = AVPK_MORE,

	/** Output contains audio data block */
	WVREAD_DATA = AVPK_DATA,

	/** Need more input data at offset wvread_offset() */
	WVREAD_SEEK = AVPK_SEEK,

	WVREAD_ERROR = AVPK_ERROR,
	WVREAD_WARN = AVPK_WARNING,
};

#define _WVR_ERR(w, msg) \
	(w)->error = (msg),  WVREAD_ERROR

static inline const char* wvread_error(wvread *w)
{
	return w->error;
}

/**
total_size: optional */
static inline void wvread_open(wvread *w, ffuint64 total_size)
{
	w->total_size = total_size;
	w->seek_sample = (ffuint64)-1;
}

static inline void wvread_open2(wvread *w, struct avpk_reader_conf *conf)
{
	wvread_open(w, conf->total_size);
	w->id3v1.codepage = conf->code_page;
}

static inline void wvread_close(wvread *w)
{
	ffvec_free(&w->buf);
	apetagread_close(&w->apetag);
}

static inline void _wvr_log(wvread *w, const char *fmt, ...)
{
	if (w->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	w->log(w->udata, fmt, va);
	va_end(va);
}

/** Search for wvpk header inside the buffer
Return header position
  <0 if not found */
static ffssize wv_blk_find(ffstr data, struct wv_hdr *h)
{
	const char *d = data.ptr;
	ffsize len = data.len;

	for (ffsize i = 0;  i != len;  i++) {
		if (d[i] != 'w') {
			ffssize r = ffs_findchar(&d[i], len - i, 'w');
			if (r < 0)
				break;
			i += r;
		}

		if (sizeof(struct wv_hdr_fmt) == wv_hdr_read(h, &d[i], len - i))
			return i;
	}

	return -1;
}

/** Find wvpk header
Return 0 if found */
static int _wvr_hdr_find(wvread *w, struct wv_hdr *h, ffstr *input, ffstr *output)
{
	int r, pos;
	ffstr chunk = {};
	ffuint n = sizeof(struct wv_hdr_fmt);

	for (;;) {
		r = _avpack_gather_header(&w->buf, *input, n, &chunk);
		ffstr_shift(input, r);
		w->off += r;
		if (chunk.len == 0) {
			return 0xfeed;
		}

		pos = wv_blk_find(chunk, h);
		if (pos >= 0)
			break;

		r = _avpack_gather_trailer(&w->buf, *input, n, r);
		// r<0: ffstr_shift() isn't suitable due to assert()
		input->ptr += r,  input->len -= r;
		w->off += r;
	}

	if (chunk.ptr == input->ptr) {
		ffstr_shift(input, n + pos);
		w->off += n + pos;
	}
	ffstr_set(output, &chunk.ptr[pos], n);
	if (w->buf.len != 0) {
		ffstr_erase_left((ffstr*)&w->buf, pos);
		ffstr_set(output, w->buf.ptr, n);
	}
	return 0;
}

static int _wvr_seek_prepare(wvread *w)
{
	if (w->total_size == 0)
		return _WVR_ERR(w, "can't seek");

	w->seekpt[0].pos = 0;
	w->seekpt[0].off = 0;
	w->seekpt[1].pos = w->info.total_samples;
	w->seekpt[1].off = w->total_size;
	return 0;
}

/** Estimate file offset by time position */
static ffuint64 _wvr_seek_offset(const struct wv_seekpoint *sp, ffuint64 target, ffuint64 last_seek_off)
{
	ffuint64 samples = sp[1].pos - sp[0].pos;
	ffuint64 size = sp[1].off - sp[0].off;
	ffuint64 off = (target - sp[0].pos) * size / samples;
	off = sp[0].off + off - ffmin(4*1024, off);
	if (off == last_seek_off)
		off++;
	return off;
}

/** Adjust the search window after the current offset has become too large
Return >0: new file offset
  <0: found the target frame */
static ffint64 _wvr_seek_adjust_edge(wvread *w, struct wv_seekpoint *sp, ffuint64 last_seek_off)
{
	ffuint64 off;
	sp[1].off = last_seek_off; // narrow the search window to make some progress
	if (sp[1].off - sp[0].off > 64*1024)
		off = sp[0].off + (sp[1].off - sp[0].off) / 2; // binary search
	else
		off = sp[0].off + 1; // small search window: try to find a 2nd frame

	if (off == last_seek_off)
		return -1;

	_wvr_log(w, "seek: no new frame at offset %xU, trying %xU", last_seek_off, off);
	return off;
}

/** Adjust the search window
Return <0: found the target frame */
static int _wvr_seek_adjust(wvread *w, struct wv_seekpoint *sp, ffuint64 pos, ffuint64 off, ffuint64 target)
{
	_wvr_log(w, "seek: tgt:%xU cur:%xU [%xU..%xU](%xU)  off:%xU [%xU..%xU](%xU)"
		, target, pos
		, sp[0].pos, sp[1].pos, sp[1].pos - sp[0].pos
		, off
		, sp[0].off, sp[1].off, sp[1].off - sp[0].off);

	int i = (target < pos);
	sp[i].pos = pos;
	sp[i].off = off;

	if (sp[0].off + 1 >= sp[1].off)
		return -1;
	return 0;
}

/**
Return enum WVREAD_R */
static inline int wvread_process(wvread *w, ffstr *input, ffstr *output)
{
	enum {
		R_FTR_SEEK, R_ID3V1, R_APETAG_FTR, R_APETAG, R_HDR_SEEK,
		R_HDR_FIND, R_BLOCK,
		R_SEEK_OFF, R_SEEK_HDR,
		R_GATHER, R_GATHER_MORE,
	};
	int r;

	ffstr empty = {};
	if ((w->eof = !input))
		input = &empty;

	for (;;) {
		switch (w->state) {
		case R_HDR_FIND: {
			if (w->seek_sample != (ffuint64)-1 && w->hdr_ok) {
				if (0 != _wvr_seek_prepare(w))
					return WVREAD_ERROR;
				w->state = R_SEEK_OFF;
				continue;
			}

			struct wv_hdr h;
			if (0 != _wvr_hdr_find(w, &h, input, &w->chunk))
				return WVREAD_MORE;

			_wvr_log(w, "index:%u  samples:%u  size:%u"
				, h.index, h.samples, h.size);

			if (!w->hdr_ok) {
				w->hdr_ok = 1;
				w->info.total_samples = h.total_samples;
				w->info.bits = h.bits;
				w->info.flt = h.flt;
				w->info.channels = h.channels;
				w->info.sample_rate = h.sample_rate;
			}

			w->block_index = h.index;
			w->block_samples = h.samples;

			w->state = R_GATHER_MORE,  w->nextstate = R_BLOCK;
			w->gather_size = h.size;
			continue;
		}

		case R_BLOCK:
			*output = w->chunk;
			w->state = R_HDR_FIND;
			return WVREAD_DATA;


		case R_SEEK_OFF:
			w->off = _wvr_seek_offset(w->seekpt, w->seek_sample, w->last_seek_off);
			w->last_seek_off = w->off;
			w->buf.len = 0;
			w->state = R_SEEK_HDR;
			return WVREAD_SEEK;

		case R_SEEK_HDR: {
			struct wv_hdr h;
			ffuint64 blk_off;
			if (0 != _wvr_hdr_find(w, &h, input, &w->chunk)) {
				if (w->eof)
					goto seek_edge;
				return WVREAD_MORE;
			}

			blk_off = w->off - sizeof(struct wv_hdr_fmt);
			if (w->buf.len != 0)
				blk_off = w->off - w->buf.len;

			if (blk_off >= w->seekpt[1].off) {
				ffint64 o;
seek_edge:
				o = _wvr_seek_adjust_edge(w, w->seekpt, w->last_seek_off);
				if (o >= 0) {
					w->off = o;
					w->last_seek_off = w->off;
					w->buf.len = 0;
					return WVREAD_SEEK;
				}

			} else if (!_wvr_seek_adjust(w, w->seekpt, h.index, blk_off, w->seek_sample)) {
				w->state = R_SEEK_OFF;
				continue;
			}

			w->seek_sample = (ffuint64)-1;
			w->off = w->seekpt[0].off;
			w->state = R_HDR_FIND;
			w->buf.len = 0;
			return WVREAD_SEEK;
		}


		case R_GATHER_MORE:
			if (w->buf.len == 0) {
				if (w->chunk.len != ffvec_addstr(&w->buf, &w->chunk))
					return _WVR_ERR(w, "not enough memory");
			}
			w->state = R_GATHER;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&w->buf, &w->buf.cap, input->ptr, input->len, w->gather_size, &w->chunk);
			if (r < 0)
				return _WVR_ERR(w, "not enough memory");
			ffstr_shift(input, r);
			w->off += r;
			if (w->chunk.len == 0)
				return WVREAD_MORE;
			w->state = w->nextstate;
			w->buf.len = 0;
			continue;


		case R_FTR_SEEK:
			if (w->total_size == 0) {
				w->state = R_HDR_FIND;
				continue;
			}

			w->state = R_GATHER,  w->nextstate = R_ID3V1;
			w->gather_size = sizeof(struct apetaghdr) + sizeof(struct id3v1);
			w->gather_size = ffmin(w->gather_size, w->total_size);
			w->off = w->total_size - w->gather_size;
			return WVREAD_SEEK;

		case R_ID3V1: {
			if (sizeof(struct id3v1) > w->chunk.len) {
				w->state = R_APETAG_FTR;
				continue;
			}

			ffstr id3;
			ffstr_set(&id3, &w->chunk.ptr[w->chunk.len - sizeof(struct id3v1)], sizeof(struct id3v1));
			r = id3v1read_process(&w->id3v1, id3, &w->tagval);

			switch (r) {
			case ID3V1READ_DONE:
				w->total_size -= sizeof(struct id3v1);
				w->chunk.len -= sizeof(struct id3v1);
				w->off -= sizeof(struct id3v1);
				break;

			case ID3V1READ_NO:
				break;

			default:
				w->tag = -r;
				return WVREAD_ID31;
			}
		}
			// fallthrough

		case R_APETAG_FTR: {
			apetagread_open(&w->apetag);
			ffint64 seek;
			r = apetagread_footer(&w->apetag, w->chunk, &seek);

			switch (r) {
			case APETAGREAD_SEEK:
				w->off += seek;
				w->total_size = w->off;
				w->state = R_APETAG;
				return WVREAD_SEEK;

			default:
				w->state = R_HDR_SEEK;
				continue;
			}
			break;
		}

		case R_APETAG: {
			ffsize len = input->len;
			r = apetagread_process(&w->apetag, input, &w->tagname, &w->tagval);
			w->off += len - input->len;

			switch (r) {
			case APETAGREAD_MORE:
				return WVREAD_MORE;

			case APETAGREAD_DONE:
				apetagread_close(&w->apetag);
				w->state = R_HDR_SEEK;
				break;

			case APETAGREAD_ERROR:
				w->state = R_HDR_SEEK;
				w->error = apetagread_error(&w->apetag);
				return WVREAD_WARN;

			default:
				w->tag = -r;
				return WVREAD_APETAG;
			}
		}
			// fallthrough

		case R_HDR_SEEK:
			w->state = R_HDR_FIND;
			w->off = 0;
			return WVREAD_SEEK;
		}
	}
}

static inline int wvread_process2(wvread *w, ffstr *input, union avpk_read_result *res)
{
	if (w->hdr_state == 1) {
		w->hdr_state = 2;
		*(ffstr*)&res->frame = w->chunk;
		res->frame.pos = w->block_index;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = w->block_samples;
		return AVPK_DATA;
	}

	int r = wvread_process(w, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_META:
		res->tag.id = w->tag;
		res->tag.name = w->tagname;
		res->tag.value = w->tagval;
		break;

	case AVPK_DATA:
		if (!w->hdr_state) {
			w->hdr_state = 1;
			ffmem_zero_obj(res);
			res->hdr.codec = AVPKC_WAVPACK;
			res->hdr.sample_bits = w->info.bits;
			res->hdr.sample_float = w->info.flt;
			res->hdr.sample_rate = w->info.sample_rate;
			res->hdr.channels = w->info.channels;
			res->hdr.duration = w->info.total_samples;
			return AVPK_HEADER;
		}
		res->frame.pos = w->block_index;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = w->block_samples;
		break;

	case AVPK_SEEK:
		res->seek_offset = w->off;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = w->error;
		res->error.offset = w->off;
		break;
	}
	return r;
}

static inline const struct wvread_info* wvread_info(wvread *w)
{
	return &w->info;
}

static inline void wvread_seek(wvread *w, ffuint64 sample)
{
	w->seek_sample = sample;
}

/**
Return enum MMTAG */
static inline int wvread_tag(wvread *w, ffstr *name, ffstr *val)
{
	*name = w->tagname;
	*val = w->tagval;
	return w->tag;
}

#define wvread_offset(w)  ((w)->off)

#define wvread_cursample(w)  ((w)->block_index)

#undef _WVR_ERR

AVPKR_IF_INIT(avpk_wv, "wv", AVPKF_WV, wvread, wvread_open2, wvread_process2, wvread_seek, wvread_close);
