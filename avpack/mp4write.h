/** avpack: .mp4 writer
* no video
* 1 track only
* audio codec: AAC
* doesn't support "co64"

2016,2021, Simon Zolin
*/

/*
mp4write_create_aac
mp4write_close
mp4write_addtag
mp4write_process
mp4write_size
mp4write_error
mp4write_finish
mp4write_offset
*/

#pragma once

#include <avpack/mp4-fmt.h>
#include <ffbase/vector.h>

struct mp4_tag {
	ffuint id;
	ffstr val;
};

typedef struct mp4write {
	ffuint state;
	int err;
	ffvec buf;
	ffvec stsz;
	ffvec stco;
	ffuint64 off;
	ffuint stco_off;
	ffuint stsz_off;
	ffuint mdat_off;
	ffuint64 mdat_size;
	ffuint mp4_size;
	const struct mp4_bbox* ctx[10];
	ffuint boxoff[10];
	ffuint ictx;
	struct mp4_aformat fmt;
	struct {
		ffuint nframes;
		ffuint frame_samples;
		ffuint64 total_samples;
		ffuint bitrate;
		ffuint enc_delay;
		ffuint end_padding;
	} info;
	ffuint frameno;
	ffuint64 samples;
	ffuint chunk_frames;
	ffuint chunk_curframe;

	char aconf[64];
	ffuint aconf_len;

	ffvec tags;
	struct {
		ffuint id;
		ffushort num;
		ffushort total;
	} trkn;
	struct mp4_tag *curtag;

	ffuint stream :1; //total length isn't known in advance
	ffuint fin :1;
} mp4write;

struct mp4_info {
	struct mp4_aformat fmt;
	ffstr conf;
	ffuint64 total_samples; //0:use stream writing mode (header after body)
	ffuint frame_samples;
	ffuint enc_delay;
	ffuint bitrate;
};

enum MP4WRITE_E {
	MP4WRITE_EOK,
	MP4WRITE_EMEM,
	MP4WRITE_ELARGE,
	MP4WRITE_ENFRAMES,
	MP4WRITE_ECO64,
};

static inline const char* mp4write_error(mp4write *m)
{
	static const char *const errs[] = {
		"",
		"not enough memory", // MP4WRITE_EMEM
		"too large data", // MP4WRITE_ELARGE
		"trying to add more frames than expected", // MP4WRITE_ENFRAMES
		"co64 output isn't supported", // MP4WRITE_ECO64
	};

	return errs[m->err];
}

#define _MP4W_ERR(m, e) \
	(m)->err = (e),  MP4WRITE_ERROR

static inline void _mp4write_log(const char *fmt, ...)
{
	(void)fmt;
#ifdef AVPACK_DEBUG
	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	printf("mp4write: %.*s\n", (int)s.len, s.ptr);
	ffstr_free(&s);
#endif
}

enum MP4WRITE_R {
	MP4WRITE_DATA,

	/* mp4write_offset() */
	MP4WRITE_SEEK,
	MP4WRITE_MORE,
	MP4WRITE_DONE,

	/* mp4write_error() */
	MP4WRITE_ERROR,
};

/**
Return 0 on success */
static inline int mp4write_create_aac(mp4write *m, struct mp4_info *info)
{
	if (NULL == ffvec_alloc(&m->buf, 4*1024, 1))
		return _MP4W_ERR(m, MP4WRITE_EMEM);
	m->info.total_samples = info->total_samples;
	m->info.frame_samples = info->frame_samples;
	m->info.enc_delay = info->enc_delay;
	m->info.bitrate = info->bitrate;
	m->fmt = info->fmt;
	m->chunk_frames = (m->fmt.rate / 2) / m->info.frame_samples;
	m->stream = (m->info.total_samples == 0);
	if (!m->stream) {
		m->info.total_samples += m->info.enc_delay;
		ffuint64 ts = m->info.total_samples;
		m->info.total_samples = ffint_align_ceil(m->info.total_samples, m->info.frame_samples);
		m->info.end_padding = m->info.total_samples - ts;
		m->info.nframes = m->info.total_samples / m->info.frame_samples;
		m->ctx[0] = &mp4_ctx_global[0];
	} else {
		m->ctx[0] = &mp4_ctx_global_stream[0];
	}

	if (info->conf.len > sizeof(m->aconf))
		return _MP4W_ERR(m, MP4WRITE_ELARGE);
	ffmem_copy(m->aconf, info->conf.ptr, info->conf.len);
	m->aconf_len = info->conf.len;
	return 0;
}

/**
Return 0 on success */
static inline int mp4write_addtag(mp4write *m, ffuint mmtag, ffstr val)
{
	switch (mmtag) {
	case MMTAG_TRACKNO:
	case MMTAG_TRACKTOTAL: {
		ffushort n;
		if (!ffstr_toint(&val, &n, FFS_INT16))
			return 1;
		if (mmtag == MMTAG_TRACKNO)
			m->trkn.num = n;
		else
			m->trkn.total = n;
		m->trkn.id = MMTAG_TRACKNO;
		return 0;
	}
	}

	if (mmtag == MMTAG_DISCNUMBER
		|| NULL == mp4_ilst_find(mmtag))
		return 1;

	struct mp4_tag *t = ffvec_pushT(&m->tags, struct mp4_tag);
	t->id = mmtag;
	ffstr_null(&t->val);
	if (NULL == ffstr_dupstr(&t->val, &val))
		return -1;
	return 0;
}

static void tag_free(struct mp4_tag *t)
{
	ffstr_free(&t->val);
}

static struct mp4_tag* tags_find(struct mp4_tag *t, ffuint cnt, ffuint id)
{
	for (ffuint i = 0;  i != cnt;  i++) {
		if (id == t[i].id)
			return &t[i];
	}
	return NULL;
}

static inline void mp4write_close(mp4write *m)
{
	ffvec_free(&m->buf);
	ffvec_free(&m->stsz);
	ffvec_free(&m->stco);
	FFSLICE_FOREACH_T(&m->tags, tag_free, struct mp4_tag);
	ffslice_free(&m->tags);
}

/** Get approximate output file size */
static inline ffuint64 mp4write_size(mp4write *m)
{
	return 64 * 1024 + (m->info.total_samples / m->fmt.rate + 1) * (m->info.bitrate / 8);
}

/** Return size needed for the box */
static inline ffuint _mp4_boxsize(mp4write *m, const struct mp4_bbox *b)
{
	ffuint t = MP4_GET_TYPE(b->flags), n = MP4_GET_MINSIZE(b->flags);

	switch (t) {
	case BOX_STSC:
	case BOX_STTS:
		n = 64;
		break;

	case BOX_STSZ:
		if (m->stream) {
			n = m->stsz.len;
			break;
		}

		n = mp4_stsz_size(m->info.nframes);
		if (NULL == ffvec_alloc(&m->stsz, n, 1))
			return _MP4W_ERR(m, MP4WRITE_EMEM);
		break;

	case BOX_STCO: {
		if (m->stream) {
			n = m->stco.len;
			break;
		}

		ffuint chunks = m->info.nframes / m->chunk_frames + !!(m->info.nframes % m->chunk_frames);
		n = mp4_stco_size(BOX_STCO, chunks);
		if (NULL == ffvec_alloc(&m->stco, n, 1))
			return _MP4W_ERR(m, MP4WRITE_EMEM);
		break;
	}

	case BOX_ILST_DATA:
		if (m->curtag->id == MMTAG_TRACKNO) {
			n = mp4_ilst_trkn_data_write(NULL, 0, 0);
			break;
		}
		n = mp4_ilst_data_write(NULL, &m->curtag->val);
		break;

	case BOX_ITUNES_MEAN:
		n = FFS_LEN("com.apple.iTunes");
		break;

	case BOX_ITUNES_NAME:
		n = FFS_LEN("iTunSMPB");
		break;

	case BOX_ITUNES_DATA:
		n = mp4_itunes_smpb_write(NULL, 0, 0, 0);
		break;
	}

	return n;
}

/** Write box data.
Return -1 if box should be skipped */
static inline int _mp4_boxdata(mp4write *m, const struct mp4_bbox *b)
{
	ffuint t = MP4_GET_TYPE(b->flags), n, boxoff = m->buf.len, box_data_off;
	struct mp4_fullbox *fbox = NULL;
	void *data = ffslice_end(&m->buf, 1) + sizeof(struct mp4box);

	if (b->flags & F_RO) {
		return -1;
	}

	if (b->flags & F_FULLBOX) {
		fbox = data;
		ffmem_zero(fbox, sizeof(struct mp4_fullbox));
		data = fbox + 1;
	}
	box_data_off = (char*)data - (char*)m->buf.ptr;

	n = MP4_GET_MINSIZE(b->flags);
	if (n != 0)
		ffmem_zero(data, n);

	switch (t) {
	case BOX_FTYP: {
		static const char ftyp_aac[24] = {
			"M4A " "\0\0\0\0" "M4A " "mp42" "isom" "\0\0\0\0"
		};
		n = sizeof(ftyp_aac);
		ffmem_copy(data, ftyp_aac, n);
		break;
	}

	case BOX_MDAT:
		m->mdat_off = boxoff;
		if (m->stream)
			m->state = 10 /*W_MDAT_HDR*/;
		break;

	case BOX_TKHD:
		n = mp4_tkhd_write((void*)fbox, 1, m->info.total_samples);
		break;

	case BOX_MVHD:
		n = mp4_mvhd_write((void*)fbox, m->fmt.rate, m->info.total_samples);
		break;

	case BOX_MDHD:
		n = mp4_mdhd_write((void*)fbox, m->fmt.rate, m->info.total_samples);
		break;

	case BOX_DREF: {
		struct mp4_dref *dref = data;
		*(ffuint*)dref->cnt = ffint_be_cpu32(1);
		break;
	}

	case BOX_DREF_URL:
		ffmem_copy(fbox->flags, "\x00\x00\x01", 3); //"mdat" is in the same file as "moov"
		break;

	case BOX_STSD:
		n = mp4_stsd_write(data);
		break;

	case BOX_HDLR: {
		n = mp4_hdlr_write(data);
		break;
	}

	case BOX_STSD_MP4A:
		n = mp4_afmt_write(data, &m->fmt);
		break;

	case BOX_ESDS: {
		struct mp4_acodec ac = {
			.type = MP4_ESDS_DEC_MPEG4_AUDIO,
			.stm_type = 0x15,
			.avg_brate = m->info.bitrate,
			.conf = m->aconf,  .conflen = m->aconf_len,
		};
		n = mp4_esds_write(data, &ac);
		break;
	}

	case BOX_STSC:
		n = mp4_stsc_write(data, m->info.total_samples, m->info.frame_samples, m->fmt.rate / 2);
		break;

	case BOX_STTS:
		n = mp4_stts_write(data, m->info.total_samples, m->info.frame_samples);
		break;

	case BOX_STSZ:
		if (m->stream) {
			ffmem_copy(data, m->stsz.ptr, m->stsz.len);
			n = m->stsz.len;
			break;
		}

		ffmem_zero(data, m->stsz.cap);
		ffmem_zero(m->stsz.ptr, m->stsz.cap);
		n = m->stsz.cap;
		m->stsz_off = box_data_off;
		break;

	case BOX_STCO:
		if (m->stream) {
			ffmem_copy(data, m->stco.ptr, m->stco.len);
			n = m->stco.len;
			break;
		}

		ffmem_zero(data, m->stco.cap);
		ffmem_zero(m->stco.ptr, m->stco.cap);
		n = m->stco.cap;
		m->stco_off = box_data_off;
		break;

	case BOX_ILST_DATA:
		if (m->curtag->id == MMTAG_TRACKNO) {
			n = mp4_ilst_trkn_data_write(data, m->trkn.num, m->trkn.total);
			break;
		}
		n = mp4_ilst_data_write(data, &m->curtag->val);
		break;

	case BOX_ITUNES_MEAN:
		n = _ffs_copyz(data, -1, "com.apple.iTunes");
		break;

	case BOX_ITUNES_NAME:
		n = _ffs_copyz(data, -1, "iTunSMPB");
		break;

	case BOX_ITUNES_DATA:
		n = mp4_itunes_smpb_write(data, m->info.total_samples, m->info.enc_delay, m->info.end_padding);
		break;

	default:
		if (t >= _BOX_TAG) {
			ffuint tag = t - _BOX_TAG;
			if (tag == 0)
				return -1;
			m->curtag = tags_find((void*)m->tags.ptr, m->tags.len, tag);

			if (tag == MMTAG_TRACKNO && (m->trkn.num != 0 || m->trkn.total != 0))
				m->curtag = (void*)&m->trkn;

			if (m->curtag == NULL)
				return -1;
		}
	}

	return n;
}

static inline void mp4write_finish(mp4write *m)
{
	m->fin = 1;
}

static inline ffuint64 mp4write_offset(mp4write *m)
{
	return m->off;
}

/**
Return enum MP4WRITE_R */
/* MP4 writing algorithm:
If total data length is known in advance:
  . Write MP4 header: "ftyp", "moov" with empty "stco" & "stsz", "mdat" box header.
  . Pass audio frames data and fill "stco" & "stsz" data buffers.
  . After all frames are written, seek back to header and write "stsz" data.
  . Seek to "stco" and write its data.
  . Seek to "mdat" and write its size.
else:
  . Write "ftyp", "mdat" box header with 0 size.
  . Pass audio frames data and fill "stco" & "stsz" data buffers.
  . After all frames are written, write "moov".
  . Seek back to "mdat" and write its size.
*/
static inline int mp4write_process(mp4write *m, ffstr *input, ffstr *output)
{
	enum {
		W_META, W_META_NEXT, W_DATA1, W_DATA, W_MORE, W_STSZ, W_STCO_SEEK, W_STCO, W_DONE,
		W_MDAT_HDR=10, W_MDAT_SEEK, W_MDAT_SIZE, W_STM_DATA,
	};

	for (;;) {
		switch (m->state) {

		case W_META_NEXT:
			if (m->ctx[m->ictx]->flags & F_LAST) {
				if (m->ictx == 0) {
					m->mp4_size += m->buf.len;
					ffstr_set2(output, &m->buf);
					m->buf.len = 0;
					m->off += output->len;
					m->state = (m->stream) ? W_MDAT_SEEK : W_DATA1;
					return MP4WRITE_DATA;
				}

				m->ctx[m->ictx--] = NULL;

				struct mp4box *b = (struct mp4box*)(m->buf.ptr + m->boxoff[m->ictx]);
				*(ffuint*)b->size = ffint_be_cpu32(m->buf.len - m->boxoff[m->ictx]);
				continue;
			}
			m->ctx[m->ictx]++;
			m->state = W_META;
			// fallthrough

		case W_META: {
			const struct mp4_bbox *b = m->ctx[m->ictx];
			char *box;
			ffuint n;

			// determine box size and reallocate m->buf
			n = _mp4_boxsize(m, b);
			n += sizeof(struct mp4box) + sizeof(struct mp4_fullbox);
			if (NULL == ffvec_grow(&m->buf, n, 1))
				return _MP4W_ERR(m, MP4WRITE_EMEM);

			m->boxoff[m->ictx] = m->buf.len;
			n = _mp4_boxdata(m, b);
			if ((int)n == -1) {
				m->state = W_META_NEXT;
				continue;
			}

			box = ffslice_end(&m->buf, 1);
			if (b->flags & F_FULLBOX)
				m->buf.len += mp4_fbox_write(b->type, box, n);
			else
				m->buf.len += mp4_box_write(b->type, box, n);

			if (b->ctx != NULL) {
				FF_ASSERT(m->ictx != FF_COUNT(m->ctx));
				m->ctx[++m->ictx] = &b->ctx[0];
			} else if (m->state == W_META) { // state may be changed in _mp4_boxdata()
				m->state = W_META_NEXT;
			}
			break;
		}

		case W_MDAT_HDR:
			if (NULL == ffvec_alloc(&m->stsz, mp4_stsz_size(0), 1)
				|| NULL == ffvec_alloc(&m->stco, mp4_stco_size(BOX_STCO, 0), 1))
				return _MP4W_ERR(m, MP4WRITE_EMEM);
			ffmem_zero(m->stsz.ptr, m->stsz.cap);
			m->stsz.len = m->stsz.cap;
			ffmem_zero(m->stco.ptr, m->stco.cap);
			m->stco.len = m->stco.cap;

			m->state = W_STM_DATA;
			m->mp4_size += m->buf.len;
			ffstr_set2(output, &m->buf);
			m->buf.len = 0;
			m->off += output->len;
			return MP4WRITE_DATA;

		case W_MDAT_SEEK:
			m->state = W_MDAT_SIZE;
			m->off = m->mdat_off;
			return MP4WRITE_SEEK;

		case W_MDAT_SIZE:
			m->buf.len = 0;
			mp4_box_write("mdat", m->buf.ptr, m->mdat_size);
			ffstr_set(output, m->buf.ptr, sizeof(struct mp4box));
			m->state = W_DONE;
			return MP4WRITE_DATA;

		case W_STM_DATA:
			if (m->fin) {
				m->info.total_samples = m->frameno * m->info.frame_samples;
				m->buf.len = 0;
				m->state = W_META_NEXT;
				continue;
			} else if (input->len == 0)
				return MP4WRITE_MORE;

			if (NULL == ffvec_grow(&m->stsz, sizeof(int), 1)
				|| NULL == ffvec_grow(&m->stco, sizeof(int), 1))
				return _MP4W_ERR(m, MP4WRITE_EMEM);
			goto frame;

		case W_DATA1:
			ffvec_free(&m->buf);
			FFSLICE_FOREACH_T(&m->tags, tag_free, struct mp4_tag);
			ffslice_free(&m->tags);
			m->state = W_DATA;
			// fallthrough

		case W_DATA:
			if (m->fin) {
				if (m->frameno != m->info.nframes)
					return _MP4W_ERR(m, MP4WRITE_ENFRAMES);
				m->state = W_STSZ;
				m->off = m->stsz_off;
				return MP4WRITE_SEEK;
			} else if (input->len == 0)
				return MP4WRITE_MORE;

			if (m->frameno == m->info.nframes)
				return _MP4W_ERR(m, MP4WRITE_ENFRAMES);

frame:
			m->stsz.len = mp4_stsz_add(m->stsz.ptr, input->len);
			m->frameno++;

			if (m->chunk_curframe == 0) {
				if (m->off > (ffuint)-1)
					return _MP4W_ERR(m, MP4WRITE_ECO64);
				m->stco.len = mp4_stco_add(m->stco.ptr, BOX_STCO, m->off);
			}
			m->chunk_curframe = (m->chunk_curframe + 1) % m->chunk_frames;

			_mp4write_log("fr#%u  pos:%U  size:%L  off:%U"
				, m->frameno - 1, m->samples, input->len, m->off);

			m->samples += m->info.frame_samples;
			m->off += input->len;
			m->mdat_size += input->len;
			*output = *input;
			input->len = 0;
			m->state = W_MORE;
			return MP4WRITE_DATA;

		case W_MORE:
			m->state = (m->stream) ? W_STM_DATA : W_DATA;
			return MP4WRITE_MORE;

		case W_STSZ:
			ffstr_set2(output, &m->stsz);
			m->state = W_STCO_SEEK;
			return MP4WRITE_DATA;

		case W_STCO_SEEK:
			m->state = W_STCO;
			m->off = m->stco_off;
			return MP4WRITE_SEEK;

		case W_STCO:
			ffstr_set2(output, &m->stco);
			m->state = W_MDAT_SEEK;
			return MP4WRITE_DATA;

		case W_DONE:
			return MP4WRITE_DONE;

		default:
			FF_ASSERT(0);
			return MP4WRITE_ERROR;
		}
	}
}

#undef _MP4W_ERR
