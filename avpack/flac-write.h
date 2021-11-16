/** avpack: .flac writer
2015,2021, Simon Zolin
*/

/*
flacwrite_create flacwrite_close
flacwrite_addtag
flacwrite_pic
flacwrite_process
flacwrite_error
flacwrite_finish
flacwrite_offset
*/

/* format:
fLaC INFO VORBIS_TAGS [PADDING] [PICTURE] [SEEKTABLE]
(FRAME)...
*/

#pragma once

#include <avpack/flac-fmt.h>
#include <avpack/vorbistag.h>
#include <ffbase/vector.h>

typedef struct flacwrite {
	ffuint state;
	const char *error;
	ffvec buf;
	ffuint64 off;
	struct flac_info info;

	vorbistagwrite vtag;
	struct flac_picinfo picinfo;
	ffstr picdata;
	ffuint min_meta; // minimum size of meta data (add padding block if needed)
	ffuint64 hdrlen;

	ffuint64 nsamples, total_samples;
	ffuint seektable_interval; // interval (in samples) for seek table.  Default: 1 sec.  0=disabled.
	ffuint iskpt;
	struct flac_seektab sktab;
	ffuint64 frlen;
	ffuint seektab_off;

	ffuint frames_len;
	int fin :1;
	int outdev_seekable :1;
} flacwrite;

/**
total_samples: (Optional) if set, allows writing seek table */
static inline void flacwrite_create(flacwrite *f, const struct flac_info *info, ffuint64 total_samples)
{
	f->info = *info;
	f->min_meta = 1000;
	f->seektable_interval = (ffuint)-1;
	f->total_samples = total_samples;
	f->outdev_seekable = 1;
}

static inline void flacwrite_close(flacwrite *f)
{
	vorbistagwrite_destroy(&f->vtag);
	ffvec_free(&f->buf);
}

#define _FLACW_ERR(f, e) \
	(f)->error = e,  FLACWRITE_ERROR

static inline int flacwrite_addtag_name(flacwrite *f, ffstr name, ffstr val)
{
	if (0 != vorbistagwrite_add_name(&f->vtag, name, val))
		return -1;
	return 0;
}

/**
mmtag: enum MMTAG
Return 0 on success */
static inline int flacwrite_addtag(flacwrite *f, ffuint mmtag, ffstr val)
{
	if (0 != vorbistagwrite_add(&f->vtag, mmtag, val))
		return -1;
	return 0;
}

/** Set picture
Data must be valid until the header is written */
static inline void flacwrite_pic(flacwrite *f, const struct flac_picinfo *info, const ffstr *pic)
{
	f->picinfo = *info;
	if (f->picinfo.mime == NULL)
		f->picinfo.mime = "";
	if (f->picinfo.desc == NULL)
		f->picinfo.desc = "";
	f->picdata = *pic;
}

static inline const char* flacwrite_error(flacwrite *f)
{
	return f->error;
}

/** Get buffer with INFO, TAGS and PADDING blocks */
static int _flacw_hdr(flacwrite *f)
{
	if (f->seektable_interval != 0 && f->total_samples != 0) {
		ffuint interval = (f->seektable_interval == (ffuint)-1) ? f->info.sample_rate : f->seektable_interval;
		if (0 > flac_seektab_init(&f->sktab, f->total_samples, interval))
			return -1;
	}

	ffstr tag = vorbistagwrite_fin(&f->vtag);
	ffuint padding = (f->min_meta > tag.len) ? (f->min_meta - tag.len) : 0;
	ffuint nblocks = 1 + (padding != 0) + (f->picdata.len != 0) + (f->sktab.len != 0);

	if (NULL == ffvec_alloc(&f->buf, flac_meta_size(tag.len, padding), 1))
		return -1;

	char *p = (char*)f->buf.ptr;
	ffssize i = flac_info_write(p, f->buf.cap, &f->info);
	if (i < 0)
		return -1;

	i += flac_hdr_write(&p[i], FLAC_TTAGS, (nblocks == 1), tag.len);
	ffmem_copy(&p[i], tag.ptr, tag.len);
	i += tag.len;
	vorbistagwrite_destroy(&f->vtag);

	if (padding != 0)
		i += flac_padding_write(&p[i], padding, (nblocks == 2));

	f->buf.len = i;
	f->hdrlen = i;
	return 0;
}

enum FLACWRITE_R {
	/** Awaiting next FLAC frame to write */
	FLACWRITE_MORE,

	/** Output data chunk is ready */
	FLACWRITE_DATA,

	/** Next output data chunk must be written at offset flacwrite_offset() */
	FLACWRITE_SEEK,
	FLACWRITE_DONE,
	FLACWRITE_ERROR,
};

/* .flac write algorithm:
. Reserve the space in output file for FLAC stream info
. Write vorbis comments and padding
. Write picture
. Write empty seek table
. After all frames have been written,
   seek back to the beginning and write the complete FLAC stream info and seek table
*/
/**
frame_samples: audio samples encoded in this frame
Return enum FLACWRITE_R */
static inline int flacwrite_process(flacwrite *f, ffstr *in, ffuint frame_samples, ffstr *out)
{
	enum {
		W_HDR, W_PIC, W_SEEKTAB_SPACE,
		W_FRAMES,
		W_SEEK0, W_INFO_WRITE, W_SEEKTAB_SEEK, W_SEEKTAB_WRITE,
	};
	int r;

	for (;;) {
		switch (f->state) {

		case W_HDR:
			if (0 != _flacw_hdr(f))
				return _FLACW_ERR(f, "not enough memory");

			ffstr_setstr(out, &f->buf);
			f->buf.len = 0;
			f->state = W_PIC;
			return FLACWRITE_DATA;

		case W_PIC: {
			f->state = W_SEEKTAB_SPACE;
			if (f->picdata.len == 0)
				continue;

			r = flac_pic_write(NULL, 0, &f->picinfo, &f->picdata, 0);
			if (NULL == ffvec_realloc(&f->buf, r, 1))
				return _FLACW_ERR(f, "not enough memory");
			r = flac_pic_write(f->buf.ptr, f->buf.cap, &f->picinfo, &f->picdata, (f->sktab.len == 0));
			if (r < 0)
				return _FLACW_ERR(f, "can't write picture");
			ffstr_set(out, f->buf.ptr, r);
			f->hdrlen += r;
			return FLACWRITE_DATA;
		}

		case W_SEEKTAB_SPACE: {
			f->state = W_FRAMES;
			if (f->sktab.len == 0)
				continue;
			r = flac_seektab_size(f->sktab.len);
			if (NULL == ffvec_realloc(&f->buf, r, 1))
				return _FLACW_ERR(f, "not enough memory");
			f->seektab_off = f->hdrlen;
			ffmem_zero(f->buf.ptr, r);
			ffstr_set(out, f->buf.ptr, r);
			return FLACWRITE_DATA;
		}

		case W_FRAMES:
			if (f->fin) {
				f->state = W_SEEK0;
				continue;
			}
			if (in->len == 0)
				return FLACWRITE_MORE;
			f->iskpt = flac_seektab_add(f->sktab.ptr, f->sktab.len, f->iskpt, f->nsamples, f->frames_len, frame_samples);
			*out = *in;
			in->len = 0;
			f->frames_len += out->len;
			f->nsamples += frame_samples;
			return FLACWRITE_DATA;

		case W_SEEK0:
			if (!f->outdev_seekable) {
				out->len = 0;
				return FLACWRITE_DONE;
			}
			f->state = W_INFO_WRITE;
			f->off = 0;
			return FLACWRITE_SEEK;

		case W_INFO_WRITE:
			f->info.total_samples = f->nsamples;
			r = flac_info_write(f->buf.ptr, f->buf.cap, &f->info);
			if (r < 0)
				return _FLACW_ERR(f, "too large total samples value in FLAC header");
			ffstr_set(out, f->buf.ptr, r);
			f->state = W_SEEKTAB_SEEK;
			return FLACWRITE_DATA;

		case W_SEEKTAB_SEEK:
			if (f->sktab.len == 0)
				return FLACWRITE_DONE;
			f->state = W_SEEKTAB_WRITE;
			f->off = f->seektab_off;
			return FLACWRITE_SEEK;

		case W_SEEKTAB_WRITE:
			r = flac_seektab_write(f->buf.ptr, f->buf.cap, f->sktab.ptr, f->sktab.len, f->info.minblock);
			ffstr_set(out, f->buf.ptr, r);
			return FLACWRITE_DONE;

		default:
			FF_ASSERT(0);
			return _FLACW_ERR(f, "corruption");
		}
	}
}

#undef _FLACW_ERR

static inline void flacwrite_finish(flacwrite *f, const struct flac_info *info)
{
	f->info = *info;
	f->fin = 1;
}

/** Get an absolute file offset to seek */
static inline ffuint64 flacwrite_offset(flacwrite *f)
{
	return f->off;
}
