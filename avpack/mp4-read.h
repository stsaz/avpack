/** avpack: .mp4 reader
* no video (avc1 meta info only)
* audio codec: AAC, ALAC, MP3

2016,2021, Simon Zolin
*/

/*
mp4read_open
mp4read_close
mp4read_process
mp4read_seek
mp4read_track_info
mp4read_cursample
mp4read_error
mp4read_tag
mp4read_offset
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/mp4.h>
#include <ffbase/stream.h>
#include <ffbase/vector.h>


struct mp4_bbox;
struct mp4_box {
	char name[4];
	ffuint type; // enum BOX; flags
	ffuint usedboxes; // bit-table of children boxes that were processed
	ffuint prio;
	ffuint64 osize; // the whole box size
	ffuint64 size; // unprocessed box size
	const struct mp4_bbox *ctx; // non-NULL if the box may have children
};

typedef void (*mp4_log_t)(void *udata, const char *fmt, va_list va);

struct mp4read_audio_info {
	ffuint type; // 0:video  1:audio
	ffuint codec; // enum MP4_CODEC
	const char *codec_name;
	struct mp4_aformat format;
	ffuint64 total_samples;
	ffuint enc_delay;
	ffuint end_padding;
	ffuint frame_samples; // number of samples per frame (if constant). 0:variable
	ffuint aac_bitrate;
	ffuint real_bitrate;
	ffstr codec_conf;
};

struct mp4read_video_info {
	ffuint type; // 0:video  1:audio
	ffuint codec; // enum MP4_CODEC
	const char *codec_name;
	ffuint width, height;
};

struct mp4read_track {
	ffuint isamp; // current MP4-sample

	ffvec sktab; // struct mp4_seekpt[], MP4-sample table
	ffvec chunktab; // ffuint64[], offsets of audio chunks

	union {
		struct mp4read_audio_info audio;
		struct mp4read_video_info video;
	};
};

typedef struct mp4read {
	ffuint state, nextstate;
	ffuint gather_size;
	ffstream stream;
	ffstr chunk;

	int err; // enum MP4READ_E
	char errmsg[64];

	ffuint ictx;
	struct mp4_box boxes[10];

	ffuint64 off;
	ffuint64 total_size;
	ffuint frsize;
	ffstr stts;
	ffstr stsc;
	ffint64 seek_sample;
	ffuint64 cursample;

	struct mp4read_track *curtrack;
	ffvec tracks; // struct mp4read_track[]

	ffuint tag; // enum MMTAG
	ffstr tagval, tag_trktotal;
	char tagbuf[32];

	mp4_log_t log;
	void *udata;

	ffuint itunes_smpb :1
		, codec_conf_pending :1
		;
} mp4read;

static inline void _mp4read_log(mp4read *m, const char *fmt, ...)
{
	if (m->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	m->log(m->udata, fmt, va);
	va_end(va);
}

enum MP4READ_R {
	MP4READ_DATA = AVPK_DATA,

	/** Need input data at absolute file offset = mp4read_offset()
	Expecting mp4read_process() with more data at the specified offset */
	MP4READ_SEEK = AVPK_SEEK,

	MP4READ_MORE = AVPK_MORE,
	MP4READ_DONE = AVPK_FIN,
	MP4READ_WARN = AVPK_WARNING,
	MP4READ_ERROR = AVPK_ERROR,

	/** Header is processed
	User may call mp4read_track_info() to enumerate all tracks */
	MP4READ_HEADER = AVPK_HEADER,

	/** New tag has been read
	User may call mp4read_tag() */
	MP4READ_TAG = AVPK_META,
};

/** Get track meta info
Return struct mp4read_audio_info | struct mp4read_video_info */
static inline const void* mp4read_track_info(mp4read *m, int index)
{
	const struct mp4read_track *t = (struct mp4read_track*)m->tracks.ptr;
	if (index == -1) {
		if (m->tracks.len == 0)
			return NULL;
		t = &t[m->tracks.len-1];
	} else if (index < (ffssize)m->tracks.len) {
		t = &t[index];
	} else {
		return NULL;
	}
	return &t->audio;
}

static inline void mp4read_track_activate(mp4read *m, int index)
{
	struct mp4read_track *t = (struct mp4read_track*)m->tracks.ptr;
	m->curtrack = &t[index];
}

/** Get an absolute sample number */
#define mp4read_cursample(m)  ((m)->cursample)

enum MP4READ_E {
	MP4READ_EOK,
	MP4READ_EMEM,
	MP4READ_EDATA,
	MP4READ_ESIZE,
	MP4READ_EORDER,
	MP4READ_EDUPBOX,
	MP4READ_ENOREQ,
	MP4READ_EACODEC,
	MP4READ_ESEEK,
};

static inline const char* mp4read_error(mp4read *m)
{
	static const char *const errs[] = {
		"", // MP4READ_EOK
		"not enough memory", // MP4READ_EMEM
		"invalid data", // MP4READ_EDATA
		"invalid box size", // MP4READ_ESIZE
		"unsupported order of boxes", // MP4READ_EORDER
		"duplicate box", // MP4READ_EDUPBOX
		"mandatory box not found", // MP4READ_ENOREQ
		"unsupported audio codec", // MP4READ_EACODEC
		"invalid seek position", // MP4READ_ESEEK
	};

	if (m->boxes[m->ictx].name[0] == '\0')
		return errs[m->err];

	ffs_format(m->errmsg, sizeof(m->errmsg), "%4s: %s%Z"
		, m->boxes[m->ictx].name, errs[m->err]);
	return m->errmsg;
}

/** Get info for a MP4-sample
Return file offset */
static inline ffuint64 _mp4_data(const ffuint64 *chunks, const struct mp4_seekpt *sk, ffuint isamp, ffuint *data_size, ffuint64 *cursample)
{
	ffuint off = 0;

	for (ffuint i = isamp - 1;  (int)i >= 0;  i--) {
		if (sk[i].chunk_id != sk[isamp].chunk_id)
			break;
		off += sk[i].size;
	}

	*data_size = sk[isamp].size;
	*cursample = sk[isamp].audio_pos;
	return chunks[sk[isamp].chunk_id] + off;
}

static inline void mp4read_open(mp4read *m)
{
	m->seek_sample = -1;
	m->boxes[0].ctx = mp4_ctx_global;
	m->boxes[0].size = (ffuint64)-1;
}

static inline void mp4read_open2(mp4read *m, struct avpk_reader_conf *conf)
{
	mp4read_open(m);
	m->log = conf->log;
	m->udata = conf->opaque;
}

static inline void mp4read_close(mp4read *m)
{
	struct mp4read_track *t;
	FFSLICE_WALK(&m->tracks, t) {
		if (t->audio.type == 1)
			ffstr_free(&t->audio.codec_conf);
		ffvec_free(&t->sktab);
		ffvec_free(&t->chunktab);
	}
	ffvec_free(&m->tracks);
	ffstr_free(&m->stts);
	ffstr_free(&m->stsc);
	ffstream_free(&m->stream);
}

/**
Return 0 on success
  -1 if need more data, i.e. sizeof(struct mp4box64)
  >0: enum MP4READ_E error */
static inline int _mp4_box_parse(mp4read *m, struct mp4_box *parent, struct mp4_box *box, ffstr *data)
{
	const struct mp4box *b = (struct mp4box*)data->ptr;
	if (sizeof(*b) > data->len)
		return -1;

	ffuint64 sz = ffint_be_cpu32_ptr(b->size);
	ffuint box_szof = sizeof(struct mp4box);
	if (sz == 1) {
		const struct mp4box64 *b64 = (struct mp4box64*)data->ptr;
		if (sizeof(*b64) > data->len)
			return -1;
		sz = ffint_be_cpu64_ptr(b64->largesize);
		box_szof = sizeof(struct mp4box64);
	}
	box->osize = sz;

	int idx = mp4_box_find(parent->ctx, b->type);
	if (idx != -1) {
		const struct mp4_bbox *b = &parent->ctx[idx];
		ffmem_copy(box->name, b->type, 4);
		box->type = b->flags;
		box->ctx = b->ctx;
	}

	ffuint64 box_off = m->off - ffstream_used(&m->stream);
	_mp4read_log(m, "%*c%4s  %U  @%xU"
		, (ffsize)m->ictx, ' '
		, b->type, box->osize, box_off);

	if (box->osize < box_szof || box->osize > parent->size)
		return MP4READ_ESIZE;

	if (idx != -1 && ffbit_set32(&parent->usedboxes, idx) && !(box->type & MP4_F_MULTI))
		return MP4READ_EDUPBOX;

	box->size = sz - box_szof;
	return 0;
}

/**
Return -1 on success
  0 if success, but the parent box must be closed too
  >0: enum MP4READ_E error */
static inline int _mp4_box_close(mp4read *m, struct mp4_box *box)
{
	struct mp4_box *parent = box - 1;

	if (box->ctx != NULL) {
		for (ffuint i = 0;  ;  i++) {
			if ((box->ctx[i].flags & MP4_F_REQ)
				&& !ffbit_test32(&box->usedboxes, i))
				return MP4READ_ENOREQ;
			if (box->ctx[i].flags & MP4_F_LAST)
				break;
		}
	}

	if (m->ictx == 0) {
		ffmem_zero_obj(box);
		return -1;
	}

	parent->size -= box->osize;
	ffmem_zero_obj(box);
	m->ictx--;
	if (parent->size != 0)
		return -1;

	return 0;
}

/**
Return absolute file offset */
static inline ffuint64 mp4read_offset(mp4read *m)
{
	return m->off;
}


/**
Return enum MMTAG */
static inline int mp4read_tag(mp4read *m, ffstr *val)
{
	*val = m->tagval;
	return m->tag;
}

static inline void mp4read_seek(mp4read *m, ffuint64 sample)
{
	m->seek_sample = sample;
}

#define _MP4R_ERR(m, e) \
	(m)->err = (e),  MP4READ_ERROR

/**
Return MP4READ_DATA or MP4READ_TAG on success;
	MP4READ_ERROR on error */
static inline int _mp4_box_process(mp4read *m, ffstr sbox)
{
	int r, rc = MP4READ_DATA, rd;
	struct mp4_box *box = &m->boxes[m->ictx];
	rd = MP4_GET_MINSIZE(box->type);

	if (box->type & MP4_F_FULLBOX)
		box->size -= 4;
	ffstr_shift(&sbox, box->osize - box->size);

	switch (MP4_GET_TYPE(box->type)) {

	case BOX_FTYP:
		break;

	case BOX_TRAK:
		m->curtrack = ffvec_pushT(&m->tracks, struct mp4read_track);
		ffmem_zero_obj(m->curtrack);
		break;

	case BOX_MDHD: {
		const struct mp4_mdhd0 *mdhd = (struct mp4_mdhd0*)sbox.ptr;
		m->curtrack->audio.format.rate = ffint_be_cpu32_ptr(mdhd->timescale);
		break;
	}

	case BOX_STSD_ALAC:
	case BOX_STSD_MP4A:
		rd = mp4_afmt_read(sbox, &m->curtrack->audio.format);
		m->curtrack->audio.type = 1;
		break;

	case BOX_STSD_AVC1: {
		struct mp4_video v;
		r = mp4_avc1_read(sbox.ptr, sbox.len, &v);
		if (r < 0)
			return _MP4R_ERR(m, MP4READ_EDATA);
		m->curtrack->video.type = 0;
		m->curtrack->video.codec = AVPKC_AVC;
		m->curtrack->video.codec_name = "H.264";
		m->curtrack->video.width = v.width;
		m->curtrack->video.height = v.height;
		break;
	}

	case BOX_ALAC:
		if (m->curtrack->audio.codec_conf.len != 0)
			break;
		if (NULL == ffstr_dup(&m->curtrack->audio.codec_conf, sbox.ptr, sbox.len))
			return _MP4R_ERR(m, MP4READ_EMEM);
		m->curtrack->audio.codec = AVPKC_ALAC;
		m->curtrack->audio.codec_name = "ALAC";
		break;

	case BOX_ESDS: {
		struct mp4_acodec ac;
		r = mp4_esds_read(sbox.ptr, sbox.len, &ac);
		if (r < 0)
			return _MP4R_ERR(m, MP4READ_EDATA);

		switch (ac.type) {
		case MP4_ESDS_DEC_MPEG4_AUDIO:
			m->curtrack->audio.codec = AVPKC_AAC;
			m->curtrack->audio.codec_name= "AAC";
			break;
		case MP4_ESDS_DEC_MPEG1_AUDIO:
			m->curtrack->audio.codec = AVPKC_MP3;
			m->curtrack->audio.codec_name= "MPEG-1";
			break;
		}

		if (m->curtrack->audio.codec == 0)
			return _MP4R_ERR(m, MP4READ_EACODEC);

		if (m->curtrack->audio.codec_conf.len != 0)
			break;
		if (NULL == ffstr_dup(&m->curtrack->audio.codec_conf, ac.conf, ac.conflen))
			return _MP4R_ERR(m, MP4READ_EMEM);

		m->curtrack->audio.aac_bitrate = ac.avg_brate;
		break;
	}

	case BOX_STSZ:
		r = mp4_stsz_read(sbox.ptr, sbox.len, NULL);
		if (r < 0)
			return _MP4R_ERR(m, MP4READ_EDATA);
		if (NULL == ffvec_alloc(&m->curtrack->sktab, r, sizeof(struct mp4_seekpt)))
			return _MP4R_ERR(m, MP4READ_EMEM);
		r = mp4_stsz_read(sbox.ptr, sbox.len, (void*)m->curtrack->sktab.ptr);
		if (r < 0)
			return _MP4R_ERR(m, MP4READ_EDATA);
		m->curtrack->sktab.len = r;
		break;

	case BOX_STTS:
		ffstr_dup(&m->stts, sbox.ptr, sbox.len);
		break;

	case BOX_STSC:
		ffstr_dup(&m->stsc, sbox.ptr, sbox.len);
		break;

	case BOX_STCO:
	case BOX_CO64:
		r = mp4_stco_read(sbox.ptr, sbox.len, MP4_GET_TYPE(box->type), NULL);
		if (r < 0)
			return _MP4R_ERR(m, MP4READ_EDATA);
		if (NULL == ffvec_alloc(&m->curtrack->chunktab, r, sizeof(ffint64)))
			return _MP4R_ERR(m, MP4READ_EMEM);
		r = mp4_stco_read(sbox.ptr, sbox.len, MP4_GET_TYPE(box->type), (void*)m->curtrack->chunktab.ptr);
		if (r < 0)
			return _MP4R_ERR(m, MP4READ_EDATA);
		m->curtrack->chunktab.len = r;
		break;

	case BOX_ILST_DATA: {
		const struct mp4_box *parent = &m->boxes[m->ictx - 1];
		r = mp4_ilst_data_read(sbox.ptr, sbox.len, MP4_GET_TYPE(parent->type) - _BOX_TAG, &m->tagval, m->tagbuf, sizeof(m->tagbuf));
		if (r == 0)
			break;

		m->tag = r;
		rc = MP4READ_TAG;
		break;
	}

	case BOX_ITUNES_NAME:
		m->itunes_smpb = ffstr_eqcz(&sbox, "iTunSMPB");
		break;

	case BOX_ITUNES_DATA:
		if (!m->itunes_smpb)
			break;
		m->itunes_smpb = 0;
		mp4_itunes_smpb_read(sbox.ptr, sbox.len, &m->curtrack->audio.enc_delay, &m->curtrack->audio.end_padding);
		break;
	}

	// skip processed data
	box->size -= rd;
	return rc;
}

static inline int _mp4_trak_closed(mp4read *m)
{
	struct mp4read_track *t = m->curtrack;

	if (t->chunktab.len == 0) {
		ffmem_copy(m->boxes[++m->ictx].name, "stco", 4);
		return MP4READ_EDATA;
	}

	int r;
	ffint64 rr = mp4_stts_read((struct mp4_seekpt*)t->sktab.ptr, t->sktab.len, m->stts.ptr, m->stts.len);
	if (rr < 0) {
		ffmem_copy(m->boxes[++m->ictx].name, "stts", 4);
		return MP4READ_EDATA;
	}
	if (0 != (r = mp4_stsc_read((struct mp4_seekpt*)t->sktab.ptr, t->sktab.len, m->stsc.ptr, m->stsc.len))) {
		ffmem_copy(m->boxes[++m->ictx].name, "stsc", 4);
		return MP4READ_EDATA;
	}

	if (m->curtrack->audio.type == 1) {
		t->audio.total_samples = ffmin64(rr - (t->audio.enc_delay + t->audio.end_padding), rr);
		if (t->audio.total_samples != 0)
			t->audio.real_bitrate = m->total_size*8*t->audio.format.rate / t->audio.total_samples;

		ffuint stts_cnt = ffint_be_cpu32_ptr(m->stts.ptr);
		if (stts_cnt <= 2) {
			const struct mp4_seekpt *pt = (struct mp4_seekpt*)t->sktab.ptr;
			t->audio.frame_samples = pt[1].audio_pos;
		}
	}

	return 0;
}

/** Read meta data and codec data
Return enum MP4READ_R */
/* MP4 reading algorithm:
. Gather box header (size + name)
. Check size, gather box64 header if needed
. Search box within the current context
. Skip box if unknown
. Check flags, gather fullbox data, or minimum size, or the whole box, if needed
. Process box
*/
static inline int mp4read_process(mp4read *m, ffstr *input, ffstr *output)
{
	enum {
		R_BOXREAD, R_BOX_PARSE, R_BOXSKIP, R_BOXPROCESS,
		R_GATHER,
		R_TRKTOTAL,
		R_DATA, R_DATAREAD,
	};
	struct mp4_box *box;
	int r;

	for (;;) {

		box = &m->boxes[m->ictx];

		switch (m->state) {

		case R_BOXREAD:
			m->state = R_GATHER,  m->nextstate = R_BOX_PARSE,  m->gather_size = sizeof(struct mp4box);
			continue;

		case R_GATHER:
			if (ffstream_realloc(&m->stream, m->gather_size))
				return _MP4R_ERR(m, MP4READ_EMEM);
			r = ffstream_gather(&m->stream, *input, m->gather_size, &m->chunk);
			ffstr_shift(input, r);
			m->off += r;
			if (m->chunk.len < m->gather_size)
				return MP4READ_MORE;
			m->chunk.len = m->gather_size;
			m->state = m->nextstate;
			continue;

		case R_BOX_PARSE: {
			struct mp4_box *parent = &m->boxes[m->ictx];
			box = &m->boxes[m->ictx + 1];
			r = _mp4_box_parse(m, parent, box, &m->chunk);
			if (r == -1) {
				m->state = R_GATHER,  m->nextstate = R_BOX_PARSE,  m->gather_size = sizeof(struct mp4box64);
				continue;
			}
			FF_ASSERT(m->ictx != FF_COUNT(m->boxes));
			m->ictx++;

			if (r > 0) {
				m->err = r;
				if (r == MP4READ_EDUPBOX) {
					m->state = R_BOXSKIP;
					return MP4READ_WARN;
				}
				return MP4READ_ERROR;
			}

			ffuint prio = MP4_GET_PRIO(box->type);
			if (prio != 0) {
				if (prio > parent->prio + 1)
					return _MP4R_ERR(m, MP4READ_EORDER);
				parent->prio = prio;
			}

			ffuint minsize = MP4_GET_MINSIZE(box->type);
			if (box->type & MP4_F_FULLBOX)
				minsize += sizeof(struct mp4_fullbox);
			if (box->size < minsize)
				return _MP4R_ERR(m, MP4READ_ESIZE);
			if (box->type & MP4_F_WHOLE)
				minsize = box->size;
			if (minsize != 0) {
				m->state = R_GATHER,  m->nextstate = R_BOXPROCESS,  m->gather_size += minsize;
				continue;
			}

			m->state = R_BOXPROCESS;
		}
			// fallthrough

		case R_BOXPROCESS:
			r = _mp4_box_process(m, m->chunk);
			if (r == MP4READ_ERROR)
				return MP4READ_ERROR;
			ffstream_consume(&m->stream, box->osize - box->size);
			m->state = (box->ctx == NULL) ? R_BOXSKIP : R_BOXREAD;
			if (r == MP4READ_TAG) {
				if (m->tag == MMTAG_TRACKNO) {
					ffstr_splitby(&m->tagval, '/', &m->tagval, &m->tag_trktotal);
					m->state = R_TRKTOTAL;
					if (m->tagval.len == 1 && m->tagval.ptr[0] == '0')
						continue;
				}
				return MP4READ_TAG;
			}
			continue;

		case R_TRKTOTAL:
			m->state = R_BOXSKIP;
			if (!(m->tag_trktotal.len == 1 && m->tag_trktotal.ptr[0] == '0')) {
				m->tag = MMTAG_TRACKTOTAL;
				m->tagval = m->tag_trktotal;
				return MP4READ_TAG;
			}
			continue;

		case R_BOXSKIP: {
			m->state = R_BOXREAD;

			int seek = 0;
			if (box->size != 0) {
				if (ffstream_used(&m->stream) >= box->size) {
					ffstream_consume(&m->stream, box->size);
				} else {
					m->off += box->size - ffstream_used(&m->stream);
					ffstream_reset(&m->stream);
					seek = 1;
				}
				box->size = 0;
			}

			for (;;) {
				int t = MP4_GET_TYPE(m->boxes[m->ictx].type);
				r = _mp4_box_close(m, &m->boxes[m->ictx]);
				if (r > 0)
					return _MP4R_ERR(m, r);

				int r2;
				switch (t) {
				case BOX_TRAK:
					r2 = _mp4_trak_closed(m);
					ffstr_free(&m->stts);
					ffstr_free(&m->stsc);
					if (r2 != 0)
						return _MP4R_ERR(m, r2);
					break;

				case BOX_MOOV:
					m->state = R_DATA;
					return MP4READ_HEADER;
				}

				if (r != 0)
					break;
			}

			if (seek)
				return MP4READ_SEEK;

			continue;
		}


		case R_DATA: {
			if (m->seek_sample >= 0) {
				r = mp4_seek((void*)m->curtrack->sktab.ptr, m->curtrack->sktab.len, m->seek_sample);
				m->seek_sample = -1;
				if (r == -1)
					return _MP4R_ERR(m, MP4READ_ESEEK);
				m->curtrack->isamp = r;
			}

			if (m->curtrack->isamp == m->curtrack->sktab.len - 1)
				return MP4READ_DONE;
			ffuint64 off = _mp4_data((void*)m->curtrack->chunktab.ptr, (void*)m->curtrack->sktab.ptr, m->curtrack->isamp, &m->frsize, &m->cursample);
			m->curtrack->isamp++;
			m->state = R_GATHER,  m->nextstate = R_DATAREAD,  m->gather_size = m->frsize;
			ffuint64 cur_off = m->off - ffstream_used(&m->stream);
			if (off != cur_off) {
				m->off = off;
				ffstream_reset(&m->stream);
				return MP4READ_SEEK;
			}
			continue;
		}

		case R_DATAREAD: {
			ffstr_set(output, m->chunk.ptr, m->frsize);
			m->state = R_DATA;

			const struct mp4_seekpt *pts = m->curtrack->sktab.ptr;
			ffuint64 fr_off = m->off - ffstream_used(&m->stream);
			_mp4read_log(m, "fr#%u  size:%u  data-chunk:%u  audio-pos:%U  off:%xU"
				, m->curtrack->isamp - 1, m->frsize, pts[m->curtrack->isamp - 1].chunk_id, m->cursample, fr_off);

			ffstream_consume(&m->stream, m->gather_size);
			return MP4READ_DATA;
		}

		default:
			FF_ASSERT(0);
			return MP4READ_ERROR;
		}
	}

	// unreachable
}

static inline int mp4read_process2(mp4read *m, ffstr *input, union avpk_read_result *res)
{
	if (m->codec_conf_pending) {
		m->codec_conf_pending = 0;
		*(ffstr*)&res->frame = m->curtrack->audio.codec_conf;
		res->frame.pos = ~0ULL;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		return AVPK_DATA;
	}

	int r = mp4read_process(m, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_HEADER: {
		const struct mp4read_audio_info *ai;
		for (unsigned i = 0;;  i++) {
			if (!(ai = mp4read_track_info(m, i))) {
				res->error.message = "No audio track";
				res->error.offset = ~0ULL;
				return AVPK_ERROR;
			}
			if (ai->type == 1) {
				mp4read_track_activate(m, i);
				break;
			}
		}
		res->hdr.codec = ai->codec;
		res->hdr.sample_rate = ai->format.rate;
		res->hdr.channels = ai->format.channels;
		res->hdr.duration = ai->total_samples;
		res->hdr.audio_bitrate = ((int)ai->aac_bitrate > 0) ? ai->aac_bitrate : 0;
		res->hdr.delay = ai->enc_delay;
		res->hdr.padding = ai->end_padding;
		m->codec_conf_pending = 1;
		break;
	}

	case AVPK_META:
		res->tag.id = m->tag;
		ffstr_null(&res->tag.name);
		res->tag.value = m->tagval;
		break;

	case AVPK_DATA:
		res->frame.pos = m->cursample;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = m->curtrack->audio.frame_samples;
		break;

	case AVPK_SEEK:
		res->seek_offset = m->off;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = mp4read_error(m);
		res->error.offset = m->off;
		break;
	}
	return r;
}

#undef _MP4R_ERR

AVPKR_IF_INIT(avpk_mp4, "mp4\0m4a", AVPKF_MP4, mp4read, mp4read_open2, mp4read_process2, mp4read_seek, mp4read_close);
