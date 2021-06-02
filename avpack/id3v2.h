/** avpack: ID3v2 tag
2021, Simon Zolin
*/

/*
id3v2read_open id3v2read_close
id3v2read_process
id3v2read_error
id3v2read_size
id3v2write_create id3v2write_close
id3v2write_add
id3v2write_finish
*/

/** ID3v2 format:
HEADER [EXT-HDR]
(FRAME-HEADER  [TEXT-ENCODING]  DATA...)...
PADDING
*/

#pragma once

#include <avpack/id3v1.h>
#include <avpack/mmtag.h>
#include <ffbase/vector.h>
#include <ffbase/unicode.h>

static const ffbyte id3v2_frame_int[] = {
	MMTAG_PICTURE,
	MMTAG_COMMENT,
	MMTAG_ALBUM,
	MMTAG_GENRE,
	MMTAG_TITLE,
	MMTAG_ARTIST,
	MMTAG_ALBUMARTIST,
	MMTAG_PUBLISHER,
	MMTAG_TRACKNO,
	MMTAG_DATE,
};
static const char id3v2_frame_str[][4] = {
	"APIC", // MMTAG_PICTURE
	"COMM", // MMTAG_COMMENT // "LNG" "SHORT" \0 "TEXT"
	"TALB", // MMTAG_ALBUM
	"TCON", // MMTAG_GENRE // "Genre" | "(NN)Genre" | "(NN)" where NN is ID3v1 genre index
	"TIT2", // MMTAG_TITLE
	"TPE1", // MMTAG_ARTIST
	"TPE2", // MMTAG_ALBUMARTIST
	"TPUB", // MMTAG_PUBLISHER
	"TRCK", // MMTAG_TRACKNO // "N[/TOTAL]"
	"TYER", // MMTAG_DATE
};

static const ffbyte id3v22_frame_int[] = {
	MMTAG_COMMENT,
	MMTAG_PICTURE,
	MMTAG_ALBUM,
	MMTAG_GENRE,
	MMTAG_ARTIST,
	MMTAG_TRACKNO,
	MMTAG_TITLE,
	MMTAG_DATE,
};
static const char id3v22_frame_str[][3] = {
	"COM", // MMTAG_COMMENT
	"PIC", // MMTAG_PICTURE
	"TAL", // MMTAG_ALBUM
	"TCO", // MMTAG_GENRE
	"TP1", // MMTAG_ARTIST
	"TRK", // MMTAG_TRACKNO
	"TT2", // MMTAG_TITLE
	"TYE", // MMTAG_DATE
};

struct id3v2_hdr {
	char id3[3]; // "ID3"
	ffbyte ver[2]; // e.g. \4\0 for v2.4
	ffbyte flags; // enum ID3V2_HDR_F
	ffbyte size[4]; // 7-bit bytes
};

enum ID3V2_HDR_F {
	ID3V2_HDR_EXT = 0x40, // extended header follows
	ID3V2_HDR_UNSYNC = 0x80,
};

struct id3v2_framehdr {
	char id[4];
	ffbyte size[4]; // v2.4: 7-bit bytes
	ffbyte flags[2]; // [1]: enum ID3V2_FRAME_F
};

enum ID3V2_FRAME_F {
	ID3V2_FRAME_DATALEN = 1, // 4 bytes follow the frame header
	ID3V2_FRAME_UNSYNC = 2,
};

struct id3v22_framehdr {
	char id[3];
	ffbyte size[3];
};

enum ID3V2_TXTENC {
	ID3V2_ANSI,
	ID3V2_UTF16BOM,
	ID3V2_UTF16BE,
	ID3V2_UTF8,
};

/** Encode integer so that it doesn't contain 0xff byte */
static inline void _id3v2_int7_write(void *dst, ffuint i)
{
	ffuint i7 = (i & 0x0000007f)
		| ((i & (0x0000007f << 7)) << 1)
		| ((i & (0x0000007f << 14)) << 2)
		| ((i & (0x0000007f << 21)) << 3);
	*(ffuint*)dst = ffint_be_cpu32(i7);
}

/** Decode integer */
static inline ffuint _id3v2_int7_read(const void *src)
{
	ffuint i = ffint_be_cpu32_ptr(src);
	return ((i & 0x7f000000) >> 3)
		| ((i & 0x007f0000) >> 2)
		| ((i & 0x00007f00) >> 1)
		| (i & 0x0000007f);
}


struct id3v2read {
	ffuint state, nextstate;
	ffsize gather_size;
	ffvec buf, unsync_buf;
	ffstr chunk;

	ffuint ver;
	ffuint total_size;
	ffuint tagsize;
	ffuint tag; // enum MMTAG
	ffuint txtenc;
	ffuint have_txtenc;
	ffuint frame_size;
	ffuint hdrflags;
	ffuint frame_flags;
	const char *error;
	char error_s[200];
	char frame_id[5];

	ffuint codepage;
};

static inline void id3v2read_open(struct id3v2read *d)
{
	ffvec_alloc(&d->buf, 1024, 1);
	d->codepage = FFUNICODE_WIN1252;
}

static inline void id3v2read_close(struct id3v2read *d)
{
	ffvec_free(&d->buf);
	ffvec_free(&d->unsync_buf);
}

enum ID3V2READ_R {
	ID3V2READ_MORE = 1, // need more input data
	ID3V2READ_NO, // not ID3v2 tag
	ID3V2READ_DONE, // done reading
	ID3V2READ_ERROR,
};

#define id3v2read_size(d)  (d)->total_size
#define id3v2read_version(d)  (d)->ver

#define _ID3V2READ_ERR(d, e) \
	(d)->error = (e),  ID3V2READ_ERROR

static inline const char* id3v2read_error(struct id3v2read *d)
{
	int n = ffs_format(d->error_s, sizeof(d->error_s) - 1, "ID3v2.%u: %s: %s"
		, d->ver, d->frame_id, d->error);
	if (n <= 0)
		return d->error;
	d->error_s[n] = '\0';
	return d->error_s;
}

/** Replace: FF 00 -> FF */
static void _id3v2read_unsync(ffvec *buf, ffstr in)
{
	ffbyte *out = buf->ptr;
	ffuint skip0 = 0;

	for (ffsize i = 0;  i < in.len;  i++) {

		if (skip0) {
			skip0 = 0;
			out[buf->len++] = 0xff;
			if (in.ptr[i] == 0)
				continue;
		}

		if ((ffbyte)in.ptr[i] == 0xff) {
			skip0 = 1;
			continue;
		}

		out[buf->len++] = in.ptr[i];
	}
}

static int _id3v2read_frame_read(struct id3v2read *d, ffuint *framesize)
{
	int tag = MMTAG_UNKNOWN;
	d->have_txtenc = 0;
	if (d->ver == 2) {
		const struct id3v22_framehdr *f = (struct id3v22_framehdr*)d->chunk.ptr;
		*framesize = ffint_be_cpu24_ptr(f->size);

		ffsz_copyn(d->frame_id, sizeof(d->frame_id), f->id, 3);
		int i = ffcharr_findsorted(id3v22_frame_str, FF_COUNT(id3v22_frame_str), sizeof(id3v22_frame_str[0]), f->id, 3);
		if (i >= 0)
			tag = id3v22_frame_int[i];

		if (f->id[0] == 'T' || tag == MMTAG_COMMENT)
			d->have_txtenc = 1;

	} else {
		const struct id3v2_framehdr *f = (struct id3v2_framehdr*)d->chunk.ptr;

		ffsz_copyn(d->frame_id, sizeof(d->frame_id), f->id, 4);
		int i = ffcharr_findsorted(id3v2_frame_str, FF_COUNT(id3v2_frame_str), sizeof(id3v2_frame_str[0]), f->id, 4);
		if (i >= 0)
			tag = id3v2_frame_int[i];

		if (f->id[0] == 'T' || tag == MMTAG_COMMENT)
			d->have_txtenc = 1;

		if (d->ver == 3) {
			*framesize = ffint_be_cpu32_ptr(f->size);
			if (f->flags[1] != 0)
				return _ID3V2READ_ERR(d, "v3 frame flags not supported");

		} else {
			*framesize = _id3v2_int7_read(f->size);
			if ((f->flags[1] & ~(ID3V2_FRAME_DATALEN | ID3V2_FRAME_UNSYNC)) != 0)
				return _ID3V2READ_ERR(d, "frame flags not supported");
			d->frame_flags = f->flags[1];
		}
	}
	return -tag;
}

static int _id3v2read_text_decode(struct id3v2read *d, ffstr in, ffstr *out)
{
	int r;
	ffvec *b = &d->unsync_buf;
	switch (d->txtenc) {
	case ID3V2_UTF8:
		ffstr_setstr(out, &in);
		break;

	case ID3V2_ANSI:
		ffvec_grow(b, in.len, 1);
		b->len = ffutf8_from_cp(b->ptr, b->cap, in.ptr, in.len, d->codepage);
		ffstr_setstr(out, b);
		break;

	case ID3V2_UTF16BOM: {
		ffsize len = in.len;
		r = ffutf_bom(in.ptr, &len);
		if (!(r == FFUNICODE_UTF16LE || r == FFUNICODE_UTF16BE))
			return _ID3V2READ_ERR(d, "invalid BOM");
		ffstr_shift(&in, len);
		ffvec_grow(b, in.len * 2, 1);
		b->len = ffutf8_from_utf16(b->ptr, b->cap, in.ptr, in.len, r);
		ffstr_setstr(out, b);
		break;
	}

	case ID3V2_UTF16BE:
		ffvec_grow(b, in.len * 2, 1);
		b->len = ffutf8_from_utf16(b->ptr, b->cap, in.ptr, in.len, FFUNICODE_UTF16BE);
		ffstr_setstr(out, b);
		break;

	default:
		return _ID3V2READ_ERR(d, "invalid encoding");
	}

	return 0xdeed;
}

/** Read next ID3v2 tag field
Return >0: enum ID3V2READ_R
 <=0: enum MMTAG */
static inline int id3v2read_process(struct id3v2read *d, ffstr *input, ffstr *name, ffstr *value)
{
	enum {
		R_INIT, R_GATHER, R_HDR, R_EXTHDR_SIZE, R_EXTHDR,
		R_FR, R_FRHDR, R_FRDATA,
		R_UNSYNC,
		R_DATA, R_TRKTOTAL, R_PADDING,
	};
	int r;
	ffuint n;

	for (;;) {
		switch (d->state) {
		case R_INIT:
			d->gather_size = sizeof(struct id3v2_hdr);
			d->state = R_GATHER,  d->nextstate = R_HDR;
			break;

		case R_GATHER:
			r = ffstr_gather((ffstr*)&d->buf, &d->buf.cap, input->ptr, input->len, d->gather_size, &d->chunk);
			if (r < 0)
				return _ID3V2READ_ERR(d, "not enough memory");
			ffstr_shift(input, r);
			if (d->chunk.len == 0)
				return ID3V2READ_MORE;
			d->buf.len = 0;
			d->state = d->nextstate;
			break;

		case R_HDR: {
			const struct id3v2_hdr *h = (struct id3v2_hdr*)d->chunk.ptr;
			n = *(ffuint*)h->size;

			if (!(!ffmem_cmp(h->id3, "ID3", 3)
				&& (n & 0x80808080) == 0))
				return ID3V2READ_NO;

			d->tagsize = _id3v2_int7_read(h->size);
			d->total_size = d->tagsize;
			d->ver = h->ver[0];
			d->hdrflags = h->flags;

			if (!(d->ver >= 2 && d->ver <= 4))
				return _ID3V2READ_ERR(d, "version not supported");

			if ((d->ver == 3 && (h->flags & ~(ID3V2_HDR_UNSYNC | ID3V2_HDR_EXT)) != 0)
				|| (d->ver != 3 && (h->flags & ~ID3V2_HDR_UNSYNC) != 0)) {
				return _ID3V2READ_ERR(d, "header flags not supported");
			}

			if (d->ver == 3 && (h->flags & ID3V2_HDR_EXT)) {
				if (4 > d->tagsize)
					return _ID3V2READ_ERR(d, "data too large");
				d->tagsize -= 4;
				d->gather_size = 4;
				d->state = R_GATHER,  d->nextstate = R_EXTHDR_SIZE;
				break;
			}

			d->state = R_FR;
			break;
		}

		case R_EXTHDR_SIZE:
			n = ffint_be_cpu32_ptr(d->chunk.ptr);
			if (n > d->tagsize)
				return _ID3V2READ_ERR(d, "data too large");
			d->tagsize -= n;
			d->gather_size = n;
			d->state = R_GATHER,  d->nextstate = R_EXTHDR;
			break;
		case R_EXTHDR:
			d->state = R_FR;
			break;

		case R_FR:
			if (d->tagsize == 0)
				return ID3V2READ_DONE;
			if (input->len == 0)
				return ID3V2READ_MORE;
			if (input->ptr[0] == '\0') {
				d->state = R_PADDING;
				break;
			}

			n = (d->ver == 2) ? sizeof(struct id3v22_framehdr) : sizeof(struct id3v2_framehdr);
			if (n > d->tagsize)
				return _ID3V2READ_ERR(d, "data too large");
			d->tagsize -= n;
			d->gather_size = n;
			d->state = R_GATHER,  d->nextstate = R_FRHDR;
			break;

		case R_FRHDR:
			if ((r = _id3v2read_frame_read(d, &n)) > 0)
				return r;
			d->tag = -r;

			if (n > d->tagsize)
				return _ID3V2READ_ERR(d, "data too large");
			d->frame_size = n;

			d->gather_size = n;
			d->state = R_GATHER,  d->nextstate = R_FRDATA;
			break;

		case R_FRDATA:
			if (d->frame_flags & ID3V2_FRAME_DATALEN) {
				if (4 > d->chunk.len)
					return _ID3V2READ_ERR(d, "data too large");
				ffstr_shift(&d->chunk, 4);
			}

			d->txtenc = ID3V2_UTF8;
			if (d->have_txtenc) {
				if (1 > d->chunk.len)
					return _ID3V2READ_ERR(d, "data too large");
				d->txtenc = (ffbyte)d->chunk.ptr[0];
				ffstr_shift(&d->chunk, 1);
			}
			d->tagsize -= d->frame_size;

			if ((d->frame_flags & ID3V2_FRAME_UNSYNC) || (d->hdrflags & ID3V2_HDR_UNSYNC)) {
				d->unsync_buf.len = 0;
				if (NULL == ffvec_grow(&d->unsync_buf, d->frame_size, 1))
					return _ID3V2READ_ERR(d, "not enough memory");
				d->state = R_UNSYNC;
				break;
			}

			d->state = R_DATA;
			break;

		case R_UNSYNC:
			_id3v2read_unsync(&d->unsync_buf, d->chunk);
			ffvec_free(&d->buf);
			d->buf = d->unsync_buf;
			ffvec_null(&d->unsync_buf);
			ffstr_setstr(&d->chunk, &d->buf);
			d->buf.len = 0;
			d->state = R_DATA;
			break;

		case R_DATA:
			switch (d->tag) {
			case MMTAG_COMMENT:
				if (d->chunk.len >= FFS_LEN("LNG\0")) {
					// skip language, short description and NULL
					ffstr_shift(&d->chunk, FFS_LEN("LNG"));
					switch (d->txtenc) {
					case ID3V2_UTF8:
					case ID3V2_ANSI:
						if ((r = ffstr_findchar(&d->chunk, '\0')) >= 0)
							ffstr_shift(&d->chunk, r + 1);
						break;

					case ID3V2_UTF16BOM:
					case ID3V2_UTF16BE:
						if ((r = ffutf16_findchar(d->chunk.ptr, d->chunk.len, '\0')) >= 0)
							ffstr_shift(&d->chunk, r + 2);
						break;
					}
				}
				break;
			}

			if (0xdeed != (r = _id3v2read_text_decode(d, d->chunk, value)))
				return r;

			if (value->len != 0 && *ffslice_lastT(value, char) == '\0')
				value->len--;

			d->state = R_FR;

			switch (d->tag) {
			case MMTAG_TRACKNO:
				if (ffstr_splitby(value, '/', value, &d->chunk) >= 0)
					d->state = R_TRKTOTAL;
				break;

			case MMTAG_GENRE:
				if ((r = ffstr_matchfmt(value, "(%u)", &n)) >= 0) {
					if (r > 0)
						ffstr_shift(value, r);
					else if (n < FF_COUNT(id3v1_genres))
						ffstr_setz(value, id3v1_genres[n]);
				}
				break;
			}

			ffstr_setz(name, d->frame_id);
			return -(int)d->tag;

		case R_TRKTOTAL:
			ffstr_setz(name, d->frame_id);
			*value = d->chunk;
			d->state = R_FR;
			return -MMTAG_TRACKTOTAL;

		case R_PADDING:
			if (d->tagsize == 0)
				return ID3V2READ_DONE;
			if (input->len == 0)
				return ID3V2READ_MORE;
			n = ffmin(d->tagsize, input->len);
			ffstr_shift(input, n);
			d->tagsize -= n;
			break;
		}
	}
}


struct id3v2write {
	ffvec buf;
	char trackno[32];
	char tracktotal[32];
};

static inline void id3v2write_create(struct id3v2write *w)
{
	ffvec_alloc(&w->buf, 1024, 1);
	w->buf.len = sizeof(struct id3v2_hdr);
}

static inline void id3v2write_close(struct id3v2write *w)
{
	ffvec_free(&w->buf);
}

static int _id3v2write_addframe(struct id3v2write *w, const char *id, const char *data, ffsize len)
{
	ffsize n = sizeof(struct id3v2_framehdr) + 1 + len;

	if (!ffmem_cmp(id, "COMM", 4))
		n += FFS_LEN("LNG\0");

	if (NULL == ffvec_grow(&w->buf, n, 1))
		return -1;
	char *p = ffslice_end(&w->buf, 1);

	// write frame header
	struct id3v2_framehdr *fr = (struct id3v2_framehdr*)p;
	ffmem_copy(fr->id, id, 4);
	_id3v2_int7_write(fr->size, n - sizeof(*fr));
	fr->flags[0] = 0,  fr->flags[1] = 0;
	p += sizeof(*fr);

	*p++ = ID3V2_UTF8;

	if (!ffmem_cmp(id, "COMM", 4))
		p = ffmem_copy(p, "eng\0", 4);

	ffmem_copy(p, data, len);

	w->buf.len += n;
	return 0;
}

/** Add frame
Return 0 on success
 >0 if tag field isn't supported
 <0 on error */
static inline int id3v2write_add(struct id3v2write *w, ffuint id, ffstr data)
{
	switch (id) {
	case MMTAG_TRACKNO:
		ffsz_copyn(w->trackno, sizeof(w->trackno), data.ptr, data.len);
		return 0;
	case MMTAG_TRACKTOTAL:
		ffsz_copyn(w->tracktotal, sizeof(w->tracktotal), data.ptr, data.len);
		return 0;
	}

	if (-1 == (int)(id = ffarrint8_find(id3v2_frame_int, FF_COUNT(id3v2_frame_int), id)))
		return 1;
	return _id3v2write_addframe(w, id3v2_frame_str[id], data.ptr, data.len);
}

/** Finalize ID3v2 tag data */
static inline int id3v2write_finish(struct id3v2write *w, ffsize padding)
{
	// prepare TRCK frame data: "TRACKNO [/ TRACKTOTAL]"
	if (w->trackno[0] != '\0' || w->tracktotal[0] != '\0') {
		char *p = w->trackno + ffsz_len(w->trackno);
		if (w->trackno[0] == '\0')
			*p++ = '0';

		if (w->tracktotal[0] != '\0') {
			const char *end = w->trackno + sizeof(w->trackno);
			p += ffmem_ncopy(p, end - p, "/", 1);
			p += ffmem_ncopy(p, end - p, w->tracktotal, ffsz_len(w->tracktotal));
		}

		if (0 != _id3v2write_addframe(w, "TRCK", w->trackno, p - w->trackno))
			return -1;
	}

	// add padding
	if (padding != 0) {
		if (NULL == ffvec_grow(&w->buf, padding, 1))
			return -1;
		ffmem_zero(ffslice_end(&w->buf, 1), padding);
		w->buf.len += padding;
	}

	// write header
	struct id3v2_hdr *h = (struct id3v2_hdr*)w->buf.ptr;
	ffmem_copy(h->id3, "ID3", 3);
	h->ver[0] = 4,  h->ver[1] = 0;
	h->flags = 0;
	_id3v2_int7_write(h->size, w->buf.len - sizeof(*h));
	return 0;
}
