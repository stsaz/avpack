/** avpack: .mp3 writer
2021, Simon Zolin
*/

/*
mp3write_create
mp3write_close
mp3write_addtag
mp3write_process
mp3write_offset
*/

#pragma once

#include <ffbase/vector.h>
#include <avpack/id3v1.h>
#include <avpack/id3v2.h>
#include <avpack/mpeg1-fmt.h>

struct _mp3write_tag {
	ffuint mmtag;
	ffstr val;
};

typedef struct mp3write {
	ffuint state;
	ffvec buf;
	ffvec tags; // struct _mp3write_tag[]
	ffuint data_off;
	ffbyte frame1[4];
	ffuint nframes;
	ffuint nbytes;

	int vbr_scale; // -1:CBR; VBR:100(worst)..0(best)
	ffuint options; // enum MP3WRITE_OPT
	ffuint id3v2_min_size; // minimum size for ID3v2 tag
} mp3write;

enum MP3WRITE_OPT {
	MP3WRITE_ID3V1 = 1,
	MP3WRITE_ID3V2 = 2,
	MP3WRITE_XINGTAG = 4, // write custom Xing tag (incompatible with MP3WRITE_FLAMEFRAME)
};

static inline void mp3write_create(mp3write *m)
{
	m->options = MP3WRITE_ID3V1 | MP3WRITE_ID3V2;
	m->id3v2_min_size = 1000;
	m->vbr_scale = -1;
}

static inline void mp3write_close(mp3write *m)
{
	struct _mp3write_tag *t;
	FFSLICE_WALK(&m->tags, t) {
		ffstr_free(&t->val);
	}
	ffvec_free(&m->tags);
	ffvec_free(&m->buf);
}

/**
mmtag: enum MMTAG
Return 0 on success */
static inline int mp3write_addtag(mp3write *m, ffuint mmtag, ffstr val)
{
	struct _mp3write_tag *t;
	if (NULL == (t = ffvec_pushT(&m->tags, struct _mp3write_tag)))
		return -1;
	ffmem_zero_obj(t);
	t->mmtag = mmtag;
	if (NULL == ffstr_dupstr(&t->val, &val))
		return -1;
	return 0;
}

enum MP3WRITE_R {
	MP3WRITE_MORE,
	MP3WRITE_DATA,
	MP3WRITE_DONE,
	MP3WRITE_SEEK,
	MP3WRITE_ERROR,
};

enum MP3WRITE_F {
	MP3WRITE_FLAST = 1, // this packet is the last one
	MP3WRITE_FLAMEFRAME = 2, // this packet is LAME frame
};

/**
Return enum MP3WRITE_R */
/* .mp3 write algorithm:
. Write ID3v2 tag
. Write frames
  . When MP3WRITE_FLAST is set: write the current frame and enter the ID3v1-writing state
  . When MP3WRITE_FLAMEFRAME is set: enter the ID3v1-writing state
. Write ID3v1 tag
. If MP3WRITE_XINGTAG is set: seek to the first frame, write Xing tag;  done
. If MP3WRITE_FLAMEFRAME is set: seek to the first frame, write LAME frame
*/
static inline int mp3write_process(mp3write *m, ffstr *input, ffstr *output, int flags)
{
	enum {
		W_ID3V2, W_DATA1, W_DATA, W_ID3V1, W_FRAME1_SEEK, W_XING, W_LAME, W_FIN,
	};
	int r;

	for (;;) {
		switch (m->state) {
		case W_ID3V2: {
			if (!(m->options & MP3WRITE_ID3V2)) {
				m->state = W_DATA1;
				continue;
			}

			struct id3v2write id3 = {};
			id3v2write_create(&id3);

			struct _mp3write_tag *t;
			FFSLICE_WALK(&m->tags, t) {
				int r = id3v2write_add(&id3, t->mmtag, t->val, 0);
				if (r < 0)
					return MP3WRITE_ERROR; // not enough memory
				else if (r > 0)
					{} // field not supported
			}

			ffuint padding = 0;
			if (id3.buf.len < m->id3v2_min_size)
				padding = m->id3v2_min_size - id3.buf.len;
			id3v2write_finish(&id3, padding);

			ffvec_free(&m->buf);
			m->buf = id3.buf;
			ffvec_null(&id3.buf);
			id3v2write_close(&id3);
			ffstr_setstr(output, &m->buf);
			m->state = W_DATA1;
			m->data_off = output->len;
			return MP3WRITE_DATA;
		}

		case W_DATA1:
			m->state = W_DATA;
			if (m->options & MP3WRITE_XINGTAG) {
				if (input->len < 4)
					return MP3WRITE_ERROR; // bad input data
				char h[4];
				*(ffuint*)h = *(ffuint*)input->ptr;
				h[2] = 0x90 | (h[2] & 0x0f); // bitrate=128
				ffuint n = mpeg1_size(h);
				if (NULL == ffvec_realloc(&m->buf, n, 1))
					return MP3WRITE_ERROR; // not enough memory
				ffmem_zero(m->buf.ptr, n);
				ffmem_copy(m->buf.ptr, h, 4);
				ffmem_copy(m->frame1, h, 4);
				ffstr_set(output, m->buf.ptr, n);
				return MP3WRITE_DATA;
			}
			// fallthrough

		case W_DATA:
			if (flags & MP3WRITE_FLAST) {
				m->state = W_ID3V1;
				if (input->len == 0)
					continue;
			}
			if (flags & MP3WRITE_FLAMEFRAME) {
				m->state = W_ID3V1;
				continue;
			}
			if (input->len == 0)
				return MP3WRITE_MORE;

			*output = *input;
			ffstr_shift(input, input->len);
			m->nframes++;
			m->nbytes += output->len;
			return MP3WRITE_DATA;

		case W_ID3V1: {
			if (!(m->options & MP3WRITE_ID3V1)) {
				m->state = W_FRAME1_SEEK;
				continue;
			}

			if (NULL == ffvec_realloc(&m->buf, sizeof(struct id3v1), 1))
				return MP3WRITE_ERROR;
			struct id3v1 *id3 = (struct id3v1*)m->buf.ptr;
			id3v1write_init(id3);

			struct _mp3write_tag *t;
			FFSLICE_WALK(&m->tags, t) {
				if ((int)t->val.len != id3v1write_set(id3, t->mmtag, t->val))
					{} // value is trimmed
			}

			ffstr_set(output, m->buf.ptr, sizeof(struct id3v1));
			m->state = W_FRAME1_SEEK;
			return MP3WRITE_DATA;
		}

		case W_FRAME1_SEEK:
			if (m->options & MP3WRITE_XINGTAG) {
				m->state = W_XING;
				return MP3WRITE_SEEK;
			}
			if (flags & MP3WRITE_FLAMEFRAME) {
				m->state = W_LAME;
				return MP3WRITE_SEEK;
			}
			m->state = W_FIN;
			continue;

		case W_LAME:
			ffstr_setstr(output, input);
			m->state = W_FIN;
			return MP3WRITE_DATA;

		case W_XING: {
			struct mpeg1_info info = {};
			info.frames = m->nframes;
			info.bytes = m->nbytes;
			info.vbr_scale = m->vbr_scale;
			ffmem_zero(m->buf.ptr, ffmin(m->buf.cap, 2048));
			ffmem_copy(m->buf.ptr, m->frame1, 4);
			r = mpeg1_xing_write(&info, m->buf.ptr);
			ffstr_set(output, m->buf.ptr, r);
			m->state = W_FIN;
			return MP3WRITE_DATA;
		}

		case W_FIN:
			return MP3WRITE_DONE;
		}
	}
}

static inline ffuint64 mp3write_offset(mp3write *m)
{
	return m->data_off;
}

#define mp3write_frames(m)  (m)->nframes
