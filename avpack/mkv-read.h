/** avpack: .mkv reader
* inaccurate seeking (next Block element after the target)

2016,2021, Simon Zolin
*/

/*
mkvread_open
mkvread_close
mkvread_process
mkvread_curpos
mkvread_offset
mkvread_tag
mkvread_track_info
mkvread_block_trackid
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/mkv.h>
#include <avpack/vorbistag.h>
#include <ffbase/stream.h>

struct mkv_el {
	int id; // enum MKV_ELID
	ffuint flags;
	ffuint usemask;
	ffuint prio;
	ffuint64 size, endoff;
	const struct mkv_binel *ctx;
};

typedef void (*mkv_log_t)(void *udata, const char *fmt, va_list va);

struct mkvread_audio_info {
	int type; // enum MKV_TRKTYPE
	ffuint codec; // enum AVPK_CODEC
	int id;

	ffuint bits;
	ffuint channels;
	ffuint sample_rate;
	ffuint64 duration_msec;
	ffstr codec_conf;
};

struct mkvread_video_info {
	int type; // enum MKV_TRKTYPE
	ffuint codec; // enum AVPK_CODEC
	int id;

	ffuint64 duration_msec;
	ffuint width, height;
	ffstr codec_conf;
};

struct mkvread_track {
	union {
		int type;
		struct mkvread_audio_info audio;
		struct mkvread_video_info video;
	};
};

struct _mkvread_seekpoint {
	ffuint64 pos;
	ffuint64 off;
};

typedef struct mkvread {
	ffuint state;
	int err;
	const char *errmsg;
	ffuint64 off;
	ffuint64 el_off;

	ffuint gstate;
	ffuint gsize;
	ffstr gbuf;
	ffstream stream;

	struct mkv_el els[6];
	ffuint ictx;

	struct mkvread_track *curtrack;
	ffvec tracks; // struct mkvread_track[]
	ffuint64 curpos;
	ffuint64 clust_time;
	ffstr codec_data;
	ffuint scale; //ns
	float dur;
	ffvec lacing; //ffuint[].  Sizes of frames that are placed in a single block.
	ffsize lacing_off;
	ffuint64 block_trackid;
	unsigned sel_track_id, sel_track_index, codec_conf_state;
	struct mkv_vorbis mkv_vorbis;
	ffstr mkv_vorbis_data;

	// seeking:
	struct _mkvread_seekpoint seekpt_glob[2];
	struct _mkvread_seekpoint seekpt[2];
	ffuint64 seek_msec;
	ffuint64 last_seek_off;
	ffuint seek_block, seek_cluster;
	ffuint64 clust_off;
	unsigned a_sample_rate;

	ffvec tagname;
	ffstr tagval;

	mkv_log_t log;
	void *udata;
} mkvread;

#define mkvread_curpos(m)  ((m)->curpos)
#define mkvread_offset(m)  ((m)->off)

/** Get track meta info
Return struct mkvread_audio_info | struct mkvread_video_info */
static inline const void* mkvread_track_info(mkvread *m, int index)
{
	const struct mkvread_track *t = (struct mkvread_track*)m->tracks.ptr;
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

/** Get tag info */
static inline ffstr mkvread_tag(mkvread *m, ffstr *val)
{
	*val = m->tagval;
	return *(ffstr*)&m->tagname;
}

static inline void _mkvread_log(mkvread *m, const char *fmt, ...)
{
	if (m->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	m->log(m->udata, fmt, va);
	va_end(va);
}

static inline const char* mkvread_error(mkvread *m)
{
	static const char* const errors[] = {
		"",
		"bad integer", // MKV_EINTVAL
		"bad float number", // MKV_EFLTVAL
		"lacing: bad frame size", // MKV_ELACING
		"bad Vorbis codec private data", // MKV_EVORBISHDR
		"not enough memory", // MKV_EMEM
	};
	if ((ffuint)m->err < FF_COUNT(errors))
		return errors[m->err];
	if (m->err == -1)
		return m->errmsg;
	return "";
}

enum MKVREAD_R {
	/** Use mkvread_block_trackid() to get track ID */
	MKVREAD_DATA = AVPK_DATA,
	MKVREAD_MORE = AVPK_MORE,
	MKVREAD_HEADER = AVPK_HEADER,
	MKVREAD_SEEK = AVPK_SEEK,
	MKVREAD_DONE = AVPK_FIN,
	MKVREAD_TAG = AVPK_META,
	MKVREAD_ERROR = AVPK_ERROR,
};

#define _MKVR_ERR(m, e) \
	(m)->err = (e),  MKVREAD_ERROR
#define _MKVR_ERRSTR(m, e) \
	(m)->err = -1,  (m)->errmsg = (e),  MKVREAD_ERROR

/**
total_size: (optional) */
static inline int mkvread_open(mkvread *m, ffuint64 total_size)
{
	m->seek_msec = (ffuint64)-1;
	m->seekpt_glob[1].off = (total_size == 0) ? (ffuint64)-1 : total_size;
	m->els[m->ictx].size = (ffuint64)-1;
	m->els[m->ictx].endoff = (ffuint64)-1;
	m->els[m->ictx].ctx = mkv_ctx_global;
	ffstream_realloc(&m->stream, 4096);
	return 0;
}

static inline void mkvread_open2(mkvread *m, struct avpk_reader_conf *conf)
{
	mkvread_open(m, conf->total_size);
	m->log = conf->log;
	m->udata = conf->opaque;
}

static inline void mkvread_close(mkvread *m)
{
	struct mkvread_track *t;
	FFSLICE_WALK(&m->tracks, t) {
		if (t->type == MKV_TRK_AUDIO)
			ffstr_free(&t->audio.codec_conf);
		else if (t->type == MKV_TRK_VIDEO)
			ffstr_free(&t->video.codec_conf);
	}
	ffvec_free(&m->tracks);
	ffstream_free(&m->stream);
	ffstr_free(&m->codec_data);
	ffvec_free(&m->lacing);
	ffvec_free(&m->tagname);
}

static inline void mkvread_seek(mkvread *m, ffuint64 msec)
{
	m->seek_msec = msec;
}

static inline void mkvread_seek_s(mkvread *m, ffuint64 sample)
{
	m->seek_msec = sample * 1000 / m->a_sample_rate;
}

/** Get track ID of the current data block */
#define mkvread_block_trackid(m)  ((m)->block_trackid)

/** Read block header */
static int _mkvread_block(mkvread *m, ffstr *data)
{
	int r;
	struct {
		ffuint64 trackno;
		ffuint time;
		ffuint flags;
	} sblk;

	if (-1 == (r = mkv_varint_shift(data, &sblk.trackno)))
		return _MKVR_ERR(m, MKV_EINTVAL);

	if (data->len < 3)
		return _MKVR_ERRSTR(m, "too small element");
	sblk.time = ffint_be_cpu16_ptr(data->ptr);
	sblk.flags = (ffbyte)data->ptr[2];
	ffstr_shift(data, 3);

	_mkvread_log(m, "block: track:%U  time:%u (cluster:%U)  flags:%xu"
		, sblk.trackno, sblk.time, m->clust_time, sblk.flags);

	m->curpos = (ffuint64)(m->clust_time + sblk.time) * m->scale/1000000;
	m->block_trackid = sblk.trackno;

	if (sblk.flags & 0x06) {
		if (0 != (r = mkv_lacing(data, &m->lacing, sblk.flags & 0x06)))
			return _MKVR_ERR(m, r);
		m->lacing_off = 0;
		return 0x1ace;
	}

	return MKVREAD_DATA;
}

/** Process element header */
static int _mkvread_el_hdr(mkvread *m)
{
	ffuint id = 0;
	ffuint64 size = 0;
	int n = mkv_read_id_size(m->gbuf, &id, &size);
	if (n == 0) {
		return _MKVR_ERRSTR(m, "bad element ID or size");
	} else if (n < 0) {
		m->gsize = -n;
		return 0xfeed;
	}

	struct mkv_el *parent = &m->els[m->ictx];
	FF_ASSERT(m->ictx != FF_COUNT(m->els));
	m->ictx++;
	struct mkv_el *el = &m->els[m->ictx];
	el->size = size;
	m->el_off = m->off - ffstream_used(&m->stream);
	el->endoff = m->el_off + n + el->size;

	el->id = MKV_T_UKN;
	int i = mkv_el_find(parent->ctx, (ffuint)id);
	if (i >= 0) {
		el->id = parent->ctx[i].flags & MKV_MASK_ELID;
		el->flags = parent->ctx[i].flags & ~MKV_MASK_ELID;
		el->ctx = parent->ctx[i].children;
	}

	_mkvread_log(m, "%*c%xu%s  size:%u  offset:%xU  end:%xU"
		, (ffsize)m->ictx - 1, ' '
		, id, (i < 0) ? "(?)" : "", el->size, m->el_off, el->endoff);

	if (el->endoff > parent->endoff)
		return _MKVR_ERRSTR(m, "too large element");

	ffstream_consume(&m->stream, n); // skip header
	ffstr_shift(&m->gbuf, n);

	if (el->id == MKV_T_UKN) {
		return 0xbad;
	}

	if ((ffuint)i < 32 && ffbit_set32(&parent->usemask, i)
		&& !(el->flags & MKV_F_MULTI))
		return _MKVR_ERRSTR(m, "duplicate element");

	ffuint prio = MKV_GET_PRIO(el->flags);
	if (prio != 0) {
		if (prio > parent->prio + 1)
			return _MKVR_ERRSTR(m, "unsupported order of elements");
		parent->prio = prio;
	}

	if (el->flags & (MKV_F_WHOLE | MKV_F_INT | MKV_F_INT8 | MKV_F_FLT)) {
		m->gsize = el->size;
		return 0xfeede1;
	}

	return 0xd0;
}

/** Process element */
static int _mkvread_el(mkvread *m, ffstr *output)
{
	int r;
	int val4 = 0;
	ffuint64 val = 0;
	double fval = 0;
	struct mkv_el *el = &m->els[m->ictx];
	ffstr data = FFSTR_INITN(m->gbuf.ptr, el->size);

	if (el->flags & (MKV_F_INT | MKV_F_INT8)) {
		if (0 != mkv_int_ntoh(&val, data.ptr, data.len))
			return _MKVR_ERR(m, MKV_EINTVAL);

		if ((el->flags & MKV_F_INT) && val > 0xffffffff)
			return _MKVR_ERR(m, MKV_EINTVAL);

		val4 = val;
	}

	if (el->flags & MKV_F_FLT) {
		if (0 != mkv_flt_ntoh(&fval, data.ptr, data.len))
			return _MKVR_ERR(m, MKV_EFLTVAL);
	}

	switch (el->id) {
	case MKV_T_VER:
		if (val4 != 1)
			return _MKVR_ERRSTR(m, "unsupported EBML version");
		break;

	case MKV_T_DOCTYPE:
		_mkvread_log(m, "doctype: %S", &data);
		break;


	case MKV_T_SCALE:
		m->scale = val4;  break;

	case MKV_T_DUR:
		m->dur = fval;  break;

	case MKV_T_TITLE:
		m->tagval = data;  break;


	// case MKV_T_SEEKID:
	// case MKV_T_SEEKPOS:
	// case MKV_T_TRKNAME:


	case MKV_T_TRKENT:
		m->curtrack = ffvec_pushT(&m->tracks, struct mkvread_track);
		ffmem_zero_obj(m->curtrack);
		m->curtrack->type = -1;
		m->curtrack->audio.id = -1;
		break;

	case MKV_T_TRKNO:
		m->curtrack->audio.id = val4;  break;

	case MKV_T_TRKTYPE:
		m->curtrack->type = val4;  break;

	case MKV_T_CODEC_ID:
		_mkvread_log(m, "codec: %S", &data);
		if (0 != (r = mkv_codec(data))) {
			m->curtrack->audio.codec = r;
		}
		break;

	case MKV_T_CODEC_PRIV:
		ffstr_free(&m->codec_data);
		if (NULL == ffstr_dup2(&m->codec_data, &data))
			return _MKVR_ERR(m, MKV_EMEM);
		break;


	case MKV_T_A_RATE:
		m->curtrack->audio.sample_rate = (ffuint)fval;  break;
	case MKV_T_A_OUTRATE:
		m->curtrack->audio.sample_rate = (ffuint)fval;  break;

	case MKV_T_A_CHANNELS:
		m->curtrack->audio.channels = val4;  break;

	case MKV_T_A_BITS:
		m->curtrack->audio.bits = val4;  break;


	case MKV_T_V_WIDTH:
		m->curtrack->video.width = val4;  break;

	case MKV_T_V_HEIGHT:
		m->curtrack->video.height = val4;  break;


	case MKV_T_TAG_NAME:
		ffvec_free(&m->tagname);
		ffvec_addstr(&m->tagname, &data);
		break;

	case MKV_T_TAG_VAL:
	case MKV_T_TAG_BVAL:
		ffstr_set2(&m->tagval, &data);
		break;


	case MKV_T_CLUST:
		if (m->seekpt_glob[0].off == 0) {
			m->seekpt_glob[0].off = m->el_off;
			m->curtrack = NULL;
			return MKVREAD_HEADER;
		}
		break;

	case MKV_T_TIME:
		m->clust_time = val4;
		break;

	case MKV_T_BLOCK:
	case MKV_T_SBLOCK:
		if (!m->seek_block && m->seek_msec != (ffuint64)-1) {
			m->state = 7 /*R_SEEK_INIT*/;
			return 0xca11;
		}

		r = _mkvread_block(m, &data);

		if (m->seek_block
			&& (r == 0x1ace || r == MKVREAD_DATA)) {

			if (m->curpos >= m->seek_msec) {
				m->seek_msec = (ffuint64)-1;
				m->seek_block = 0;
			} else {
				return -1;
			}
		}

		switch (r) {
		case 0x1ace:
			m->state = 4 /*R_LACING*/;
			return 0xca11;
		case MKVREAD_DATA:
			break;
		case MKVREAD_ERROR:
			return MKVREAD_ERROR;
		}

		ffstr_set2(output, &data);
		return MKVREAD_DATA;
	}

	return -1;
}

/** Element is closed */
static int _mkvread_el_close(mkvread *m)
{
	struct mkv_el *el = &m->els[m->ictx];
	ffuint id = el->id;
	ffmem_zero_obj(el);
	m->ictx--;

	switch (id) {
	case MKV_T_SEG:
		return MKVREAD_DONE;

	case MKV_T_TRKENT:
		if (m->scale == 0)
			m->scale = 1000000;
		switch (m->curtrack->type) {
		case MKV_TRK_AUDIO:
			m->curtrack->audio.duration_msec = m->dur * m->scale / 1000000;
			m->seekpt_glob[1].pos = ffmax(m->seekpt_glob[1].pos, m->curtrack->audio.duration_msec);
			m->curtrack->audio.codec_conf = m->codec_data;
			break;
		case MKV_TRK_VIDEO:
			m->curtrack->video.duration_msec = m->dur * m->scale / 1000000;
			m->seekpt_glob[1].pos = ffmax(m->seekpt_glob[1].pos, m->curtrack->video.duration_msec);
			m->curtrack->video.codec_conf = m->codec_data;
			break;
		}
		m->curtrack = NULL;
		ffstr_null(&m->codec_data);
		break;

	case MKV_T_TITLE:
		ffvec_free(&m->tagname);
		ffstr_setz((ffstr*)&m->tagname, "title");
		return MKVREAD_TAG;

	case MKV_T_TAG:
		return MKVREAD_TAG;
	}

	return -1;
}

/** Exit from Cluster element's context */
static void _mkvr_cluster_exit(mkvread *m)
{
	for (;;) {
		struct mkv_el *el = &m->els[m->ictx];
		ffuint id = el->id;
		ffmem_zero_obj(el);
		m->ictx--;
		if (id == MKV_T_CLUST) {
			FF_ASSERT(m->els[m->ictx].id == MKV_T_SEG);
			break;
		}
	}
}

/** Estimate file offset by time position */
static ffuint64 _mkvr_seek_offset(const struct _mkvread_seekpoint *pt, ffuint64 target)
{
	ffuint64 samples = pt[1].pos - pt[0].pos;
	ffuint64 size = pt[1].off - pt[0].off;
	ffuint64 off = (target - pt[0].pos) * size / samples;
	return pt[0].off + off;
}

/** Find Cluster element; find Cluster.Timecode */
static int _mkvr_seek_sync(mkvread *m, ffstr *input, ffstr *output)
{
	int r, pos;
	ffstr chunk = {};
	for (;;) {
		r = ffstream_gather(&m->stream, *input, 4, &chunk);
		ffstr_shift(input, r);
		m->off += r;
		if (chunk.len < 4)
			return 0xfeed;

		if (0 <= (pos = ffstr_find(&chunk, "\x1f\x43\xb6\x75", 4)))
			break;

		ffstream_consume(&m->stream, chunk.len - (4-1));
	}

	ffstream_consume(&m->stream, pos);
	ffstr_shift(&chunk, pos);
	m->clust_off = m->off - chunk.len;
	*output = chunk;
	return 0;
}

/** Adjust the search window after the current offset has become too large */
static int _mkvr_seek_adjust_edge(mkvread *m, struct _mkvread_seekpoint *sp)
{
	_mkvread_log(m, "seek: no new cluster at offset %xU", m->last_seek_off);

	sp[1].off = m->last_seek_off; // narrow the search window to make some progress
	if (sp[1].off - sp[0].off > 64*1024) {
		m->off = sp[0].off + (sp[1].off - sp[0].off) / 2; // binary search
	} else {
		m->off = sp[0].off + 1; // small search window: try to find a 2nd cluster
	}

	if (m->off == m->last_seek_off) {
		return 0xdeed;
	}

	m->last_seek_off = m->off;
	return MKVREAD_SEEK;
}

/** Adjust the search window */
static int _mkvr_seek_adjust(mkvread *m, struct _mkvread_seekpoint *sp)
{
	_mkvread_log(m, "seek: tgt:%U cur:%U [%U..%U](%U)  off:%xU [%xU..%xU](%xU)"
		, m->seek_msec, m->clust_time
		, sp[0].pos, sp[1].pos, sp[1].pos - sp[0].pos
		, m->clust_off
		, sp[0].off, sp[1].off, sp[1].off - sp[0].off);

	int i = (m->seek_msec < m->clust_time);
	sp[i].pos = m->clust_time;
	sp[i].off = m->clust_off;

	if (sp[0].off >= sp[1].off)
		return 1;
	return 0;
}

/* MKV read algorithm:
. read element id & size
. process element:
 . search element within the current context
 . skip if unknown
 . or gather its data and convert (string -> int/float) if needed

Seeking:
. seek to an estimated file position
. find Cluster element, parse its Time element, narrow the search window
. repeat until the target Cluster is found
. skip Block elements until the target Block is found
*/
static inline int mkvread_process(mkvread *m, ffstr *input, ffstr *output)
{
	enum {
		R_ELID1, R_EL,
		R_LACING=4,
		R_NEXTCHUNK, R_SKIP,
		R_SEEK_INIT=7, R_SEEK, R_SEEK_SYNC, R_SEEK_NEXT, R_SEEK_DATA, R_SEEK_DONE,
		R_GATHER,
	};
	int r;

	for (;;) {
		switch (m->state) {
		case R_GATHER:
			if (0 != ffstream_realloc(&m->stream, m->gsize))
				return _MKVR_ERRSTR(m, "not enough memory");
			r = ffstream_gather(&m->stream, *input, m->gsize, &m->gbuf);
			ffstr_shift(input, r);
			m->off += r;
			if (m->gbuf.len < m->gsize)
				return MKVREAD_MORE;
			m->state = m->gstate;
			continue;

		case R_SKIP: {
			struct mkv_el *el = &m->els[m->ictx];
			m->state = R_NEXTCHUNK;
			if (el->endoff > m->off + input->len) {
				ffstream_reset(&m->stream);
				m->gbuf.len = 0;
				m->off = el->endoff;
				return MKVREAD_SEEK;
			}

			// skip existing bufferred data for this element
			ffuint n = ffmin(el->size, m->gbuf.len);
			ffstream_consume(&m->stream, n);
			ffstr_shift(&m->gbuf, n);

			// skip existing input data for this element
			n = el->size - n;
			ffstr_shift(input, n);
			m->off += n;
		}
			// fallthrough

		case R_NEXTCHUNK: {
			struct mkv_el *el = &m->els[m->ictx];
			if (m->off - ffstream_used(&m->stream) == el->endoff) {
				if (-1 != (r = _mkvread_el_close(m)))
					return r;
				continue;
			}

			m->state = R_ELID1;
		}
			// fallthrough

		case R_ELID1:
			r = _mkvread_el_hdr(m);
			switch (r) {
			case 0xbad:
				m->state = R_SKIP;  break;

			case 0xfeed:
				m->state = R_GATHER;  m->gstate = R_ELID1;  break;

			case 0xfeede1:
				m->state = R_GATHER;  m->gstate = R_EL;  break;

			case 0xd0:
				m->state = R_EL;  break;

			case MKVREAD_ERROR:
				if (m->seek_cluster) {
					m->state = R_SEEK_NEXT;
					continue; // couldn't parse data; continue searching for a valid Cluster element
				}
				return MKVREAD_ERROR;
			}
			continue;

		case R_EL: {
			r = _mkvread_el(m, output);
			if (r == 0xca11)
				continue;
			else if (r == MKVREAD_DATA) {
				m->state = R_SKIP;
				return MKVREAD_DATA;
			} else if (r != -1)
				return r;

			struct mkv_el *el = &m->els[m->ictx];

			if (m->seek_cluster && el->id == MKV_T_TIME) {
				m->state = R_SEEK_DATA;
				continue; // got 'clust_time'
			}

			m->state = R_SKIP;
			if (el->ctx != NULL)
				m->state = R_NEXTCHUNK;
			continue;
		}

		case R_LACING: {
			if (m->seek_msec != (ffuint64)-1) {
				m->state = R_SEEK_INIT;
				continue;
			}

			if (m->lacing_off == m->lacing.len) {
				m->state = R_SKIP;
				ffstr_set2(output, &m->gbuf);
				return MKVREAD_DATA;
			}
			ffuint n = *ffslice_itemT(&m->lacing, m->lacing_off, ffuint);
			m->lacing_off++;
			ffstr_set(output, m->gbuf.ptr, n);
			ffstr_shift(&m->gbuf, n);
			return MKVREAD_DATA;
		}


		case R_SEEK_INIT:
			if (m->seekpt_glob[1].off == (ffuint64)-1)
				return _MKVR_ERRSTR(m, "can't seek");
			m->seek_cluster = 1;
			ffmem_copy(m->seekpt, m->seekpt_glob, sizeof(m->seekpt));
			// fallthrough

		case R_SEEK:
			m->off = _mkvr_seek_offset(m->seekpt, m->seek_msec);
			if (m->off == m->last_seek_off) {
				m->state = R_SEEK_DONE;
				continue;
			}
			m->last_seek_off = m->off;
			ffstream_reset(&m->stream);
			m->state = R_SEEK_SYNC;
			return MKVREAD_SEEK;

		case R_SEEK_SYNC:
			r = _mkvr_seek_sync(m, input, &m->gbuf);

			if ((r == 0xfeed && m->off >= m->seekpt[1].off)
					|| (r != 0xfeed && m->clust_off >= m->seekpt[1].off)) {
				int r2 = _mkvr_seek_adjust_edge(m, m->seekpt);
				if (r2 == 0xdeed) {
					m->state = R_SEEK_DONE;
					continue;
				} else if (r2 == MKVREAD_SEEK) {
					ffstream_reset(&m->stream);
					return MKVREAD_SEEK;
				}
			}

			if (r == 0xfeed)
				return MKVREAD_MORE;

			_mkvr_cluster_exit(m);
			m->state = R_ELID1;
			continue;

		case R_SEEK_NEXT:
			ffstream_reset(&m->stream);
			m->state = R_SEEK_SYNC;
			m->off = m->clust_off + 1;
			return MKVREAD_SEEK;

		case R_SEEK_DATA:
			if (!_mkvr_seek_adjust(m, m->seekpt)) {
				m->state = R_SEEK;
				continue;
			}
			// fallthrough

		case R_SEEK_DONE:
			_mkvr_cluster_exit(m);
			m->seek_cluster = 0;
			m->seek_block = 1;
			ffstream_reset(&m->stream);
			m->gbuf.len = 0;
			m->last_seek_off = 0;
			m->off = m->seekpt[0].off;
			ffmem_zero(m->seekpt, sizeof(m->seekpt));
			m->state = R_ELID1;
			return MKVREAD_SEEK;

		}
	}
}

static inline int mkvread_process2(mkvread *m, ffstr *input, union avpk_read_result *res)
{
	int r, n;
	switch (m->codec_conf_state) {
	case 1: {
		m->codec_conf_state = 0;
		const struct mkvread_track *t = (struct mkvread_track*)m->tracks.ptr;
		t = &t[m->sel_track_index];
		*(ffstr*)&res->frame = t->audio.codec_conf;
		res->frame.pos = ~0ULL;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		return AVPK_DATA;
	}

	case 2: // VORBIS_INFO
	case 3: // VORBIS_TAGS
	case 4: // VORBIS_BOOK
		r = mkv_vorbis_hdr(&m->mkv_vorbis, &m->mkv_vorbis_data, (ffstr*)&res->frame);
		if (r == 0) {
			if (m->codec_conf_state++ == 4)
				m->codec_conf_state = 0;
			res->frame.pos = ~0ULL;
			res->frame.end_pos = ~0ULL;
			res->frame.duration = ~0U;
			return AVPK_DATA;
		}
		res->error.message = "mkv_vorbis_hdr()";
		res->error.offset = ~0ULL;
		return AVPK_ERROR;
	}

	for (;;) {
		r = mkvread_process(m, input, (ffstr*)&res->frame);
		switch (r) {
		case AVPK_HEADER: {
			const struct mkvread_audio_info *ai;
			for (unsigned i = 0;;  i++) {
				if (!(ai = mkvread_track_info(m, i))) {
					res->error.message = "No audio track";
					res->error.offset = ~0ULL;
					return AVPK_ERROR;
				}
				if (ai->type == MKV_TRK_AUDIO) {
					m->sel_track_id = ai->id;
					m->sel_track_index = i;
					break;
				}
			}
			res->hdr.codec = ai->codec;
			res->hdr.sample_bits = ai->bits;
			res->hdr.sample_rate = ai->sample_rate;
			res->hdr.channels = ai->channels;
			res->hdr.duration = ai->duration_msec * ai->sample_rate / 1000;
			m->a_sample_rate = ai->sample_rate;

			if (ai->codec_conf.len)
				m->codec_conf_state = 1;
			if (ai->codec == AVPKC_VORBIS) {
				m->mkv_vorbis_data = ai->codec_conf;
				m->codec_conf_state = 2;
			}
			break;
		}

		case AVPK_META:
			res->tag.name = *(ffstr*)&m->tagname;
			res->tag.value = m->tagval;
			n = ffszarr_ifindsorted(_vorbistag_str, FF_COUNT(_vorbistag_str), res->tag.name.ptr, res->tag.name.len);
			res->tag.id = (n >= 0) ? _vorbistag_mmtag[n] : 0;
			break;

		case AVPK_DATA:
			if (mkvread_block_trackid(m) != m->sel_track_id)
				continue;

			res->frame.pos = m->curpos * m->a_sample_rate / 1000;
			res->frame.end_pos = ~0ULL;
			res->frame.duration = ~0U;
			break;

		case AVPK_SEEK:
			res->seek_offset = m->off;
			break;

		case AVPK_ERROR:
		case AVPK_WARNING:
			res->error.message = mkvread_error(m);
			res->error.offset = m->off;
			break;
		}

		return r;
	}
}

#undef _MKVR_ERR
#undef _MKVR_ERRSTR

AVPKR_IF_INIT(avpk_mkv, "mkv", AVPKF_MKV, mkvread, mkvread_open2, mkvread_process2, mkvread_seek_s, mkvread_close);
