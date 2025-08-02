/** avpack: musepack (.mpc) reader
* incomplete seeking algorithm
2017,2021, Simon Zolin
*/

/*
mpcread_open mpcread_close
mpcread_process
mpcread_seek
mpcread_info
mpcread_offset
mpcread_tag
mpcread_error
mpcread_cursample
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/mpc.h>
#include <avpack/apetag.h>
#include <ffbase/vector.h>
#include <avpack/shared.h>

enum MPCREAD_O {
	MPCREAD_O_NOTAGS = 1,
};

typedef void (*mpc_log_t)(void *udata, const char *fmt, va_list va);

typedef struct mpcread {
	ffuint state;
	const char *error;
	ffuint nextstate;
	ffuint gather_size, hdr_ready;
	ffvec buf;
	ffstr chunk;
	struct mpcread_info info;
	ffuint options; // enum MPCREAD_O

	ffuint64 dataoff;
	ffuint64 total_size;
	ffuint64 off, blk_off;
	ffuint blk_dataoff;
	ffuint64 blk_size;
	ffuint64 blk_apos;

	ffuint64 ST_off;
	ffuint64 seek_sample;

	char sh_block[4+1+8+8+2];
	ffuint sh_block_len;
	ffuint enc_profile;
	ffbyte enc_ver[3];

	struct apetagread apetag;
	ffstr tagname, tagval;
	int tag;

	mpc_log_t log;
	void *udata;

	ffuint hdrok :1;
} mpcread;

enum MPCREAD_R {
	/** Use mpcread_info() to get header info.  Output contains SH block data. */
	MPCREAD_HEADER = AVPK_HEADER,

	/** New tag is read: use mpcread_tag() */
	MPCREAD_TAG = AVPK_META,

	/** Need more input data */
	MPCREAD_MORE = AVPK_MORE,

	/** Output contains AP block data */
	MPCREAD_DATA = AVPK_DATA,

	/** Need more input data at offset mpcread_offset() */
	MPCREAD_SEEK = AVPK_SEEK,

	MPCREAD_DONE = AVPK_FIN,
	MPCREAD_ERROR = AVPK_ERROR,
	MPCREAD_WARN = AVPK_WARNING,
};

#define _MPCR_ERR(m, msg) \
	(m)->error = (msg),  MPCREAD_ERROR

#define _MPCR_WARN(m, msg) \
	(m)->error = (msg),  MPCREAD_WARN

static inline const char* mpcread_error(mpcread *m)
{
	return m->error;
}

/**
total_size: optional */
static inline void mpcread_open(mpcread *m, ffuint64 total_size)
{
	ffvec_alloc(&m->buf, 4096, 1);
	m->total_size = total_size;
	m->seek_sample = (ffuint64)-1;
}

static inline void mpcread_open2(mpcread *m, struct avpk_reader_conf *conf)
{
	mpcread_open(m, !(conf->flags & AVPKR_F_NO_SEEK) ? conf->total_size : 0);
	m->log = conf->log;
	m->udata = conf->opaque;
}

static inline void mpcread_close(mpcread *m)
{
	ffvec_free(&m->buf);
	apetagread_close(&m->apetag);
}

static int _mpcr_block_id(const char *name)
{
	static const char block_ids[][2] = {
		"AP",
		"EI",
		"SE",
		"SH",
		"SO",
		"ST",
	};
	ffssize r = ffcharr_findsorted(block_ids, FF_COUNT(block_ids), 2, name, 2);
	if (r < 0)
		return 0;
	return 0x100 | r;
}

/* EI block:
byte profile :7
byte pns :1
byte ver[3]
*/
static int _mpcr_ei_read(mpcread *m, ffstr body)
{
	if (body.len != 4)
		return -1;
	const ffbyte *d = (ffbyte*)body.ptr;
	m->enc_profile = (d[0] >> 1) & 0x7f;
	ffmem_copy(m->enc_ver, d + 1, 3);
	return 0;
}

/** Find AP block */
static int _mpcr_ap_find(mpcread *m, ffstr *input, ffstr *output)
{
	int r, pos;
	ffstr chunk = {};

	for (;;) {

		r = _avpack_gather_header(&m->buf, *input, 2, &chunk);
		ffstr_shift(input, r);
		m->off += r;
		if (chunk.len == 0) {
			ffstr_null(output);
			return 0xfeed;
		}

		pos = ffstr_find(&chunk, "AP", 2);
		if (pos >= 0)
			break;

		r = _avpack_gather_trailer(&m->buf, *input, 2, r);
		// r<0: ffstr_shift() isn't suitable due to assert()
		input->ptr += r,  input->len -= r;
		m->off += r;
	}

	if (chunk.ptr == input->ptr) {
		ffstr_shift(input, 2 + pos);
		m->off += 2 + pos;
	}
	ffstr_set(output, &chunk.ptr[pos], 2);
	if (m->buf.len != 0) {
		ffstr_erase_left((ffstr*)&m->buf, pos);
		ffstr_set(output, m->buf.ptr, 2);
	}

	return 0;
}

static inline void _mpcr_log(mpcread *m, const char *fmt, ...)
{
	if (m->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	m->log(m->udata, fmt, va);
	va_end(va);
}

/**
Return enum MPCREAD_R */
/* .mpc reader:
. Check Musepack ID
. Gather block header (id & size)
. Gather and process or skip block body until the first AP block is met
. Store ST block offset from SO block
. Return SH block body (MPCREAD_HEADER)
. Seek to ST block (MPCREAD_SEEK); parse it
. Seek to the end and parse APE tag (MPCREAD_SEEK, MPCREAD_TAG)
. Seek to audio data (MPCREAD_SEEK)
. Gather and return AP blocks until SE block is met (MPCREAD_DATA)
*/
static inline int mpcread_process(mpcread *m, ffstr *input, ffstr *output)
{
	enum {
		R_START, R_GATHER, R_GATHER_MORE,
		R_HDR, R_NXTBLOCK, R_BLOCK_HDR, R_BLOCK_SKIP,
		R_AP_FIND,
		R_APETAG_SEEK, R_APETAG_FTR, R_APETAG, R_TAG_DONE,

		// keep in sync with block_ids[]
		R_AP = 0x100,
		R_EI,
		R_SE,
		R_SH,
		R_SO,
		R_ST,
	};
	const unsigned BLKHDR_MINSIZE = 2+1,  BLKHDR_MAXSIZE = 2+8,  TAGS_CHUNK_LEN = 1000,
		MAX_BLOCK = 1*1024*1024;
	int r;

	for (;;) {
		switch (m->state) {

		case R_START:
			m->state = R_GATHER,  m->nextstate = R_HDR,  m->gather_size = 4;
			continue;

		case R_GATHER_MORE:
			if (m->buf.len == 0) {
				if (m->chunk.len != ffvec_add2T(&m->buf, &m->chunk, char))
					return _MPCR_ERR(m, "not enough memory");
			}
			m->state = R_GATHER;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&m->buf, &m->buf.cap, input->ptr, input->len, m->gather_size, &m->chunk);
			if (r < 0)
				return _MPCR_ERR(m, "not enough memory");
			ffstr_shift(input, r);
			m->off += r;
			if (m->chunk.len == 0)
				return MPCREAD_MORE;
			m->state = m->nextstate;
			continue;

		case R_HDR:
			if (!ffstr_eqcz(&m->chunk, "MPCK"))
				return _MPCR_ERR(m, "bad header");
			m->buf.len = 0;
			m->state = R_NXTBLOCK;
			continue;

		case R_BLOCK_SKIP:
			if (m->buf.len != 0) {
				r = ffmin(m->buf.len, m->blk_size);
				ffstr_erase_left((ffstr*)&m->buf, r);
				m->blk_size -= r;
			} else if (m->chunk.len != 0) {
				r = ffmin(m->chunk.len, m->blk_size);
				ffstr_shift(&m->chunk, r);
				m->blk_size -= r;
			}

			if (input->len != 0) {
				r = ffmin(input->len, m->blk_size);
				ffstr_shift(input, r);
				m->off += r;
				m->blk_size -= r;
			}

			if (m->blk_size != 0)
				return MPCREAD_MORE;

			// fallthrough

		case R_NXTBLOCK:
			if (m->buf.len != 0)
				ffstr_erase_left((ffstr*)&m->buf, ffmin(m->buf.len, m->blk_size));
			m->state = R_GATHER,  m->nextstate = R_BLOCK_HDR,  m->gather_size = BLKHDR_MINSIZE;
			continue;

		case R_BLOCK_HDR:
			r = mpc_int_read(&m->chunk.ptr[2], m->chunk.len - 2, &m->blk_size);
			if (r < 0) {
				if (m->chunk.len == BLKHDR_MINSIZE) {
					m->state = R_GATHER_MORE,  m->nextstate = R_BLOCK_HDR,  m->gather_size = BLKHDR_MAXSIZE;
					continue;
				}

				if (m->hdrok) {
					m->buf.len = 0;
					m->state = R_AP_FIND;
					return _MPCR_WARN(m, "bad block header");
				}

				return _MPCR_ERR(m, "bad block header");
			}
			m->blk_off = m->off - m->chunk.len;
			m->blk_dataoff = 2 + r;

			_mpcr_log(m, "block:%2s  size:%U  offset:%xU"
				, m->chunk.ptr, m->blk_size, m->blk_off);

			if (m->blk_size < m->blk_dataoff) {
				if (!m->hdrok)
					return _MPCR_ERR(m, "too small block");
				m->buf.len = 0;
				m->state = R_AP_FIND;
				return _MPCR_WARN(m, "too small block");
			}

			if (m->blk_size > MAX_BLOCK) {
				if (!m->hdrok)
					return _MPCR_ERR(m, "too large block");
				m->buf.len = 0;
				m->state = R_AP_FIND;
				return _MPCR_WARN(m, "too large block");
			}

			r = _mpcr_block_id(m->chunk.ptr);

			if (m->buf.len != 0) {
				ffstr_erase_left((ffstr*)&m->buf, m->blk_dataoff);
				m->chunk.len = 0;
			} else {
				ffstr_shift(&m->chunk, m->blk_dataoff);
			}
			m->blk_size -= m->blk_dataoff;

			if (m->hdrok) {
				switch (r) {
				case R_AP:
					m->state = R_GATHER_MORE,  m->nextstate = r,  m->gather_size = m->blk_size;
					break;
				case R_SE:
					return MPCREAD_DONE;
				case R_ST:
					m->state = R_BLOCK_SKIP;
					break;
				default:
					m->state = R_AP_FIND;
				}
				continue;
			}

			switch (r) {

			case R_AP:
				if (m->info.sample_rate == 0)
					return _MPCR_ERR(m, "no header");

				m->dataoff = m->blk_off;
				m->hdrok = 1;
				m->state = R_APETAG_SEEK;

				ffstr_set(output, m->sh_block, m->sh_block_len);
				return MPCREAD_HEADER;

			case 0:
				m->state = R_BLOCK_SKIP;
				break;

			case R_SE:
				return _MPCR_ERR(m, "no header but SE block");

			default:
				m->state = R_GATHER_MORE,  m->nextstate = r,  m->gather_size = m->blk_size;
			}
			continue;

		case R_SH:
			if (mpc_sh_read(&m->info, m->chunk.ptr, m->chunk.len) <= 0)
				return _MPCR_ERR(m, "bad SH block");

			ffmem_copy(m->sh_block, m->chunk.ptr, m->chunk.len);
			m->sh_block_len = m->chunk.len;
			m->state = R_NXTBLOCK;
			continue;

		case R_EI:
			if (0 != _mpcr_ei_read(m, m->chunk))
				return _MPCR_ERR(m, "bad header");
			m->state = R_NXTBLOCK;
			continue;

		case R_SO:
			/* SO block:
			varint ST_block_offset
			*/
			if (0 > (r = mpc_int_read(m->chunk.ptr, m->chunk.len, &m->ST_off)))
				return _MPCR_ERR(m, "bad SO block");
			m->ST_off += m->blk_off;
			m->state = R_NXTBLOCK;
			continue;

		/*
		ST:
			// use libmpc to parse ST block
			mpc_seekctx *seekctx;
			mpc_seekinit(&seekctx, sh_block, sh_block_len, ST.ptr, ST.len)
			mpc_seek(seekctx, &blk)
			mpc_seekfree(seekctx)
		*/

		case R_AP:
			m->blk_apos += m->info.frame_samples;

			if (m->seek_sample != (ffuint64)-1) {

				if (m->total_size == 0
					|| m->seek_sample >= m->info.total_samples)
					return _MPCR_ERR(m, "can't seek");

				m->off = m->dataoff + m->seek_sample * m->total_size / m->info.total_samples;
				m->blk_apos = m->seek_sample - (m->seek_sample % m->info.frame_samples);
				m->seek_sample = (ffuint64)-1;
				m->buf.len = 0;
				m->state = R_AP_FIND;
				return MPCREAD_SEEK;
			}

			ffstr_setstr(output, &m->chunk);
			m->state = R_NXTBLOCK;
			return MPCREAD_DATA;

		case R_AP_FIND:
			r = _mpcr_ap_find(m, input, &m->chunk);
			if (r == 0xfeed)
				return MPCREAD_MORE;

			m->state = R_GATHER_MORE,  m->nextstate = R_BLOCK_HDR,  m->gather_size = BLKHDR_MINSIZE;
			continue;


		case R_APETAG_SEEK: {
			if ((m->options & MPCREAD_O_NOTAGS)
				|| m->total_size == 0) {
				m->state = R_TAG_DONE;
				break;
			}

			ffuint64 n = ffmin64(m->total_size, TAGS_CHUNK_LEN);
			m->off = m->total_size - n;
			m->state = R_GATHER,  m->nextstate = R_APETAG_FTR,  m->gather_size = n;
			m->buf.len = 0;
			return MPCREAD_SEEK;
		}

		case R_APETAG_FTR: {
			apetagread_open(&m->apetag);
			ffint64 seek;
			r = apetagread_footer(&m->apetag, m->chunk, &seek);
			switch (r) {
			case APETAGREAD_SEEK:
				m->off += seek;
				m->total_size = m->off;
				m->state = R_APETAG;
				return MPCREAD_SEEK;
			default:
				m->state = R_TAG_DONE;
				break;
			}
			break;
		}

		case R_APETAG: {
			ffsize len = input->len;
			r = apetagread_process(&m->apetag, input, &m->tagname, &m->tagval);
			m->off += len - input->len;

			switch (r) {
			case APETAGREAD_DONE:
				apetagread_close(&m->apetag);
				m->state = R_TAG_DONE;
				break;

			case APETAGREAD_MORE:
				return MPCREAD_MORE;

			case APETAGREAD_ERROR:
				m->state = R_TAG_DONE;
				return _MPCR_WARN(m, "bad APE tag");

			default:
				m->tag = -r;
				return MPCREAD_TAG;
			}
			break;
		}

		case R_TAG_DONE:
			m->buf.len = 0;
			m->off = m->dataoff;
			m->state = R_NXTBLOCK;
			return MPCREAD_SEEK;
		}
	}
}

static inline int mpcread_process2(mpcread *m, ffstr *input, union avpk_read_result *res)
{
	if (m->hdr_ready) {
		m->hdr_ready = 0;
		ffstr_set(&res->frame, m->sh_block, m->sh_block_len);
		res->frame.pos = ~0ULL;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		return AVPK_DATA;
	}

	int r = mpcread_process(m, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_HEADER:
		m->hdr_ready = 1;
		ffmem_zero_obj(&res->frame);
		res->hdr.codec = AVPKC_MPC;
		res->hdr.sample_rate = m->info.sample_rate;
		res->hdr.channels = m->info.channels;
		res->hdr.duration = m->info.total_samples;
		break;

	case AVPK_META:
		res->tag.id = m->tag;
		res->tag.name = m->tagname;
		res->tag.value = m->tagval;
		break;

	case AVPK_DATA:
		res->frame.pos = m->blk_apos - m->info.frame_samples;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = m->info.frame_samples;
		break;

	case AVPK_SEEK:
		res->seek_offset = m->off;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = m->error;
		res->error.offset = m->off;
		break;
	}
	return r;
}

static inline const struct mpcread_info* mpcread_info(mpcread *m)
{
	return &m->info;
}

static inline void mpcread_seek(mpcread *m, ffuint64 sample)
{
	m->seek_sample = sample;
}

/**
Return enum MMTAG */
static inline int mpcread_tag(mpcread *m, ffstr *name, ffstr *val)
{
	*name = m->tagname;
	*val = m->tagval;
	return m->tag;
}

#define mpcread_offset(m)  ((m)->off)

#define mpcread_cursample(m)  ((m)->blk_apos - (m)->info.frame_samples)

#undef _MPCR_ERR
#undef _MPCR_WARN

AVPKR_IF_INIT(avpk_mpc, "mpc", AVPKF_MPC, mpcread, mpcread_open2, mpcread_process2, mpcread_seek, mpcread_close);
