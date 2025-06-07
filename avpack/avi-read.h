/** avpack: .avi reader
* no seeking

2016,2021, Simon Zolin
*/

/*
aviread_open
aviread_close
aviread_process
aviread_error
aviread_track_info
aviread_track_activate
aviread_tag
aviread_offset
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/avi.h>
#include <ffbase/vector.h>

struct aviread_seekpt {
	ffuint off;
};

typedef void (*avi_log_t)(void *udata, const char *fmt, va_list va);

struct aviread_chunk {
	ffuint id;
	ffuint size;
	ffuint flags;
	const struct avi_binchunk *ctx;
};

typedef struct aviread {
	ffuint state;
	ffuint nxstate;
	ffuint err;
	ffuint64 nsamples;

	struct aviread_chunk chunks[5];
	ffuint ictx;

	ffvec buf;
	ffuint gather_size;
	ffstr gbuf;
	ffuint64 off;

	ffvec tracks; // struct avi_audio_info[]
	struct avi_audio_info *curtrack;

	ffuint movi_off;
	ffuint movi_size;

	int tag; //enum FFMMTAG or -1
	ffstr tagval;

	ffuint has_fmt :1
		, codec_conf_pending :1
		;

	avi_log_t log;
	void *udata;
} aviread;

static inline void aviread_open(aviread *a)
{
	a->chunks[0].ctx = avi_ctx_global;
	a->chunks[0].size = (ffuint)-1;
}

static inline void aviread_open2(aviread *a, struct avpk_reader_conf *conf)
{
	aviread_open(a);
	a->log = conf->log;
	a->udata = conf->opaque;
}

static inline void aviread_close(aviread *a)
{
	ffvec_free(&a->buf);
	struct avi_audio_info *t;
	FFSLICE_WALK(&a->tracks, t) {
		ffstr_free(&t->codec_conf);
	}
	ffvec_free(&a->tracks);
}

#define aviread_cursample(a)  ((a)->nsamples - (a)->curtrack->blocksize)

static inline const void* aviread_track_info(aviread *a, int index)
{
	const struct avi_audio_info *t = (struct avi_audio_info*)a->tracks.ptr;
	if (index == -1) {
		if (a->tracks.len == 0)
			return NULL;
		t = &t[a->tracks.len-1];
	} else if (index < (ffssize)a->tracks.len) {
		t = &t[index];
	} else {
		return NULL;
	}
	return t;
}

static inline void aviread_track_activate(aviread *a, int index)
{
	struct avi_audio_info *t = (struct avi_audio_info*)a->tracks.ptr;
	a->curtrack = &t[index];
}

/** Get tag info */
static inline int aviread_tag(aviread *a, ffstr *val)
{
	*val = a->tagval;
	return a->tag;
}

#define aviread_offset(a)  ((a)->off)

enum AVIREAD_R {
	AVIREAD_DATA = AVPK_DATA,
	AVIREAD_MORE = AVPK_MORE,
	AVIREAD_HEADER = AVPK_HEADER,
	AVIREAD_DONE = AVPK_FIN,
	AVIREAD_TAG = AVPK_META,
	AVIREAD_ERROR = AVPK_ERROR,
};

enum AVI_E {
	AVI_EOK,
	AVI_ESMALL,
	AVI_ELARGE,
	AVI_EMEM,
};

static inline const char* aviread_error(aviread *a)
{
	static const char *const errors[] = {
		"",
		"too small chunk", // AVI_ESMALL
		"too large chunk", // AVI_ELARGE
		"not enough memory", // AVI_EMEM
	};
	return errors[a->err];
}

#define _AVIR_ERR(a, e) \
	(a)->err = (e), AVIREAD_ERROR

static inline void _aviread_log(aviread *a, const char *fmt, ...)
{
	if (a->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	a->log(a->udata, fmt, va);
	va_end(va);
}

static void _aviread_chunkinfo(aviread *a, const void *data, const struct avi_binchunk *ctx, struct aviread_chunk *chunk, ffuint64 off)
{
	const struct avi_chunk *ch = data;
	int i;
	if (-1 != (i = avi_chunk_find(ctx, ch->id))) {
		chunk->id = ctx[i].flags & AVI_MASK_CHUNKID;
		chunk->flags = ctx[i].flags & ~AVI_MASK_CHUNKID;
		chunk->ctx = ctx[i].ctx;
	} else {
		chunk->id = AVI_T_UKN;
	}

	chunk->size = ffint_le_cpu32_ptr(ch->size);
	chunk->flags |= (chunk->size % 2) ? AVI_F_PADD : 0;

	_aviread_log(a, "\"%4s\"  size:%u  offset:%xU"
		, ch->id, chunk->size, off);
}

static int _aviread_chunk(aviread *a)
{
	int r;
	struct aviread_chunk *chunk = &a->chunks[a->ictx];

	if (chunk->flags & AVI_F_LIST) {
		_aviread_log(a, "LIST \"%4s\"", a->gbuf.ptr);
		r = avi_chunk_find(chunk->ctx, a->gbuf.ptr);
		if (r != -1) {
			const struct avi_binchunk *bch = &chunk->ctx[r];
			chunk->id = bch->flags & AVI_MASK_CHUNKID;
			chunk->flags = bch->flags & ~AVI_MASK_CHUNKID;
			chunk->ctx = bch->ctx;
		} else
			chunk->ctx = NULL;
	}

	switch (chunk->id) {
	case AVI_T_STRH:
		a->curtrack = ffvec_pushT(&a->tracks, struct avi_audio_info);
		ffmem_zero_obj(a->curtrack);
		if (1 != avi_strh_read(a->curtrack, a->gbuf.ptr)) {
			// skip strl
			struct aviread_chunk *parent = &a->chunks[a->ictx - 1];
			parent->size += chunk->size;
			ffmem_zero_obj(chunk);
			a->ictx--;
		}
		break;

	case AVI_T_STRF:
		ffstr_free(&a->curtrack->codec_conf);
		if (0 != avi_strf_read(a->curtrack, a->gbuf.ptr, a->gbuf.len))
			break;

		if (a->curtrack->codec_conf.len != 0) {
			ffstr cc = a->curtrack->codec_conf;
			ffstr_null(&a->curtrack->codec_conf);
			if (NULL == ffstr_dupstr(&a->curtrack->codec_conf, &cc))
				return _AVIR_ERR(a, AVI_EMEM);
		}
		break;

	case AVI_T_INFO:
		break;

	case AVI_T_MOVI:
		a->movi_off = a->off;
		a->movi_size = chunk->size;
		return AVIREAD_HEADER;

	case AVI_T_MOVI_CHUNK: {
		const struct avi_chunk *ch = (struct avi_chunk*)a->gbuf.ptr;
		ffuint idx;
		if (2 != ffs_toint(ch->id, 2, &idx, FFS_INT32)
			|| (int)idx != a->curtrack - (struct avi_audio_info*)a->tracks.ptr
			|| !!ffmem_cmp(ch->id + 2, AVI_MOVI_AUDIO, 2)) {
			break;
		}

		if (chunk->size == 0) {
			return AVIREAD_MORE;
		}

		struct avi_audio_info *ai = a->curtrack;
		if (ai->codec == AVPKC_PCM) {
			ai->blocksize = chunk->size / (ai->bits/8 * ai->channels);
		}

		a->gather_size = chunk->size;
		chunk->size = 0;
		return AVIREAD_DATA;
	}

	default:
		if (chunk->id >= _AVI_T_TAG) {
			a->tag = chunk->id - _AVI_T_TAG;
			ffstr_setz(&a->tagval, a->gbuf.ptr);
			return AVIREAD_TAG;
		}
	}

	return AVIREAD_MORE;
}

/**
Return enum AVIREAD_R */
/* AVI reading algorithm:
. Gather chunk header (ID + size)
. Search chunk ID in the current context; skip chunk if unknown
. If it's a LIST chunk, gather its sub-ID; repeat the previous step
. Process chunk
*/
static inline int aviread_process(aviread *a, ffstr *input, ffstr *output)
{
	enum {
		R_GATHER_CHUNKHDR, R_CHUNK_HDR, R_CHUNK,
		R_NEXTCHUNK, R_SKIP, R_PADDING,
		R_DATA,
		R_GATHER,
	};
	int r;
	struct aviread_chunk *chunk, *parent;

	for (;;) {
		switch (a->state) {

		case R_SKIP:
			chunk = &a->chunks[a->ictx];
			r = ffmin(chunk->size, input->len);
			ffstr_shift(input, r);
			chunk->size -= r;
			a->off += r;
			if (chunk->size != 0)
				return AVIREAD_MORE;

			a->state = R_NEXTCHUNK;
			continue;

		case R_PADDING:
			if (input->len == 0)
				return AVIREAD_MORE;

			if (input->ptr[0] == '\0') {
				// skip padding byte
				ffstr_shift(input, 1);
				a->off += 1;
				parent = &a->chunks[a->ictx - 1];
				if (parent->size != 0)
					parent->size -= 1;
			}

			a->state = R_NEXTCHUNK;
			// fallthrough

		case R_NEXTCHUNK:
			chunk = &a->chunks[a->ictx];

			if (chunk->size == 0) {
				if (chunk->flags & AVI_F_PADD) {
					chunk->flags &= ~AVI_F_PADD;
					a->state = R_PADDING;
					continue;
				}

				ffuint id = chunk->id;
				ffmem_zero_obj(chunk);
				a->ictx--;

				switch (id) {
				case AVI_T_AVI:
					return AVIREAD_DONE;
				}

				continue;
			}

			FF_ASSERT(chunk->ctx != NULL);
			// fallthrough

		case R_GATHER_CHUNKHDR:
			a->gather_size = sizeof(struct avi_chunk);
			a->state = R_GATHER;  a->nxstate = R_CHUNK_HDR;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&a->buf, &a->buf.cap, input->ptr, input->len, a->gather_size, &a->gbuf);
			if (r < 0)
				return _AVIR_ERR(a, AVI_EMEM);
			ffstr_shift(input, r);
			a->off += r;
			if (a->gbuf.len == 0)
				return AVIREAD_MORE;
			a->buf.len = 0;
			a->state = a->nxstate;
			continue;

		case R_CHUNK_HDR:
			parent = &a->chunks[a->ictx];
			a->ictx++;
			chunk = &a->chunks[a->ictx];
			_aviread_chunkinfo(a, a->gbuf.ptr, parent->ctx, chunk, a->off - sizeof(struct avi_chunk));

			if (sizeof(struct avi_chunk) + chunk->size > parent->size)
				return _AVIR_ERR(a, AVI_ELARGE);
			parent->size -= sizeof(struct avi_chunk) + chunk->size;

			if (chunk->id == AVI_T_UKN) {
				a->state = R_SKIP;
				continue;
			}

			ffuint minsize = AVI_GET_MINSIZE(chunk->flags);
			if (chunk->size < minsize)
				return _AVIR_ERR(a, AVI_ESMALL);
			if (chunk->flags & AVI_F_WHOLE)
				minsize = chunk->size;
			if (minsize != 0) {
				a->gather_size = minsize;
				chunk->size -= a->gather_size;
				a->state = R_GATHER;  a->nxstate = R_CHUNK;
				continue;
			}

			a->state = R_CHUNK;
			continue;

		case R_CHUNK:
			r = _aviread_chunk(a);
			switch (r) {
			case AVIREAD_DATA:
				a->state = R_GATHER;  a->nxstate = R_DATA;
				continue;

			case AVIREAD_TAG:
				a->state = R_SKIP;
				return AVIREAD_TAG;

			case AVIREAD_HEADER:
				a->state = R_NEXTCHUNK;
				return AVIREAD_HEADER;

			case AVIREAD_ERROR:
				return AVIREAD_ERROR;

			case AVIREAD_MORE:
				break;
			}

			chunk = &a->chunks[a->ictx];
			if (chunk->ctx != NULL) {
				a->state = R_NEXTCHUNK;
				continue;
			}

			a->state = R_SKIP;
			continue;

		case R_DATA:
			ffstr_set(output, a->gbuf.ptr, a->gbuf.len);
			a->state = R_NEXTCHUNK;
			a->nsamples += a->curtrack->blocksize;
			return AVIREAD_DATA;
		}
	}
}

static inline int aviread_process2(aviread *a, ffstr *input, union avpk_read_result *res)
{
	if (a->codec_conf_pending) {
		a->codec_conf_pending = 0;
		*(ffstr*)&res->frame = a->curtrack->codec_conf;
		res->frame.pos = ~0ULL;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		return AVPK_DATA;
	}

	int r = aviread_process(a, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_HEADER: {
		const struct avi_audio_info *ai;
		for (unsigned i = 0;;  i++) {
			if (!(ai = aviread_track_info(a, i))) {
				res->error.message = "No audio track";
				res->error.offset = ~0ULL;
				return AVPK_ERROR;
			}
			if (ai->type == 1) {
				aviread_track_activate(a, i);
				break;
			}
		}
		res->hdr.codec = ai->codec;
		res->hdr.sample_bits = ai->bits;
		res->hdr.sample_rate = ai->sample_rate;
		res->hdr.channels = ai->channels;
		res->hdr.duration = ai->duration_msec * ai->sample_rate / 1000;
		a->codec_conf_pending = 1;
		break;
	}

	case AVPK_META:
		res->tag.id = a->tag;
		ffstr_null(&res->tag.name);
		res->tag.value = a->tagval;
		break;

	case AVPK_DATA:
		res->frame.pos = a->nsamples - a->curtrack->blocksize;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = a->curtrack->blocksize;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = aviread_error(a);
		res->error.offset = ~0ULL;
		break;
	}
	return r;
}

#undef _AVIR_ERR

AVPKR_IF_INIT(avpk_avi, "avi", AVPKF_AVI, aviread, aviread_open2, aviread_process2, NULL, aviread_close);
