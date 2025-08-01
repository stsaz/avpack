/** avpack: .mp3 reader
2021, Simon Zolin
*/

/*
mp3read_open
mp3read_close
mp3read_process
mp3read_seek
mp3read_info
mp3read_cursample
mp3read_error
mp3read_tag
mp3read_offset
*/

/* .mp3 format:
[ID3v2]
MPEG_HDR ((XING LAME) | VBRI)
(MPEG_HDR DATA)...
[APETAG]
[ID3v1]
*/

#pragma once
#include <avpack/mpeg1-read.h>
#include <avpack/id3v1.h>
#include <avpack/id3v2.h>
#include <avpack/apetag.h>

typedef void (*mp3_log_t)(void *udata, const char *fmt, va_list va);

typedef struct mp3read {
	ffuint state, nextstate;
	ffuint gather_size;
	mpeg1read rd;
	ffvec buf;
	ffstr chunk;
	ffuint64 off, total_size;
	ffuint options;

	struct id3v1read id3v1;
	struct id3v2read id3v2;
	struct apetagread apetag;
	ffuint data_off;
	ffuint tag; // enum MMTAG
	ffstr tagname, tagval;

	mp3_log_t log;
	void *udata;
} mp3read;

static inline void _mp3read_log(mp3read *m, const char *fmt, ...)
{
	if (m->log == NULL) return;

	va_list va;
	va_start(va, fmt);
	m->log(m->udata, fmt, va);
	va_end(va);
}

static inline void mp3read_open(mp3read *m, ffuint64 total_size)
{
	m->total_size = total_size;
	id3v2read_open(&m->id3v2);
}

static inline void mp3read_open2(mp3read *m, struct avpk_reader_conf *conf)
{
	mp3read_open(m, conf->total_size);
	m->options = conf->flags;
	m->id3v1.codepage = conf->code_page;
	m->id3v2.codepage = conf->code_page;
	m->log = conf->log;
	m->udata = conf->opaque;
}

static inline void mp3read_close(mp3read *m)
{
	id3v2read_close(&m->id3v2);
	apetagread_close(&m->apetag);
	mpeg1read_close(&m->rd);
	ffvec_free(&m->buf);
}

enum MP3READ_R {
	MP3READ_ID31 = AVPK_META,
	MP3READ_ID32 = AVPK_META,
	MP3READ_APETAG = AVPK_META,
	MP3READ_WARN = AVPK_WARNING,
	MP3READ_DONE = AVPK_FIN,
};

/**
Return enum MPEG1READ_R or enum MP3READ_R */
/* MP3 reading algorithm:
. read ID3v2 tag
. seek to the end
. read ID3v1 tag
. read APE tag
. seek to header
. find MPEG-1/2 header and start reading frames */
static inline int mp3read_process(mp3read *m, ffstr *input, ffstr *output)
{
	enum {
		R_INIT, R_ID3V2, R_GATHER,
		R_FTR_SEEK, R_ID3V1, R_APETAG_FTR, R_APETAG,
		R_HDR_SEEK, R_HDR, R_FRAMES,
	};
	int r;

	for (;;) {
		switch (m->state) {
		case R_INIT:
			if (!input->len)
				return MPEG1READ_MORE;
			if (input->ptr[0] != 'I') {
				m->state = R_HDR;
				continue;
			}
			m->state = R_ID3V2;
			// fallthrough

		case R_ID3V2: {
			ffsize len = input->len;
			r = id3v2read_process(&m->id3v2, input, &m->tagname, &m->tagval);
			m->off += len - input->len;
			switch (r) {
			case ID3V2READ_MORE:
				return MPEG1READ_MORE;
			case ID3V2READ_NO:
				break;
			case ID3V2READ_DONE:
				m->data_off = id3v2read_size(&m->id3v2);
				break;
			case ID3V2READ_ERROR:
				m->state = R_FTR_SEEK;
				// fallthrough
			case ID3V2READ_WARN:
				m->rd.error = id3v2read_error(&m->id3v2);
				return MP3READ_WARN;
			default:
				m->tag = -r;
				return MP3READ_ID32;
			}
		}
			// fallthrough

		case R_FTR_SEEK:
			if (m->options & AVPKR_F_NO_SEEK) {
				m->state = R_HDR;
				continue;
			}

			if (m->data_off + sizeof(struct apetaghdr) + sizeof(struct id3v1) > m->total_size) {
				m->state = R_HDR;
				if (m->total_size != 0 && m->data_off == 0)
					m->state = R_HDR_SEEK; // no or bad ID3v2: some input data was consumed
				continue;
			}
			m->state = R_GATHER,  m->nextstate = R_ID3V1;
			m->gather_size = sizeof(struct apetaghdr) + sizeof(struct id3v1);
			m->off = m->total_size - m->gather_size;
			return MPEG1READ_SEEK;

		case R_GATHER:
			r = ffstr_gather((ffstr*)&m->buf, &m->buf.cap, input->ptr, input->len, m->gather_size, &m->chunk);
			if (r < 0)
				return _MPEG1R_ERR(&m->rd, "not enough memory");
			ffstr_shift(input, r);
			m->off += r;
			if (m->chunk.len == 0)
				return MPEG1READ_MORE;
			m->buf.len = 0;
			m->state = m->nextstate;
			continue;

		case R_ID3V1: {
			ffstr id3;
			ffstr_set(&id3, &m->chunk.ptr[m->chunk.len - sizeof(struct id3v1)], sizeof(struct id3v1));
			r = id3v1read_process(&m->id3v1, id3, &m->tagval);
			switch (r) {
			case ID3V1READ_DONE:
				m->total_size -= sizeof(struct id3v1);
				m->chunk.len -= sizeof(struct id3v1);
				m->off -= sizeof(struct id3v1);
				break;
			case ID3V1READ_NO:
				break;
			default:
				m->tag = -r;
				return MP3READ_ID31;
			}
		}
			// fallthrough

		case R_APETAG_FTR: {
			apetagread_open(&m->apetag);
			ffint64 seek;
			r = apetagread_footer(&m->apetag, m->chunk, &seek);
			switch (r) {
			case APETAGREAD_SEEK:
				m->off += seek;
				m->total_size = m->off;
				m->state = R_APETAG;
				return MPEG1READ_SEEK;
			default:
				m->state = R_HDR_SEEK;
				continue;
			}
			break;
		}

		case R_APETAG: {
			ffsize len = input->len;
			r = apetagread_process(&m->apetag, input, &m->tagname, &m->tagval);
			m->off += len - input->len;
			switch (r) {
			case APETAGREAD_MORE:
				return MPEG1READ_MORE;
			case APETAGREAD_DONE:
				apetagread_close(&m->apetag);
				m->state = R_HDR_SEEK;
				break;
			case APETAGREAD_ERROR:
				m->state = R_HDR_SEEK;
				m->rd.error = apetagread_error(&m->apetag);
				return MP3READ_WARN;
			default:
				m->tag = -r;
				return MP3READ_APETAG;
			}
		}
			// fallthrough

		case R_HDR_SEEK:
			m->state = R_HDR;
			m->off = m->data_off;
			return MPEG1READ_SEEK;

		case R_HDR: {
			ffuint64 stream_size = (m->total_size != 0) ? m->total_size - m->data_off : 0;
			mpeg1read_open(&m->rd, stream_size);
			m->state = R_FRAMES;
		}
			// fallthrough

		case R_FRAMES:
			r = mpeg1read_process(&m->rd, input, output);
			m->off = m->data_off + mpeg1read_offset(&m->rd);
			switch (r) {
			case MPEG1READ_HEADER:
			case MPEG1READ_DATA:
				m->off = m->data_off + mpeg1read_frame_offset(&m->rd);
				break;

			case MPEG1READ_MORE:
				if (m->total_size != 0 && m->off >= m->total_size)
					return MP3READ_DONE;
				break;
			}
			return r;
		}
	}
}

static inline int mp3read_process2(mp3read *m, ffstr *input, union avpk_read_result *res)
{
	int r = mp3read_process(m, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_HEADER:
		ffmem_zero_obj(&res->frame);
		res->hdr.codec = AVPKC_MP3;
		res->hdr.sample_rate = m->rd.info.sample_rate;
		res->hdr.channels = m->rd.info.channels;
		res->hdr.duration = m->rd.info.total_samples;
		res->hdr.audio_bitrate = m->rd.info.bitrate;
		res->hdr.delay = m->rd.info.delay;
		res->hdr.padding = m->rd.info.padding;
		break;

	case AVPK_META:
		res->tag.id = m->tag;
		res->tag.name = m->tagname;
		res->tag.value = m->tagval;
		break;

	case AVPK_DATA:
		res->frame.pos = m->rd.cur_sample;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = mpeg1_samples(m->rd.prev_hdr);
		break;

	case AVPK_SEEK:
		res->seek_offset = m->off;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = m->rd.error;
		res->error.offset = m->off;
		break;
	}
	return r;
}

/**
Return enum MMTAG */
static inline int mp3read_tag(mp3read *m, ffstr *name, ffstr *val)
{
	*name = m->tagname;
	*val = m->tagval;
	return m->tag;
}

static inline const char* mp3read_error(mp3read *m)
{
	return mpeg1read_error(&m->rd);
}

#define mp3read_info(m)  mpeg1read_info(&(m)->rd)

static inline void mp3read_seek(mp3read *m, ffuint64 sample)
{
	return mpeg1read_seek(&m->rd, sample);
}

#define mp3read_cursample(m)  mpeg1read_cursample(&(m)->rd)

#define mp3read_offset(m)  (m)->off

AVPKR_IF_INIT(avpk_mp3, "mp3", AVPKF_MP3, mp3read, mp3read_open2, mp3read_process2, mp3read_seek, mp3read_close);
