/** avpack: .wav reader
2015,2021, Simon Zolin
*/

/*
wavread_open
wavread_close
wavread_process
wavread_info
wavread_seek
wavread_error
wavread_offset
wavread_cursample
wavread_tag
*/

#pragma once

#include <avpack/wav-fmt.h>
#include <ffbase/vector.h>

struct wav_chunk {
	ffuint id;
	ffuint size;
	ffuint flags;
	const struct wav_bchunk *ctx;
};

typedef void (*wav_log_t)(void *udata, const char *fmt, va_list va);

typedef struct wavread {
	ffuint state;
	const char *errmsg;

	struct wav_chunk chunks[4];
	ffuint ictx;

	ffvec buf;
	ffuint gsize;
	ffstr gbuf;
	ffuint nxstate;

	struct wav_info info;
	ffuint sampsize;
	ffuint samples;

	ffuint64 datasize
		, dataoff
		, off;
	ffuint64 cursample;
	ffuint64 seek_sample;
	int has_fmt :1
		, fin :1
		, inf_data :1
		;

	int tag;
	ffstr tagval;

	wav_log_t log;
	void *udata;
} wavread;

static inline void _wavread_log(wavread *w, const char *fmt, ...)
{
	if (w->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	w->log(w->udata, fmt, va);
	va_end(va);
}

/** Find chunk within the specified context */
static int _wavread_findchunk(wavread *w, const void *data, const struct wav_bchunk *ctx, struct wav_chunk *chunk, ffuint64 off)
{
	const struct wav_chunkhdr *ch = (void*)data;
	chunk->id = 0;
	int i = wav_bchunk_find(data, ctx);
	if (i >= 0) {
		chunk->id = ctx[i].flags & 0xff;
		chunk->flags = ctx[i].flags & 0xffffff00;
	}

	chunk->size = ffint_le_cpu32_ptr(ch->size);
	chunk->flags |= (chunk->size % 2) ? WAV_F_PADD : 0;

	_wavread_log(w, "chunk \"%4s\"  size:%u  off:%xU"
		, ch->id, chunk->size, off);
	return 0;
}

static inline const char* wavread_error(wavread *w)
{
	return w->errmsg;
}

#define _WAVR_ERR(w, e) \
	(w)->errmsg = (e),  WAVREAD_ERROR

static inline void wavread_open(wavread *w)
{
	w->seek_sample = (ffuint64)-1;
	w->chunks[0].ctx = wav_ctx_global;
	w->chunks[0].size = (ffuint)-1;
}

static inline void wavread_close(wavread *w)
{
	ffvec_free(&w->buf);
}

enum WAVREAD_R {
	WAVREAD_MORE,
	WAVREAD_HEADER,
	WAVREAD_SEEK,
	WAVREAD_DATA,
	WAVREAD_DONE,
	WAVREAD_TAG,
	WAVREAD_ERROR,
};

static int _wavread_chunk(wavread *w)
{
	struct wav_chunk *chunk = &w->chunks[w->ictx];
	int r;

	if (chunk->id & WAV_T_TAG) {
		w->tag = chunk->id & ~WAV_T_TAG;
		ffstr_setz(&w->tagval, w->gbuf.ptr);
		return WAVREAD_TAG;
	}

	switch (chunk->id) {
	case WAV_T_RIFF:
		if (!!ffmem_cmp(w->gbuf.ptr, "WAVE", 4))
			return _WAVR_ERR(w, "invalid RIFF chunk");

		w->chunks[w->ictx].ctx = wav_ctx_riff;
		break;

	case WAV_T_FMT:
		if (w->has_fmt)
			return _WAVR_ERR(w, "duplicate format chunk");
		w->has_fmt = 1;

		if (0 > (r = wav_fmt_read(w->gbuf.ptr, w->gbuf.len, &w->info)))
			return _WAVR_ERR(w, "bad format chunk");

		w->sampsize = (w->info.format&0xff)/8 * w->info.channels;
		if (NULL == ffvec_realloc(&w->buf, w->sampsize, 1))
			return _WAVR_ERR(w, "not enough memory");
		break;

	case WAV_T_LIST:
		if (!!ffmem_cmp(w->gbuf.ptr, "INFO", 4)) {
			return 0xbad;
		}

		w->chunks[w->ictx].ctx = wav_ctx_list;
		break;

	case WAV_T_DATA:
		if (!w->has_fmt)
			return _WAVR_ERR(w, "no format chunk");

		w->dataoff = w->off;
		w->datasize = ffint_align_floor(chunk->size, w->sampsize);
		if (!w->inf_data)
			w->info.total_samples = chunk->size / w->sampsize;
		return WAVREAD_HEADER;
	}

	return 0;
}

/**
Return enum WAVREAD_R */
static inline int wavread_process(wavread *w, ffstr *input, ffstr *output)
{
	enum {
		R_FIRSTCHUNK, R_NEXTCHUNK, R_CHUNKHDR, R_CHUNK, R_SKIP, R_PADDING,
		R_GATHER,
		R_DATA, R_DATAOK, R_BUFDATA,
	};
	int r;
	struct wav_chunk *chunk, *parent;

	for (;;) {
		switch (w->state) {

		case R_SKIP:
			chunk = &w->chunks[w->ictx];
			r = ffmin(chunk->size, input->len);
			ffstr_shift(input, r);
			chunk->size -= r;
			w->off += r;
			if (chunk->size != 0)
				return WAVREAD_MORE;

			w->state = R_NEXTCHUNK;
			continue;

		case R_PADDING:
			if (input->len == 0)
				return WAVREAD_MORE;

			if (input->ptr[0] == '\0') {
				// skip padding byte
				ffstr_shift(input, 1);
				w->off += 1;
				parent = &w->chunks[w->ictx - 1];
				if (parent->size != 0)
					parent->size -= 1;
			}

			w->state = R_NEXTCHUNK;
			// fallthrough

		case R_NEXTCHUNK:
			chunk = &w->chunks[w->ictx];

			if (chunk->size == 0) {
				if (chunk->flags & WAV_F_PADD) {
					chunk->flags &= ~WAV_F_PADD;
					w->state = R_PADDING;
					continue;
				}

				ffuint id = chunk->id;
				ffmem_zero_obj(chunk);
				w->ictx--;

				switch (id) {
				case WAV_T_RIFF:
					return WAVREAD_DONE;
				}

				continue;
			}

			FF_ASSERT(chunk->ctx != NULL);
			// fallthrough

		case R_FIRSTCHUNK:
			w->gsize = sizeof(struct wav_chunkhdr);
			w->state = R_GATHER;  w->nxstate = R_CHUNKHDR;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&w->buf, &w->buf.cap, input->ptr, input->len, w->gsize, &w->gbuf);
			if (r < 0)
				return _WAVR_ERR(w, "not enough memory");
			ffstr_shift(input, r);
			w->off += r;
			if (w->gbuf.len == 0)
				return WAVREAD_MORE;
			w->buf.len = 0;
			w->state = w->nxstate;
			continue;

		case R_CHUNKHDR: {
			parent = &w->chunks[w->ictx];
			chunk = &w->chunks[++w->ictx];
			_wavread_findchunk(w, w->gbuf.ptr, parent->ctx, chunk, w->off - w->gbuf.len);

			if (chunk->id == WAV_T_DATA && chunk->size == (ffuint)-1) {
				parent->size = 0;
			} else {
				if (chunk->size > parent->size)
					return _WAVR_ERR(w, "too large chunk");
				parent->size -= sizeof(struct wav_chunkhdr) + chunk->size;
			}

			if (chunk->id == 0 && parent->ctx == wav_ctx_global)
				return _WAVR_ERR(w, "no RIFF chunk");

			if (chunk->id == 0) {
				//unknown chunk
				w->state = R_SKIP;
				continue;
			}

			if (chunk->id == WAV_T_DATA && chunk->size == (ffuint)-1)
				w->inf_data = 1;

			ffuint minsize = WAV_GET_MINSIZE(chunk->flags);
			if (minsize != 0 && chunk->size < minsize)
				return _WAVR_ERR(w, "too small chunk");

			if ((chunk->flags & WAV_F_WHOLE) || minsize != 0) {
				w->gsize = (minsize != 0) ? minsize : chunk->size;
				chunk->size -= w->gsize;
				w->state = R_GATHER;  w->nxstate = R_CHUNK;
				continue;
			}

			w->state = R_CHUNK;
			continue;
		}

		case R_CHUNK:
			r = _wavread_chunk(w);
			switch (r) {
			case WAVREAD_HEADER:
				w->state = R_DATA;
				return WAVREAD_HEADER;

			case WAVREAD_TAG:
				w->state = R_NEXTCHUNK;
				return WAVREAD_TAG;

			case WAVREAD_ERROR:
				return WAVREAD_ERROR;

			case 0xbad:
				w->state = R_SKIP;
				continue;

			case 0:
				break;
			}

			w->state = R_NEXTCHUNK;
			continue;

		case R_DATAOK:
			w->cursample += w->samples;
			w->state = R_DATA;
			// fallthrough

		case R_DATA: {
			if (w->seek_sample != (ffuint64)-1) {
				w->cursample = w->seek_sample;
				w->off = w->dataoff + w->seek_sample * w->sampsize;
				w->seek_sample = (ffuint64)-1;
				return WAVREAD_SEEK;
			}

			ffuint chunk_size = w->dataoff + w->datasize - w->off;
			if (chunk_size == 0) {
				chunk = &w->chunks[w->ictx];
				chunk->size -= w->datasize;
				w->state = R_SKIP;
				continue;
			}
			ffuint n = (ffuint)ffmin(chunk_size, input->len);
			n = ffint_align_floor(n, w->sampsize);

			if (n == 0) {
				w->gsize = w->sampsize;
				w->state = R_GATHER;  w->nxstate = R_BUFDATA;
				continue; // not even 1 complete sample
			}
			ffstr_set(output, (void*)input->ptr, n);
			ffstr_shift(input, n);
			w->off += n;
			w->samples = n / w->sampsize;
			w->state = R_DATAOK;
			return WAVREAD_DATA;
		}

		case R_BUFDATA:
			*output = w->gbuf;
			w->samples = 1;
			w->state = R_DATAOK;
			return WAVREAD_DATA;

		}
	}
	//unreachable
}

#undef _WAVR_ERR

static inline const struct wav_info* wavread_info(wavread *w)
{
	return &w->info;
}

static inline void wavread_seek(wavread *w, ffuint64 sample)
{
	w->seek_sample = sample;
}

/**
Return enum MMTAG */
static inline int wavread_tag(wavread *w, ffstr *val)
{
	*val = w->tagval;
	return w->tag;
}

#define wavread_offset(w)  ((w)->off)
#define wavread_cursample(w)  ((w)->cursample)
