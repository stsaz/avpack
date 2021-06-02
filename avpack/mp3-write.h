/** avpack: .mp3 writer
2021, Simon Zolin
*/

/*
mp3write_create
mp3write_close
mp3write_process
mp3write_addtag
*/

#pragma once

#include <ffbase/vector.h>
#include <avpack/id3v1.h>
#include <avpack/id3v2.h>

struct _mp3write_tag {
	ffuint mmtag;
	ffstr val;
};

typedef struct mp3write {
	ffuint state;
	ffvec buf;
	ffvec tags; // struct _mp3write_tag[]

	ffuint options; // enum MP3WRITE_OPT
	ffuint id3v2_min_size; // minimum size for ID3v2 tag
} mp3write;

enum MP3WRITE_OPT {
	MP3WRITE_ID3V1 = 1,
	MP3WRITE_ID3V2 = 2,
};

static inline void mp3write_create(mp3write *m)
{
	m->options = 0xff;
	m->id3v2_min_size = 1000;
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
	MP3WRITE_ERROR,
};

enum MP3WRITE_F {
	MP3WRITE_FLAST = 1, // this packet is the last one
};

/**
Return enum MP3WRITE_R */
static inline int mp3write_process(mp3write *m, ffstr *input, ffstr *output, int flags)
{
	enum {
		W_ID3V2, W_DATA, W_ID3V1, W_FIN,
	};

	for (;;) {
		switch (m->state) {
		case W_ID3V2: {
			if (!(m->options & MP3WRITE_ID3V2)) {
				m->state = W_DATA;
				continue;
			}

			struct id3v2write id3 = {};
			id3v2write_create(&id3);

			struct _mp3write_tag *t;
			FFSLICE_WALK(&m->tags, t) {
				int r = id3v2write_add(&id3, t->mmtag, t->val);
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
			m->state = W_DATA;
			return MP3WRITE_DATA;
		}

		case W_DATA:
			if (flags & MP3WRITE_FLAST) {
				m->state = W_ID3V1;
				if (input->len == 0)
					continue;
			}
			if (input->len == 0)
				return MP3WRITE_MORE;

			*output = *input;
			ffstr_shift(input, input->len);
			return MP3WRITE_DATA;

		case W_ID3V1: {
			if (!(m->options & MP3WRITE_ID3V1)) {
				m->state = W_FIN;
				continue;
			}

			if (NULL == ffvec_alloc(&m->buf, sizeof(struct id3v1), 1))
				return MP3WRITE_ERROR;
			struct id3v1 *id3 = (struct id3v1*)m->buf.ptr;
			id3v1write_init(id3);

			struct _mp3write_tag *t;
			FFSLICE_WALK(&m->tags, t) {
				if ((int)t->val.len != id3v1write_set(id3, t->mmtag, t->val))
					{} // value is trimmed
			}

			ffstr_set(output, m->buf.ptr, sizeof(struct id3v1));
			m->state = W_FIN;
			return MP3WRITE_DATA;
		}

		case W_FIN:
			return MP3WRITE_DONE;
		}
	}
}
