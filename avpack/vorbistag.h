/** avpack: vorbis tags
2015,2021 Simon Zolin
*/

/*
vorbistagread_process
vorbistagwrite_destroy
vorbistagwrite_add_name vorbistagwrite_add
vorbistagwrite_fin
*/

/* vorbis tag format:
(LENGTH VENDOR) ENTRIES_COUNT [LENGTH KEY=VALUE]...
*/

#pragma once

#include <ffbase/vector.h>
#include <ffbase/stringz.h>
#include <avpack/mmtag.h>

typedef struct vorbistagread {
	ffuint state;
	ffuint cnt;
	int tag; // enum FFMMTAG
} vorbistagread;

enum VORBISTAGREAD_R {
	VORBISTAGREAD_DONE = -1,
	VORBISTAGREAD_ERROR = -2,
};

static const ffbyte _vorbistag_mmtag[] = {
	MMTAG_ALBUM,
	MMTAG_ALBUMARTIST,
	MMTAG_ALBUMARTIST,
	MMTAG_ARTIST,
	MMTAG_COMMENT,
	MMTAG_COMPOSER,
	MMTAG_DATE,
	MMTAG_DISCNUMBER,
	MMTAG_GENRE,
	MMTAG_LYRICS,
	MMTAG_PUBLISHER,
	MMTAG_REPLAYGAIN_TRACK_GAIN,
	MMTAG_TITLE,
	MMTAG_TRACKTOTAL,
	MMTAG_TRACKNO,
	MMTAG_TRACKTOTAL,
};

static const char *const _vorbistag_str[] = {
	"ALBUM",
	"ALBUM ARTIST", // =ALBUMARTIST
	"ALBUMARTIST",
	"ARTIST",
	"COMMENT",
	"COMPOSER",
	"DATE",
	"DISCNUMBER",
	"GENRE",
	"LYRICS",
	"PUBLISHER",
	"REPLAYGAIN_TRACK_GAIN",
	"TITLE",
	"TOTALTRACKS", // =TRACKTOTAL
	"TRACKNUMBER",
	"TRACKTOTAL",
};

/** Get the next tag.
Note: partial input is not supported.
Return >0: enum MMTAG
 <=0: enum VORBISTAGREAD_R */
static inline int vorbistagread_process(vorbistagread *v, ffstr *in, ffstr *name, ffstr *val)
{
	enum { I_VENDOR, I_CMT_CNT, I_CMT };
	ffuint n;

	switch (v->state) {
	case I_VENDOR:
		if (4 > in->len)
			return VORBISTAGREAD_ERROR;
		n = ffint_le_cpu32_ptr(in->ptr);
		ffstr_shift(in, 4);

		if (n > in->len)
			return VORBISTAGREAD_ERROR;
		ffstr_setz(name, "VENDOR");
		ffstr_set(val, in->ptr, n);
		ffstr_shift(in, n);
		v->state = I_CMT_CNT;
		return MMTAG_VENDOR;

	case I_CMT_CNT:
		if (4 > in->len)
			return VORBISTAGREAD_ERROR;
		n = ffint_le_cpu32_ptr(in->ptr);
		ffstr_shift(in, 4);
		v->cnt = n;
		v->state = I_CMT;
		// fallthrough

	case I_CMT: {
		if (v->cnt == 0)
			return VORBISTAGREAD_DONE;

		if (4 > in->len)
			return VORBISTAGREAD_ERROR;
		n = ffint_le_cpu32_ptr(in->ptr);
		ffstr_shift(in, 4);

		if (n > in->len)
			return VORBISTAGREAD_ERROR;
		ffssize pos = ffs_findchar(in->ptr, n, '=');
		ffs_split(in->ptr, n, pos, name, val);
		ffstr_shift(in, n);
		v->cnt--;

		int tag = ffszarr_ifindsorted(_vorbistag_str, FF_COUNT(_vorbistag_str), name->ptr, name->len);
		tag = (tag != -1) ? _vorbistag_mmtag[tag] : 0;
		return tag;
	}
	}

	return VORBISTAGREAD_ERROR;
}


typedef struct vorbistagwrite {
	ffvec out;
	ffuint cnt; // number of entries (including vendor)
	ffuint left_zone;
} vorbistagwrite;

static inline void vorbistagwrite_create(vorbistagwrite *v)
{
	if (v->left_zone) {
		ffvec_alloc(&v->out, v->left_zone, 1);
		v->out.len = v->left_zone;
	}
}

/** Free the object data
Note: invalidates the data returned by vorbistagwrite_fin() */
static inline void vorbistagwrite_destroy(vorbistagwrite *v)
{
	ffvec_free(&v->out);
}

/** Add an entry.  The first one must be vendor string. */
static inline int vorbistagwrite_add_name(vorbistagwrite *v, ffstr name, ffstr val)
{
	ffuint n = name.len + 1 + val.len;
	if (NULL == ffvec_grow(&v->out, 4 + 4 + n, 1))
		return -1;

	char *d = (char*)v->out.ptr + v->out.len;

	if (v->cnt == 0) {
		// vendor
		*(ffuint*)d = ffint_le_cpu32(val.len);
		d += 4;
		d = ffmem_copy(d, val.ptr, val.len);
		d += 4;

	} else {
		*(ffuint*)d = ffint_le_cpu32(n);
		d += 4;
		d += ffs_upper(d, -1, name.ptr, name.len);
		*d++ = '=';
		d = ffmem_copy(d, val.ptr, val.len);
	}

	v->out.len = d - (char*)v->out.ptr;
	v->cnt++;
	return 0;
}

/**
tag: enum FFVORBTAG */
static inline int vorbistagwrite_add(vorbistagwrite *v, ffuint tag, ffstr val)
{
	ffstr name = {};
	if (tag != MMTAG_VENDOR) {
		int i = ffarrint8_find(_vorbistag_mmtag, sizeof(_vorbistag_mmtag), tag);
		if (i < 0)
			return -1;
		ffstr_setz(&name, _vorbistag_str[i]);
	}
	return vorbistagwrite_add_name(v, name, val);
}

/** Set the total number of entries
Return Vorbis tag data */
static inline ffstr vorbistagwrite_fin(vorbistagwrite *v)
{
	FF_ASSERT(v->cnt != 0);
	char *d = (char*)v->out.ptr + v->left_zone;
	ffuint vendor_len = ffint_le_cpu32_ptr(d);
	*(ffuint*)(d + 4 + vendor_len) = ffint_le_cpu32(v->cnt - 1);
	ffstr s = FFSTR_INITN(d, v->out.len - v->left_zone);
	return s;
}
