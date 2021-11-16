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

#include <avpack/mkv-fmt.h>
#include <avpack/shared.h>

struct mkv_el {
	int id; // enum MKV_ELID
	ffuint flags;
	ffuint usemask;
	ffuint prio;
	ffuint64 size;
	const struct mkv_binel *ctx;
};

typedef void (*mkv_log_t)(void *udata, ffstr msg);

struct mkvread_audio_info {
	int type; // enum MKV_TRKTYPE
	ffuint codec; // enum MKV_CODEC
	int id;

	ffuint bits;
	ffuint channels;
	ffuint sample_rate;
	ffuint64 duration_msec;
	ffstr codec_conf;
};

struct mkvread_video_info {
	int type; // enum MKV_TRKTYPE
	ffuint codec; // enum MKV_CODEC
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
	ffuint el_hdrsize;
	ffuint64 el_off;

	ffuint gstate;
	ffuint gsize;
	ffvec buf;
	ffstr gbuf;

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

	// seeking:
	struct _mkvread_seekpoint seekpt_glob[2];
	struct _mkvread_seekpoint seekpt[2];
	ffuint64 seek_msec;
	ffuint64 last_seek_off;
	ffuint seek_block;
	ffuint64 segment_size;
	ffuint64 clust_off;

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

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	m->log(m->udata, s);
	ffstr_free(&s);
}

static int mkv_el_info(mkvread *m, struct mkv_el *el, const struct mkv_binel *ctx, const void *data, ffuint level, ffuint64 off)
{
	ffuint64 id;
	int r = mkv_varint(data, -1, &id);

	el->id = MKV_T_UKN;
	if (r <= 0 || id > 0x1fffffff)
		return -1;

	r = mkv_varint((char*)data + r, -2, &el->size);
	if (r <= 0)
		return -1;

	r = mkv_el_find(ctx, (ffuint)id);
	if (r >= 0) {
		el->id = ctx[r].flags & MKV_MASK_ELID;
		el->flags = ctx[r].flags & ~MKV_MASK_ELID;
		el->ctx = ctx[r].children;
	}

	_mkvread_log(m, "%*c%xu  size:%u  offset:%xU  %s"
		, (ffsize)level, ' ', (ffuint)id, el->size, off, (r < 0) ? "?" : "");
	return r;
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
	MKVREAD_DATA,
	MKVREAD_MORE,
	MKVREAD_HEADER,
	MKVREAD_SEEK,
	MKVREAD_DONE,
	MKVREAD_TAG,
	MKVREAD_ERROR,
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
	m->els[m->ictx].ctx = mkv_ctx_global;
	if (NULL == ffvec_alloc(&m->buf, 4096, 1))
		return -1;
	return 0;
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
	ffvec_free(&m->buf);
	ffstr_free(&m->codec_data);
	ffvec_free(&m->lacing);
	ffvec_free(&m->tagname);
}

static inline void mkvread_seek(mkvread *m, ffuint64 msec)
{
	m->seek_msec = msec;
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
	struct mkv_el *parent = &m->els[m->ictx];
	FF_ASSERT(m->ictx != FF_COUNT(m->els));
	m->ictx++;
	struct mkv_el *el = &m->els[m->ictx];
	m->el_off = m->off - m->gbuf.len;
	int r = mkv_el_info(m, el, parent->ctx, m->gbuf.ptr, m->ictx - 1, m->el_off);

	if (m->el_hdrsize + el->size > parent->size)
		return _MKVR_ERRSTR(m, "too large element");
	parent->size -= m->el_hdrsize + el->size;

	if (el->id == MKV_T_UKN) {
		return 0xbad;
	}

	if ((ffuint)r < 32 && ffbit_set32(&parent->usemask, r)
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
		el->size = 0;
		return 0xfeed;
	}

	m->gbuf.len = 0;
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

	if (el->flags & (MKV_F_INT | MKV_F_INT8)) {
		if (0 != mkv_int_ntoh(&val, m->gbuf.ptr, m->gbuf.len))
			return _MKVR_ERR(m, MKV_EINTVAL);

		if ((el->flags & MKV_F_INT) && val > 0xffffffff)
			return _MKVR_ERR(m, MKV_EINTVAL);

		val4 = val;
	}

	if (el->flags & MKV_F_FLT) {
		if (0 != mkv_flt_ntoh(&fval, m->gbuf.ptr, m->gbuf.len))
			return _MKVR_ERR(m, MKV_EFLTVAL);
	}

	switch (el->id) {
	case MKV_T_VER:
		if (val4 != 1)
			return _MKVR_ERRSTR(m, "unsupported EBML version");
		break;

	case MKV_T_DOCTYPE:
		_mkvread_log(m, "doctype: %S", &m->gbuf);
		break;


	case MKV_T_SCALE:
		m->scale = val4;  break;

	case MKV_T_DUR:
		m->dur = fval;  break;

	case MKV_T_TITLE:
		m->tagval = m->gbuf;  break;


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
		_mkvread_log(m, "codec: %S", &m->gbuf);
		if (0 != (r = mkv_codec(m->gbuf))) {
			m->curtrack->audio.codec = r;
		}
		break;

	case MKV_T_CODEC_PRIV:
		ffstr_free(&m->codec_data);
		if (NULL == ffstr_dup2(&m->codec_data, &m->gbuf))
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
		ffstr_dup2((ffstr*)&m->tagname, &m->gbuf);
		break;

	case MKV_T_TAG_VAL:
	case MKV_T_TAG_BVAL:
		ffstr_set2(&m->tagval, &m->gbuf);
		break;


	case MKV_T_CLUST:
		if (m->seekpt_glob[0].off == 0) {
			const struct mkv_el *parent = &m->els[m->ictx - 1];
			m->segment_size = parent->size - (m->el_hdrsize + el->size);
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

		r = _mkvread_block(m, &m->gbuf);

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

		ffstr_set2(output, &m->gbuf);
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
			// reset Segment element's size, otherwise we'll get the error "child element is too large"
			m->els[m->ictx].size = m->segment_size;
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

/** Find Cluster element */
static int _mkvr_seek_sync(mkvread *m, ffstr *input, ffstr *output)
{
	int r, pos;
	ffstr chunk;
	for (;;) {

		r = _avpack_gather_header(&m->buf, *input, 4, &chunk);
		ffstr_shift(input, r);
		m->off += r;

		if (chunk.len == 0) {
			ffstr_null(output);
			return 0xfeed;
		}

		pos = ffstr_find(&chunk, "\x1f\x43\xb6\x75", 4);
		if (pos >= 0)
			break;

		r = _avpack_gather_trailer(&m->buf, *input, 4, r);
		// r<0: ffstr_shift() isn't suitable due to assert()
		input->ptr += r;
		input->len -= r;
		m->off += r;
	}

	if (chunk.ptr == input->ptr) {
		ffstr_shift(input, 4 + pos);
		m->off += 4 + pos;
	}
	ffstr_set(output, &chunk.ptr[pos], 4);
	if (m->buf.len != 0) {
		ffstr_erase_left((ffstr*)&m->buf, pos);
		ffstr_set(output, m->buf.ptr, 4);
	}
	return 0;
}

/** Process Cluster.Time data while seeking */
static int _mkvr_seek_process_data(mkvread *m, ffstr data)
{
	// cluster.id, cluster.size, time.id, time.size, time.value

	m->clust_off = m->off - data.len;
	ffstr_shift(&data, 4);

	ffuint64 val = 0;
	if (mkv_varint_shift(&data, &val) < 0)
		return -1;
	data.len = ffmin(val, data.len);

	if (data.len < 1 || (ffbyte)data.ptr[0] != 0xe7)
		return -1;
	ffstr_shift(&data, 1);

	if (mkv_varint_shift(&data, &val) < 0 || val > data.len)
		return -1;
	data.len = val;

	if (0 != mkv_int_ntoh(&val, data.ptr, data.len))
		return -1;
	m->clust_time = val;
	return 0;
}

/** Adjust the search window after the current offset has become too large */
static int _mkvr_seek_adjust_edge(mkvread *m, struct _mkvread_seekpoint *sp)
{
	if (m->off >= sp[1].off) {
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
	return 0;
}

/** Adjust the search window */
static int _mkvr_seek_adjust(mkvread *m, struct _mkvread_seekpoint *sp)
{
	_mkvread_log(m, "seek: tgt:%xU cur:%xU [%xU..%xU](%xU)  off:%xU [%xU..%xU](%xU)"
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
. gather data for element id
. gather data for element size
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
		R_ELID1, R_ELSIZE1, R_ELSIZE, R_EL,
		R_LACING=4,
		R_NEXTCHUNK, R_SKIP,
		R_SEEK_INIT=7, R_SEEK, R_SEEK_SYNC, R_SEEK_DATA, R_SEEK_DONE,
		R_GATHER, R_GATHER_MORE,
	};
	int r;

	for (;;) {
		switch (m->state) {

		case R_GATHER_MORE:
			if (m->buf.ptr == m->gbuf.ptr) {
				m->buf.len = m->gbuf.len;
			} else {
				if (m->gbuf.len != ffvec_add2T(&m->buf, &m->gbuf, char))
					return _MKVR_ERR(m, MKV_EMEM);
			}
			m->state = R_GATHER;
			// fallthrough
		case R_GATHER:
			r = ffstr_gather((ffstr*)&m->buf, &m->buf.cap, input->ptr, input->len, m->gsize, &m->gbuf);
			if (r < 0)
				return _MKVR_ERR(m, MKV_EMEM);
			ffstr_shift(input, r);
			m->off += r;
			if (m->gbuf.len == 0)
				return MKVREAD_MORE;
			m->buf.len = 0;
			m->state = m->gstate;
			continue;


		case R_SKIP: {
			struct mkv_el *el = &m->els[m->ictx];
			if (el->size > input->len) {
				m->off += el->size;
				el->size = 0;
				m->state = R_NEXTCHUNK;
				return MKVREAD_SEEK;
			}
			r = ffmin(el->size, input->len);
			ffstr_shift(input, r);
			el->size -= r;
			m->off += r;
			if (el->size != 0)
				return MKVREAD_MORE;

			m->state = R_NEXTCHUNK;
		}
			// fallthrough

		case R_NEXTCHUNK: {
			struct mkv_el *el = &m->els[m->ictx];
			if (el->size == 0) {
				if (-1 != (r = _mkvread_el_close(m)))
					return r;
				continue;
			}

			m->state = R_ELID1;
			continue;
		}

		case R_ELID1:
			if (input->len == 0)
				return MKVREAD_MORE;
			r = mkv_varint(input->ptr, 1, NULL);
			if (r == -1)
				return _MKVR_ERRSTR(m, "bad ID");
			m->gsize = r + 1;
			m->state = R_GATHER;  m->gstate = R_ELSIZE1;
			continue;

		case R_ELSIZE1:
			r = mkv_varint(m->gbuf.ptr, 1, NULL);
			m->el_hdrsize = r;
			r = mkv_varint(m->gbuf.ptr + r, 1, NULL);
			if (r == -1)
				return _MKVR_ERRSTR(m, "bad size");
			m->el_hdrsize += r;
			if (r != 1) {
				m->gsize = m->el_hdrsize;
				m->state = R_GATHER_MORE,  m->gstate = R_ELSIZE;
				continue;
			}
			m->state = R_ELSIZE;
			// fallthrough

		case R_ELSIZE:
			r = _mkvread_el_hdr(m);
			switch (r) {
			case 0xbad:
				m->state = R_SKIP;  break;

			case 0xfeed:
				m->state = R_GATHER;  m->gstate = R_EL;  break;

			case 0xd0:
				m->state = R_EL;  break;

			case MKVREAD_ERROR:
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
			ffmem_copy(m->seekpt, m->seekpt_glob, sizeof(m->seekpt));
			// fallthrough
		case R_SEEK:
			m->off = _mkvr_seek_offset(m->seekpt, m->seek_msec);
			if (m->off == m->last_seek_off)
				return _MKVR_ERRSTR(m, "seek error");
			m->last_seek_off = m->off;
			m->buf.len = 0;
			m->state = R_SEEK_SYNC;
			return MKVREAD_SEEK;

		case R_SEEK_SYNC: {
			r = _mkvr_seek_sync(m, input, &m->gbuf);

			int r2 = _mkvr_seek_adjust_edge(m, m->seekpt);
			if (r2 == 0xdeed) {
				m->state = R_SEEK_DONE;
				continue;
			} else if (r2 == MKVREAD_SEEK)
				return MKVREAD_SEEK;

			if (r == 0xfeed)
				return MKVREAD_MORE;

			m->gsize = 4+8+1+1+8; // cluster.id, cluster.size, time.id, time.size, time.value
			m->state = R_GATHER_MORE;  m->gstate = R_SEEK_DATA;
			continue;
		}

		case R_SEEK_DATA:
			r = _mkvr_seek_process_data(m, m->gbuf);
			if (r == -1) {
				m->buf.len = 0;
				m->off = m->off - m->gbuf.len + 1;
				m->last_seek_off = m->off;
				m->state = R_SEEK_SYNC;
				return MKVREAD_SEEK;
			}

			if (!_mkvr_seek_adjust(m, m->seekpt)) {
				m->state = R_SEEK;
				continue;
			}
			// fallthrough

		case R_SEEK_DONE:
			_mkvr_cluster_exit(m);
			m->seek_block = 1;
			m->buf.len = 0;
			m->last_seek_off = 0;
			m->off = m->seekpt[0].off;
			ffmem_zero(m->seekpt, sizeof(m->seekpt));
			m->state = R_ELID1;
			return MKVREAD_SEEK;

		}
	}
}

#undef _MKVR_ERR
#undef _MKVR_ERRSTR
