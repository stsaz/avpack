/** avpack: meta tags
2021, Simon Zolin
*/

#pragma once

enum MMTAG {
	MMTAG_UNKNOWN,

	MMTAG_ALBUM,
	MMTAG_ALBUMARTIST,
	MMTAG_ARTIST,
	MMTAG_COMMENT,
	MMTAG_COMPOSER,
	MMTAG_COPYRIGHT,
	MMTAG_DATE,
	MMTAG_DISCNUMBER,
	MMTAG_GENRE,
	MMTAG_LYRICS,
	MMTAG_PICTURE,
	MMTAG_PUBLISHER,
	MMTAG_REPLAYGAIN_TRACK_GAIN,
	MMTAG_TITLE,
	MMTAG_TRACKNO,
	MMTAG_TRACKTOTAL,
	MMTAG_VENDOR,

	_MMTAG_N,
};

struct avpk_pic {
	const char *mime, *desc;
};

/** Pack picture metadata for MMTAG_PICTURE.
Format:
"avpkpict"
mime\0
description\0
data
Return N of bytes written. */
static inline ffsize avpk_pic_write(void *dst, const struct avpk_pic *meta, ffstr data)
{
	if (dst == NULL)
		return 8 + ffsz_len(meta->mime) + 1 + ffsz_len(meta->desc) + 1 + data.len;

	char *p = ffmem_copy(dst, "avpkpict", 8);
	p = ffmem_copy(p, meta->mime, ffsz_len(meta->mime) + 1);
	p = ffmem_copy(p, meta->desc, ffsz_len(meta->desc) + 1);
	p = ffmem_copy(p, data.ptr, data.len);
	return p - (char*)dst;
}

/** Unpack MMTAG_PICTURE data.
Return N of bytes read. */
static inline ffsize avpk_pic_read(ffstr data, struct avpk_pic *meta, ffstr *body)
{
	const char *p = data.ptr, *end = data.ptr + data.len;
	if (ffmem_cmp(p, "avpkpict", 8))
		return 0;
	p += 8;

	meta->mime = p;
	p += _ffsz_nlen(p, end - p);
	if (p == end || *p != '\0')
		return 0;
	p++;

	meta->desc = p;
	p += _ffsz_nlen(p, end - p);
	if (p == end || *p != '\0')
		return 0;
	p++;

	ffstr_set(body, p, end - p);
	return data.len;
}
