/** avpack: APE tag
2021, Simon Zolin
*/

/*
apetagread_open apetagread_close
apetagread_footer
apetagread_process
apetagread_error
*/

/* APETAG format:
[HDR]  (VAL_SIZE  FLAGS  NAME\0  VAL)...  FOOTER
*/

#pragma once

#include <avpack/mmtag.h>
#include <ffbase/vector.h>
#include <ffbase/stringz.h>

struct apetaghdr {
	char id[8]; // "APETAGEX"
	ffbyte ver[4]; // 2000
	ffbyte size[4]; // all fields + footer
	ffbyte nfields[4];
	ffbyte flags[4]; // 0x80000000: has header
	ffbyte reserved[8];
};

enum APETAG_FLAGS {
	APETAG_FMASK = 6,
	APETAG_FBINARY = 2,
};

struct apetagfld {
	ffbyte val_len[4];
	ffbyte flags[4]; // enum APETAG_FLAGS
	char name_val[0];
};

static const char *const _apetag_str[] = {
	"album",
	"albumartist",
	"artist",
	"comment",
	"cover art (front)",
	"genre",
	"publisher",
	"replaygain_track_gain",
	"title",
	"track",
	"year",
};
static const ffbyte _apetag_int[] = {
	MMTAG_ALBUM,
	MMTAG_ALBUMARTIST,
	MMTAG_ARTIST,
	MMTAG_COMMENT,
	MMTAG_PICTURE,
	MMTAG_GENRE,
	MMTAG_PUBLISHER,
	MMTAG_REPLAYGAIN_TRACK_GAIN,
	MMTAG_TITLE,
	MMTAG_TRACKNO,
	MMTAG_DATE,
};


typedef struct apetagread {
	ffuint state;
	ffvec buf;
	ffstr chunk;
	ffuint tagsize;
	ffuint has_hdr;
	const char *error;
} apetagread;

static inline void apetagread_open(struct apetagread *a)
{
	(void)a;
}

static inline void apetagread_close(struct apetagread *a)
{
	ffvec_free(&a->buf);
}

enum APETAGREAD_R {
	APETAGREAD_MORE = 1, // need more input data
	APETAGREAD_NO, // not APE tag
	APETAGREAD_DONE, // done reading
	APETAGREAD_SEEK,
	APETAGREAD_ERROR,
};

static inline const char* apetagread_error(struct apetagread *a)
{
	return a->error;
}

#define _APETAGREAD_ERR(a, e) \
	(a)->error = (e),  APETAGREAD_ERROR

/**
seek_offset: [output] relative offset (always negative) to the beginning of APE tag
Return >0: enum APETAGREAD_R */
static inline int apetagread_footer(struct apetagread *a, ffstr input, ffint64 *seek_offset)
{
	switch (a->state) {
	case 0: {
		if (input.len < sizeof(struct apetaghdr))
			return _APETAGREAD_ERR(a, "input data too small");

		const struct apetaghdr *h = (struct apetaghdr*)&input.ptr[input.len - sizeof(struct apetaghdr)];
		ffuint64 size = ffint_le_cpu32_ptr(h->size);
		if (!(!ffmem_cmp(h->id, "APETAGEX", 8)
			&& size >= sizeof(struct apetaghdr)))
			return APETAGREAD_NO;

		if (ffint_le_cpu32_ptr(h->ver) != 2000)
			return _APETAGREAD_ERR(a, "version not supported");

		ffuint flags = ffint_le_cpu32_ptr(h->flags);
		if (flags & 0x80000000) {
			size += sizeof(struct apetaghdr);
			a->has_hdr = 1;
		}
		a->tagsize = size;
		*seek_offset = -(ffint64)size;
		a->state = 1;
		return APETAGREAD_SEEK;
	}
	}
	return _APETAGREAD_ERR(a, "invalid parser state");
}

/**
Return >0: enum APETAGREAD_R
 <=0: enum MMTAG */
static inline int apetagread_process(struct apetagread *a, ffstr *input, ffstr *name, ffstr *value)
{
	enum {
		R_FTR = 0, R_GATHER = 1, R_HDR, R_FLD,
	};
	int r;

	for (;;) {
		switch (a->state) {
		case R_FTR:
			return APETAGREAD_ERROR;

		case R_GATHER:
			r = ffstr_gather((ffstr*)&a->buf, &a->buf.cap, input->ptr, input->len, a->tagsize, &a->chunk);
			if (r < 0)
				return _APETAGREAD_ERR(a, "not enough memory");
			ffstr_shift(input, r);
			if (a->chunk.len == 0)
				return APETAGREAD_MORE;
			a->chunk.len -= sizeof(struct apetaghdr);
			a->buf.len = 0;
			a->state = R_FLD;
			if (a->has_hdr)
				a->state = R_HDR;
			break;

		case R_HDR:
			ffstr_shift(&a->chunk, sizeof(struct apetaghdr));
			a->state = R_FLD;
			// fallthrough
		case R_FLD: {
			if (a->chunk.len == 0)
				return APETAGREAD_DONE;
			else if (sizeof(struct apetagfld) > a->chunk.len)
				return _APETAGREAD_ERR(a, "not enough data");

			const struct apetagfld *f = (struct apetagfld*)a->chunk.ptr;
			ffstr_shift(&a->chunk, sizeof(struct apetagfld));
			ffuint val_len = ffint_le_cpu32_ptr(f->val_len);
			r = ffstr_findchar(&a->chunk, '\0');
			if (r < 0 || r+1 + val_len > a->chunk.len)
				return _APETAGREAD_ERR(a, "corrupted field data");

			ffstr_set(name, a->chunk.ptr, r);
			ffstr_set(value, &a->chunk.ptr[r+1], val_len);
			ffstr_shift(&a->chunk, r+1 + val_len);

			r = ffszarr_ifindsorted(_apetag_str, FF_COUNT(_apetag_str), name->ptr, name->len);
			if (r < 0)
				return MMTAG_UNKNOWN;
			return -(int)_apetag_int[r];
		}
		}
	}

	return APETAGREAD_ERROR;
}

#undef _APETAGREAD_ERR
