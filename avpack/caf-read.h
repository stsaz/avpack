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
#include <avpack/decl.h>
#include <avpack/base/caf.h>
#include <avpack/vorbistag.h>
#include <ffbase/stream.h>
#include <ffbase/vector.h>

typedef void (*caf_log_t)(void *udata, const char *fmt, va_list va);

typedef struct cafread {
	ffuint state, nxstate;
	ffsize gathlen;
	ffstr chunk;
	ffstream stream;
	const char *error;

	ffuint nframes; // N of current frames
	ffuint64 inoff; // input offset
	ffuint64 chunk_size; // current chunk size
	ffuint64 ipkt; // current packet
	ffuint64 iframe; // current frame
	ffuint64 pakt_off; // current offset in 'pakt'
	ffstr pakt; // packets sizes
	unsigned codec_conf_pending :1;

	ffstr tagname;
	ffstr tagval;

	caf_info info;

	caf_log_t log;
	void *udata;
} cafread;

enum CAFREAD_R {
	CAFREAD_ERROR = AVPK_ERROR,
	CAFREAD_MORE = AVPK_MORE,
	CAFREAD_SEEK = AVPK_SEEK,
	CAFREAD_HEADER = AVPK_HEADER,
	CAFREAD_TAG = AVPK_META,
	CAFREAD_DATA = AVPK_DATA,
	CAFREAD_DONE = AVPK_FIN,
	CAFREAD_MORE_OR_DONE = AVPK_MORE, // can process more data if there's any
};

#define cafread_offset(c)  (c)->inoff

static inline ffuint64 cafread_cursample(cafread *c)
{
	return c->iframe - c->nframes;
}

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
	ffstream_realloc(&c->stream, 4*1024);
	return 0;
}

static inline void cafread_open2(cafread *c, struct avpk_reader_conf *conf)
{
	cafread_open(c);
	c->log = conf->log;
	c->udata = conf->opaque;
}

static inline void cafread_close(cafread *c)
{
	ffstream_free(&c->stream);
	ffstr_free(&c->info.codec_conf);
	ffstr_free(&c->pakt);
}

static inline void _cafread_log(cafread *c, const char *fmt, ...)
{
	if (c->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	c->log(c->udata, fmt, va);
	va_end(va);
}

#define _CAFR_ERR(c, e) \
	(c)->error = e,  CAFREAD_ERROR

static inline const char* cafread_error(cafread *c)
{
	return c->error;
}

static int _cafr_chunk_find(cafread *c, const struct caf_chunk *cc, const struct caf_binchunk **pchunk)
{
	ffuint64 sz = ffint_be_cpu64_ptr(cc->size);

	for (ffuint i = 0;  i != FF_COUNT(caf_chunks);  i++) {
		const struct caf_binchunk *ch = &caf_chunks[i];

		if (!!ffmem_cmp(cc->type, ch->type, 4))
			continue;

		if (sz < ch->minsize)
			return _CAFR_ERR(c, "chunk too small");

		if ((ch->flags & CAF_FWHOLE) && sz > CAFREAD_CHUNK_MAXSIZE)
			return _CAFR_ERR(c, "chunk too large");

		if ((ch->flags & CAF_FEXACT) && sz != ch->minsize)
			return _CAFR_ERR(c, "bad chunk size");

		*pchunk = ch;
		return CAFREAD_DONE;
	}
	return CAFREAD_DONE;
}

static void _cafr_gather(cafread *c, ffuint nxstate, ffsize len)
{
	c->state = 1 /*R_GATHER*/;  c->nxstate = nxstate;
	c->gathlen = len;
}

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
			if (0 != ffstream_realloc(&c->stream, c->gathlen))
				return _CAFR_ERR(c, "not enough memory");
			r = ffstream_gather(&c->stream, *input, c->gathlen, &c->chunk);
			ffstr_shift(input, r);
			c->inoff += r;
			if (c->chunk.len < c->gathlen)
				return CAFREAD_MORE;
			ffstream_consume(&c->stream, c->gathlen);
			c->state = c->nxstate;
			break;

		case R_HDR:
			if (0 != caf_hdr_read(c->chunk.ptr))
				return _CAFR_ERR(c, "bad header");
			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;

		case R_CHUNK_HDR: {
			const struct caf_chunk *cc = (struct caf_chunk*)c->chunk.ptr;
			c->chunk_size = ffint_be_cpu64_ptr(cc->size);
			ffuint64 chunk_off = c->inoff - ffstream_used(&c->stream) - sizeof(struct caf_chunk);
			_cafread_log(c, "type:%*s  size:%D  off:%xU"
				, (ffsize)4, cc->type, c->chunk_size, chunk_off);

			const struct caf_binchunk *ch = NULL;
			if (CAFREAD_DONE != (r = _cafr_chunk_find(c, cc, &ch)))
				return r;

			if (ch != NULL) {
				int t = R_CHUNK + CAF_CHUNK_TYPE(ch->flags);
				if (ch->flags & CAF_FWHOLE)
					_cafr_gather(c, t, c->chunk_size);
				else
					c->state = t;

			} else {
				_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
				ffstream_reset(&c->stream);
				c->inoff = chunk_off + sizeof(struct caf_chunk) + c->chunk_size;
				return CAFREAD_SEEK;
			}
			break;
		}

		case R_CHUNK + CAF_T_DESC:
			caf_desc_read(&c->info, c->chunk.ptr);
			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;

		case R_CHUNK + CAF_T_INFO:
			c->chunk.len = c->chunk_size;
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
			c->chunk.len = c->chunk_size;
			ffstr_free(&c->info.codec_conf);
			switch (c->info.codec) {
			case AVPKC_AAC: {
				struct caf_mp4_acodec ac;
				if (0 != caf_mp4_esds_read(c->chunk.ptr, c->chunk.len, &ac))
					return _CAFR_ERR(c, "bad esds data");
				c->info.bitrate = ac.avg_brate;
				ffstr_dup(&c->info.codec_conf, ac.conf, ac.conflen);
				break;
			}
			case AVPKC_ALAC: {
				ffstr conf;
				if (0 != kuki_alac_read(c->chunk, &conf))
					return _CAFR_ERR(c, "bad ALAC config");
				ffstr_dupstr(&c->info.codec_conf, &conf);
				break;
			}
			}
			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;
		}

		case R_CHUNK + CAF_T_PAKT:
			c->chunk.len = c->chunk_size;
			r = caf_pakt_read(&c->info, c->chunk.ptr);
			ffstr_shift(&c->chunk, r);
			ffstr_free(&c->pakt);
			ffstr_dup2(&c->pakt, &c->chunk);

			if (c->info.sample_rate == 0)
				return _CAFR_ERR(c, "bad chunks order");

			_cafr_gather(c, R_CHUNK_HDR, sizeof(struct caf_chunk));
			break;

		case R_CHUNK + CAF_T_DATA:
			_cafr_gather(c, R_DATA_NEXT, 4); // skip "edit count" field
			c->chunk_size -= 4;
			return CAFREAD_HEADER;

		case R_DATA_NEXT: {
			if (c->chunk_size == 0)
				return CAFREAD_DONE;
			if (input->len == 0 && (ffint64)c->chunk_size == -1)
				return CAFREAD_MORE_OR_DONE;

			ffuint64 pkt_off = c->inoff - ffstream_used(&c->stream);
			ffuint sz = c->info.packet_bytes;
			c->nframes = c->info.packet_frames;
			if (sz == 0) {
				r = caf_varint(c->pakt.ptr + c->pakt_off, c->pakt.len - c->pakt_off, &sz);
				if (r == 0)
					return CAFREAD_DONE;
				if (r < 0)
					return _CAFR_ERR(c, "bad audio chunk");
				c->pakt_off += r;
			} else if (c->info.codec == AVPKC_PCM) {
				ffuint n = ffmin(ffstream_used(&c->stream), c->chunk_size) / sz; // take as many samples as we can
				if (n != 0) {
					c->nframes = n;
					sz = c->nframes * sz;
				}
			}

			_cafread_log(c, "pkt#%U  size:%u  off:%U"
				, c->ipkt, sz, pkt_off);

			if (sz > c->chunk_size)
				return _CAFR_ERR(c, "bad audio chunk");
			if (sz > CAFREAD_ACHUNK_MAXSIZE)
				return _CAFR_ERR(c, "chunk too large");

			c->ipkt++;
			if ((ffint64)c->chunk_size != -1)
				c->chunk_size -= sz;
			_cafr_gather(c, R_DATA_CHUNK, sz);
			break;
		}

		case R_DATA_CHUNK:
			ffstr_set(output, c->chunk.ptr, c->gathlen);
			c->iframe += c->nframes;
			c->state = R_DATA_NEXT;
			return CAFREAD_DATA;

		}
	}
}

static inline int cafread_process2(cafread *c, ffstr *input, union avpk_read_result *res)
{
	if (c->codec_conf_pending) {
		c->codec_conf_pending = 0;
		*(ffstr*)&res->frame = c->info.codec_conf;
		res->frame.pos = ~0ULL;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		return AVPK_DATA;
	}

	int r = cafread_process(c, input, (ffstr*)&res->frame), n;
	switch (r) {
	case AVPK_HEADER:
		if (!(c->info.format & CAF_FMT_LE)) {
			res->error.message = "big-endian data isn't supported";
			res->error.offset = ~0ULL;
			return AVPK_ERROR;
		}
		res->hdr.codec = c->info.codec;
		res->hdr.sample_bits = c->info.format & 0xff;
		res->hdr.sample_float = !!(c->info.format & CAF_FMT_FLOAT);
		res->hdr.sample_rate = c->info.sample_rate;
		res->hdr.channels = c->info.channels;
		res->hdr.duration = c->info.total_frames;
		c->codec_conf_pending = 1;
		break;

	case AVPK_META:
		res->tag.name = c->tagname;
		res->tag.value = c->tagval;
		n = ffszarr_ifindsorted(_vorbistag_str, FF_COUNT(_vorbistag_str), res->tag.name.ptr, res->tag.name.len);
		res->tag.id = (n >= 0) ? _vorbistag_mmtag[n] : 0;
		break;

	case AVPK_DATA:
		res->frame.pos = c->iframe - c->nframes;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		break;

	case AVPK_SEEK:
		res->seek_offset = c->inoff;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = c->error;
		res->error.offset = c->inoff;
		break;
	}
	return r;
}

#undef _CAFR_ERR

AVPKR_IF_INIT(avpk_caf, "caf", AVPKF_CAF, cafread, cafread_open2, cafread_process2, NULL, cafread_close);
