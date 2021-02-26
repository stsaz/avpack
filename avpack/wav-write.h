/** avpack: .wav writer
2015,2021, Simon Zolin
*/

/*
wavwrite_create
wavwrite_close
wavwrite_process
wavwrite_error
wavwrite_size
wavwrite_offset
wavwrite_finish
*/

#pragma once

#include <avpack/wav-fmt.h>
#include <ffbase/vector.h>

typedef struct wavwrite {
	ffuint state;
	const char *errmsg;
	ffvec buf;
	ffuint doff;
	ffuint dsize;
	ffuint sampsize;
	ffuint64 off;
	struct wav_info info;
	int fin :1;
} wavwrite;

/**
info.format, sample_rate, channels are required
info.total_samples is optional
*/
static inline int wavwrite_create(wavwrite *w, struct wav_info *info)
{
	w->info = *info;
	return 0;
}

static inline void wavwrite_close(wavwrite *w)
{
	ffvec_free(&w->buf);
}

static inline const char* wavwrite_error(wavwrite *w)
{
	return w->errmsg;
}

#define _WAVW_ERR(w, e) \
	(w)->errmsg = (e), WAVWRITE_ERROR

enum WAVWRITE_R {
	WAVWRITE_DATA,
	WAVWRITE_HEADER,
	WAVWRITE_DONE,
	WAVWRITE_MORE,
	WAVWRITE_SEEK,
	WAVWRITE_ERROR,
};

/**
Return enum WAVWRITE_R */
/*
. Write header
. Write data
. Seek to 0 and finalize header, if necessary
*/
static inline int wavwrite_process(wavwrite *w, ffstr *input, ffstr *output)
{
	enum {
		W_HDR, W_DATA, W_HDRFIN, W_DONE,
	};

	switch (w->state) {
	case W_HDR:
		w->sampsize = (w->info.format&0xff)/8 * w->info.channels;
		w->dsize = (w->info.total_samples != 0)
			? w->info.total_samples * w->sampsize
			: (ffuint)-1;
		w->doff = sizeof(struct wav_chunkhdr) + FFS_LEN("WAVE")
			+ sizeof(struct wav_chunkhdr) + sizeof(struct wav_fmt)
			+ sizeof(struct wav_chunkhdr);
		if (NULL == ffvec_alloc(&w->buf, w->doff, 1))
			return _WAVW_ERR(w, "not enough memory");
		// fallthrough

	case W_HDRFIN: {
		ffuint riff_size = w->doff + w->dsize - sizeof(struct wav_chunkhdr);
		if (w->dsize == (ffuint)-1)
			riff_size = (ffuint)-1;
		char *p = w->buf.ptr;
		p += wav_chunk_write(w->buf.ptr, "RIFF", riff_size);
		p = ffmem_copy(p, "WAVE", 4);

		p += wav_chunk_write(p, "fmt ", sizeof(struct wav_fmt));
		p += wav_fmt_write(p, &w->info);

		p += wav_chunk_write(p, "data", w->dsize);

		ffstr_set(output, w->buf.ptr, p - (char*)w->buf.ptr);

		if (w->state == W_HDRFIN) {
			w->state = W_DONE;
			return WAVWRITE_DATA;
		}

		w->dsize = 0;
		w->state = W_DATA;
		return WAVWRITE_HEADER;
	}

	case W_DONE:
		return WAVWRITE_DONE;

	case W_DATA:
		if (input->len == 0) {
			if (!w->fin)
				return WAVWRITE_MORE;

			if (w->dsize == w->info.total_samples * w->sampsize)
				return WAVWRITE_DONE; // header already has the correct data size

			w->state = W_HDRFIN;
			w->off = 0;
			return WAVWRITE_SEEK;
		}

		if ((ffuint64)w->dsize + input->len > (ffuint)-1)
			return _WAVW_ERR(w, "too large data");

		w->dsize += input->len;
		*output = *input;
		input->len = 0;
		return WAVWRITE_DATA;
	}

	// unreachable
	return WAVWRITE_ERROR;
}

#undef _WAVW_ERR

/** Get output file size.
Call it only after WAVWRITE_HEADER is returned. */
static inline ffuint64 wavwrite_size(wavwrite *w)
{
	if (w->info.total_samples == 0)
		return 0;
	return w->doff + w->info.total_samples * w->sampsize;
}

#define wavwrite_offset(w)  ((w)->off)
#define wavwrite_finish(w)  ((w)->fin = 1)
