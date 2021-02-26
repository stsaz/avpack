/** avpack: .caf reader
* no seeking

2020,2021, Simon Zolin
*/

/*
cafread_open
cafread_close
cafread_process
cafread_info
cafread_error
cafread_asc
cafread_offset
cafread_cursample
cafread_fin
cafread_tag
*/

#pragma once

#include <avpack/caf-fmt.h>
#include <ffbase/vector.h>

typedef void (*caf_log_t)(void *udata, ffstr msg);

typedef struct cafread {
	ffuint state;
	int err;

	ffuint nxstate;
	ffsize gathlen;
	ffstr chunk;
	ffvec buf;

	ffuint64 inoff; // input offset
	ffuint64 chunk_size; // current chunk size
	ffuint64 ipkt; // current packet
	ffuint64 iframe; // current frame
	ffuint64 pakt_off; // current offset in 'pakt'
	ffstr pakt; // packets sizes

	ffstr tagname;
	ffstr tagval;

	caf_info info;

	caf_log_t log;
	void *udata;
} cafread;


#define cafread_offset(c)  (c)->inoff
#define cafread_cursample(c)  (c)->iframe

static inline const caf_info* cafread_info(cafread *c)
{
	return &c->info;
}

/** Get tag info */
static inline ffstr cafread_tag(cafread *c, ffstr *val)
{
	*val = c->tagval;
	return c->tagname;
}

#define CAFREAD_CHUNK_MAXSIZE  (2*1024*1024) // max. meta chunk size
#define CAFREAD_ACHUNK_MAXSIZE  (1*1024*1024) // max. audio chunk size

static inline int cafread_open(cafread *c)
{
	ffvec_alloc(&c->buf, 1024, 1);
	return 0;
}

static inline void cafread_close(cafread *c)
{
	ffvec_free(&c->buf);
	ffstr_free(&c->info.codec_conf);
	ffstr_free(&c->pakt);
}

static inline void _cafread_log(cafread *c, const char *fmt, ...)
{
	if (c->log == NULL)
		return;

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	c->log(c->udata, s);
	ffstr_free(&s);
}

#define _CAFR_ERR(c, e) \
	(c)->err = e,  CAFREAD_ERROR

enum CAFREAD_E {
	_CAFR_E_OK,
	_CAFR_E_MEM,
	_CAFR_E_HDR,
	_CAFR_E_CHUNKSIZE,
	_CAFR_E_CHUNKSMALL,
	_CAFR_E_CHUNKLARGE,
	_CAFR_E_ORDER,
	_CAFR_E_ESDS,
	_CAFR_E_DATA,
};

static inline const char* cafread_error(cafread *c)
{
	static const char* const errors[] = {
		"",
		"not enough memory", // _CAFR_E_MEM
		"bad header", // _CAFR_E_HDR
		"bad chunk size", // _CAFR_E_CHUNKSIZE
		"chunk too small", // _CAFR_E_CHUNKSMALL
		"chunk too large", // _CAFR_E_CHUNKLARGE
		"bad chunks order", // _CAFR_E_ORDER
		"bad esds data", // _CAFR_E_ESDS
		"bad audio chunk", // _CAFR_E_DATA
	};

	ffuint e = c->err;
	if (e > FF_COUNT(errors))
		return "";
	return errors[e];
}

static int caf_chunk_find(const struct caf_chunk *cc, const struct caf_binchunk **pchunk)
{
	ffuint64 sz = ffint_be_cpu64_ptr(cc->size);

	for (ffuint i = 0;  i != FF_COUNT(caf_chunks);  i++) {
		const struct caf_binchunk *ch = &caf_chunks[i];

		if (!!ffmem_cmp(cc->type, ch->type, 4))
			continue;

		if (sz < ch->minsize)
			return _CAFR_E_CHUNKSMALL;

		if ((ch->flags & CAF_FWHOLE) && sz > CAFREAD_CHUNK_MAXSIZE)
			return _CAFR_E_CHUNKLARGE;

		if ((ch->flags & CAF_FEXACT) && sz != ch->minsize)
			return _CAFR_E_CHUNKSIZE;

		*pchunk = ch;
		return 0;
	}
	return 0;
}

static void _cafr_gather(cafread *c, ffuint nxstate, ffsize len)
{
	c->state = 1 /*R_GATHER*/;  c->nxstate = nxstate;
	c->gathlen = len;
}

enum CAFREAD_R {
	CAFREAD_ERROR,
	CAFREAD_MORE,
	CAFREAD_MORE_OR_DONE, // can process more data if there's any
	CAFREAD_SEEK,
	CAFREAD_HEADER,
	CAFREAD_TAG,
	CAFREAD_DATA,
	CAFREAD_DONE,
};

/**
Return enum CAFREAD_R */
static inline int cafread_process(cafread *c, ffstr *input, ffstr *output)
{
	enum R {
		R_INIT,
		R_GATHER=1, R_CHUNK_HDR,
		R_DATA, R_DATA_NEXT, R_DATA_CHUNK,
		R_HDR, R_TAG, R_CHUNK, // CAF_T...
	};
	int r;

	for (;;) {
		switch (c->state) {

		case R_INIT:
			_cafr_gather(c, R_HDR, sizeof(struct caf_hdr));
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&c->buf, &c->buf.cap, input->ptr, input->len, c->gathlen, &c->chunk);
			if (r < 0)
				return _CAFR_ERR(c, _CAFR_E_MEM);
			ffstr_shift(input, r);
			c->inoff += r;
			if (c->chunk.len == 0)
				return CAFREAD_MORE;
			c->buf.len = 0;
			c->state = c->nxstate;
			break;

		case R_HDR:
			if (0 != caf_hdr_read(c->chunk.ptr))
				return _CAFR_ERR(c, _CAFR_E_HDR);
			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;

		case R_CHUNK_HDR: {
			const struct caf_chunk *cc = (struct caf_chunk*)c->chunk.ptr;
			c->chunk_size = ffint_be_cpu64_ptr(cc->size);
			_cafread_log(c, "type:%*s  size:%D  off:%U"
				, (ffsize)4, cc->type, c->chunk_size, c->inoff);

			const struct caf_binchunk *ch = NULL;
			r = caf_chunk_find(cc, &ch);
			if (r != 0)
				return _CAFR_ERR(c, r);

			if (ch != NULL) {
				int t = R_CHUNK + CAF_CHUNK_TYPE(ch->flags);
				if (ch->flags & CAF_FWHOLE)
					_cafr_gather(c, t, c->chunk_size);
				else
					c->state = t;

			} else {
				_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
				c->inoff += c->chunk_size;
				return CAFREAD_SEEK;
			}
			break;
		}

		case R_CHUNK + CAF_T_DESC:
			caf_desc_read(&c->info, c->chunk.ptr);
			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;

		case R_CHUNK + CAF_T_INFO:
			// const struct caf_info *i = (void*)c->chunk.ptr;
			ffstr_shift(&c->chunk, sizeof(struct _caf_info));
			c->state = R_TAG;
			// fallthrough

		case R_TAG:
			if (c->chunk.len == 0) {
				_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
				break;
			}

			ffstr_splitby(&c->chunk, '\0', &c->tagname, &c->chunk);
			ffstr_splitby(&c->chunk, '\0', &c->tagval, &c->chunk);
			return CAFREAD_TAG;

		case R_CHUNK + CAF_T_KUKI: {
			struct mp4_acodec ac;
			if (0 != mp4_esds_read(c->chunk.ptr, c->chunk.len, &ac))
				return _CAFR_ERR(c, _CAFR_E_ESDS);

			c->info.bitrate = ac.avg_brate;
			ffstr_free(&c->info.codec_conf);
			ffstr_dup(&c->info.codec_conf, ac.conf, ac.conflen);
			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;
		}

		case R_CHUNK + CAF_T_PAKT:
			r = caf_pakt_read(&c->info, c->chunk.ptr);
			ffstr_shift(&c->chunk, r);
			ffstr_free(&c->pakt);
			ffstr_dup2(&c->pakt, &c->chunk);

			if (c->info.sample_rate == 0)
				return _CAFR_ERR(c, _CAFR_E_ORDER);

			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;

		case R_CHUNK + CAF_T_DATA:
			_cafr_gather(c, R_DATA_NEXT, 4); // skip "edit count" field
			return CAFREAD_HEADER;

		case R_DATA_NEXT: {
			if (c->chunk_size == 0)
				return CAFREAD_DONE;
			if (input->len == 0 && (ffint64)c->chunk_size == -1)
				return CAFREAD_MORE_OR_DONE;

			ffuint sz = c->info.packet_bytes;
			if (sz == 0) {
				r = caf_varint(c->pakt.ptr + c->pakt_off, c->pakt.len - c->pakt_off, &sz);
				if (r == 0)
					return CAFREAD_DONE;
				if (r < 0)
					return _CAFR_ERR(c, _CAFR_E_DATA);
				c->pakt_off += r;
			}

			_cafread_log(c, "pkt#%U  size:%u  off:%U"
				, c->ipkt, sz, c->inoff);

			if (sz > c->chunk_size)
				return _CAFR_ERR(c, _CAFR_E_DATA);
			if (sz > CAFREAD_ACHUNK_MAXSIZE)
				return _CAFR_ERR(c, _CAFR_E_CHUNKLARGE);

			c->ipkt++;
			if ((ffint64)c->chunk_size != -1)
				c->chunk_size -= sz;
			_cafr_gather(c, R_DATA_CHUNK, sz);
			break;
		}

		case R_DATA_CHUNK:
			*output = c->chunk;
			c->iframe += c->info.packet_frames;
			c->state = R_DATA_NEXT;
			return CAFREAD_DATA;

		}
	}
}

#undef _CAFR_ERR
