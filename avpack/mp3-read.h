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

typedef struct mp3read {
	ffuint state, nextstate;
	ffuint gather_size;
	mpeg1read rd;
	ffvec buf;
	ffstr chunk;
	ffuint64 off, total_size;

	struct id3v1read id3v1;
	struct id3v2read id3v2;
	struct apetagread apetag;
	ffuint data_off;
	ffuint tag; // enum MMTAG
	ffstr tagname, tagval;
} mp3read;

static inline void mp3read_open(mp3read *m, ffuint64 total_size)
{
	m->total_size = total_size;
	id3v2read_open(&m->id3v2);
}

static inline void mp3read_close(mp3read *m)
{
	id3v2read_close(&m->id3v2);
	apetagread_close(&m->apetag);
	mpeg1read_close(&m->rd);
	ffvec_free(&m->buf);
}

enum MP3READ_R {
	MP3READ_ID31 = 100,
	MP3READ_ID32,
	MP3READ_APETAG,
	MP3READ_WARN,
	MP3READ_DONE,
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
		R_ID3V2, R_GATHER,
		R_FTR_SEEK, R_ID3V1, R_APETAG_FTR, R_APETAG,
		R_HDR_SEEK, R_HDR, R_FRAMES,
	};
	int r;

	for (;;) {
		switch (m->state) {
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
				m->data_off = m->off;
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

		case R_HDR:
			mpeg1read_open(&m->rd, m->total_size - m->data_off);
			m->state = R_FRAMES;
			// fallthrough

		case R_FRAMES:
			r = mpeg1read_process(&m->rd, input, output);
			m->off = m->data_off + mpeg1read_offset(&m->rd);
			switch (r) {
			case MPEG1READ_MORE:
				if (m->total_size != 0 && m->off >= m->total_size)
					return MP3READ_DONE;
				break;
			}
			return r;
		}
	}
}

/**
Return enum MMTAG */
static inline int mp3read_tag(mp3read *m, ffstr *name, ffstr *val)
{
	*name = m->tagname;
	*val = m->tagval;
	return m->tag;
}

#define mp3read_error(m)  mpeg1read_error(&(m)->rd)

#define mp3read_info(m)  mpeg1read_info(&(m)->rd)

#define mp3read_seek(m, sample)  mpeg1read_seek(&(m)->rd, sample)

#define mp3read_cursample(m)  mpeg1read_cursample(&(m)->rd)

#define mp3read_offset(m)  (m)->off
