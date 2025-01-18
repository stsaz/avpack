/** avpack: ID3v2 tag
2021, Simon Zolin
*/

/*
id3v2read_open id3v2read_close
id3v2read_process
id3v2read_error
id3v2read_size
id3v2write_create id3v2write_close
id3v2write_add id3v2write_add_txxx
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
#include <avpack/shared.h>
#include <ffbase/vector.h>
#include <ffbase/unicode.h>

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
	// unused[4] // if ID3V2_FRAME_DATALEN
	// ffbyte text_encoding; // enum ID3V2_TXTENC - for ID starting with 'T' or 'COM', or 'APIC'
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
static inline unsigned _id3v2_int7_read(unsigned i)
{
	return ((i & 0x7f000000) >> 3)
		| ((i & 0x007f0000) >> 2)
		| ((i & 0x00007f00) >> 1)
		| (i & 0x0000007f);
}


struct id3v2read {
	ffuint state, nextstate;
	ffsize gather_size;
	struct avp_stream stm;
	ffvec unsync_buf, text_buf;
	ffstr chunk;
	ffuint offset;

	ffuint ver;
	ffuint total_size;
	ffuint hdr_flags;

	ffuint tag; // enum MMTAG
	ffuint body_off;
	ffuint txtenc;
	ffuint frame_flags;
	const char *error;
	char error_s[200];
	char frame_id[5];

	ffuint codepage;
	ffuint as_is; // return whole frames as-is
};

static inline void id3v2read_open(struct id3v2read *d)
{
	d->codepage = FFUNICODE_WIN1252;
}

static inline void id3v2read_close(struct id3v2read *d)
{
	_avp_stream_free(&d->stm);
	ffvec_free(&d->unsync_buf);
	ffvec_free(&d->text_buf);
}

enum ID3V2READ_R {
	ID3V2READ_MORE = 1, // need more input data
	ID3V2READ_NO, // not ID3v2 tag
	ID3V2READ_DONE, // done reading
	ID3V2READ_WARN,
	ID3V2READ_ERROR,
};

#define id3v2read_size(d)  (d)->total_size
#define id3v2read_version(d)  (d)->ver

#define _ID3V2READ_ERR(d, e) \
	(d)->error = (e),  ID3V2READ_ERROR
#define _ID3V2READ_WARN(d, e) \
	(d)->error = (e),  ID3V2READ_WARN

static inline const char* id3v2read_error(struct id3v2read *d)
{
	int n = ffs_format(d->error_s, sizeof(d->error_s) - 1, "ID3v2.%u: %s: %s"
		, d->ver, d->frame_id, d->error);
	if (n <= 0)
		return d->error;
	d->error_s[n] = '\0';
	return d->error_s;
}

static int _id3v2r_hdr_read(struct id3v2read *d, ffstr data, unsigned *hdr_len)
{
	if (sizeof(struct id3v2_hdr) + 4 > data.len)
		return ID3V2READ_ERROR;
	const struct id3v2_hdr *h = (struct id3v2_hdr*)data.ptr;
	unsigned n = ffint_be_cpu32_ptr(h->size);

	if (!(!ffmem_cmp(h->id3, "ID3", 3)
		&& (n & 0x80808080) == 0))
		return ID3V2READ_NO;

	d->total_size = sizeof(struct id3v2_hdr) + _id3v2_int7_read(n);
	d->ver = h->ver[0];
	d->hdr_flags = h->flags;

	switch (d->ver) {
	case 3:
		if ((h->flags & ~(ID3V2_HDR_UNSYNC | ID3V2_HDR_EXT)) != 0)
			return ID3V2READ_NO; // header flags not supported

		if (h->flags & ID3V2_HDR_EXT) {
			*hdr_len = sizeof(struct id3v2_hdr) + 4 + ffint_be_cpu32_ptr(h + 1);
			return ID3V2READ_MORE;
		}
		break;

	case 2:
	case 4:
		if ((h->flags & ~ID3V2_HDR_UNSYNC) != 0)
			return ID3V2READ_NO; // header flags not supported
		break;

	default:
		return ID3V2READ_NO; // version not supported
	}

	return 0;
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

static int _id3v2r_frame_read(struct id3v2read *d, ffstr in, ffuint *framesize)
{
	static const char id3v2_frame_str[][4] = {
		"APIC", // MMTAG_PICTURE // "MIME" \0 TYPE[1] "DESCRIPTION" \0 DATA[]
		"COMM", // MMTAG_COMMENT
		"TALB", // MMTAG_ALBUM
		"TCOM", // MMTAG_COMPOSER
		"TCON", // MMTAG_GENRE // "Genre" | "(NN)Genre" | "(NN)" where NN is ID3v1 genre index
		"TCOP", // MMTAG_COPYRIGHT
		"TIT2", // MMTAG_TITLE
		"TPE1", // MMTAG_ARTIST
		"TPE2", // MMTAG_ALBUMARTIST
		"TPUB", // MMTAG_PUBLISHER
		"TRCK", // MMTAG_TRACKNO // "N[/TOTAL]"
		"TYER", // MMTAG_DATE
		"USLT", // MMTAG_LYRICS
	};
	static const char id3v2_frame_int[] = {
		MMTAG_PICTURE,
		MMTAG_COMMENT,
		MMTAG_ALBUM,
		MMTAG_COMPOSER,
		MMTAG_GENRE,
		MMTAG_COPYRIGHT,
		MMTAG_TITLE,
		MMTAG_ARTIST,
		MMTAG_ALBUMARTIST,
		MMTAG_PUBLISHER,
		MMTAG_TRACKNO,
		MMTAG_DATE,
		MMTAG_LYRICS,
	};

	static const char id3v22_frame_str[][4] = {
		"COM\0", // MMTAG_COMMENT
		"PIC\0", // MMTAG_PICTURE
		"TAL\0", // MMTAG_ALBUM
		"TCO\0", // MMTAG_GENRE
		"TP1\0", // MMTAG_ARTIST
		"TRK\0", // MMTAG_TRACKNO
		"TT2\0", // MMTAG_TITLE
		"TYE\0", // MMTAG_DATE
	};
	static const char id3v22_frame_int[] = {
		MMTAG_COMMENT,
		MMTAG_PICTURE,
		MMTAG_ALBUM,
		MMTAG_GENRE,
		MMTAG_ARTIST,
		MMTAG_TRACKNO,
		MMTAG_TITLE,
		MMTAG_DATE,
	};

	const struct id3v2_framehdr *f = (struct id3v2_framehdr*)in.ptr;
	const void *frames = id3v2_frame_str;
	const char *tags = id3v2_frame_int;
	unsigned i, hdr_len = sizeof(struct id3v2_framehdr), n, n_frames = FF_COUNT(id3v2_frame_str);
	i = hdr_len;
	ffmem_copy(d->frame_id, in.ptr, 4);
	n = ffint_be_cpu32_ptr(in.ptr + 4);
	d->frame_flags = f->flags[1];

	switch (d->ver) {
	case 2:
		hdr_len = sizeof(struct id3v22_framehdr);
		ffmem_copy(d->frame_id, in.ptr, 3);
		n = ffint_be_cpu24_ptr(in.ptr + 3);
		frames = id3v22_frame_str;
		n_frames = FF_COUNT(id3v22_frame_str);
		tags = id3v22_frame_int;
		break;

	case 3:
		if (n & 0x80000000)
			return _ID3V2READ_ERR(d, "corrupt data");

		if (d->frame_flags)
			return _ID3V2READ_ERR(d, "v3 frame flags not supported");
		break;

	case 4:
		if (n & 0x80808080)
			return _ID3V2READ_ERR(d, "corrupt data");
		n = _id3v2_int7_read(n);

		if (d->frame_flags & ~(ID3V2_FRAME_DATALEN | ID3V2_FRAME_UNSYNC))
			return _ID3V2READ_ERR(d, "frame flags not supported");

		if (d->frame_flags & ID3V2_FRAME_DATALEN) {
			if (4 > n || i + 4 > in.len)
				return _ID3V2READ_ERR(d, "corrupt data");
			i += 4;
		}
		break;
	}

	int r = ffcharr_findsorted(frames, n_frames, 4, d->frame_id, 4);
	int tag = MMTAG_UNKNOWN;
	if (r >= 0)
		tag = tags[r];

	d->txtenc = ID3V2_UTF8;
	if (d->frame_id[0] == 'T'
		|| tag == MMTAG_COMMENT || tag == MMTAG_LYRICS || tag == MMTAG_PICTURE) {
		if (i - hdr_len + 1 > n || i + 1 > in.len)
			return _ID3V2READ_ERR(d, "corrupt data");
		d->txtenc = (ffbyte)in.ptr[i];
		i++;
	}

	d->body_off = i;
	*framesize = hdr_len + n;
	return -tag;
}

static int _id3v2read_text_decode(struct id3v2read *d, ffstr in, ffstr *out)
{
	int r;
	ffsize n;
	ffvec *b = &d->text_buf;
	switch (d->txtenc) {
	case ID3V2_UTF8:
		n = ffutf8_from_utf8(NULL, 0, in.ptr, in.len, 0);
		if (NULL == ffvec_realloc(b, n, 1))
			return _ID3V2READ_WARN(d, "not enough memory");
		b->len = ffutf8_from_utf8(b->ptr, b->cap, in.ptr, in.len, 0);
		break;

	case ID3V2_ANSI:
		n = ffutf8_from_cp(NULL, 0, in.ptr, in.len, d->codepage);
		if (NULL == ffvec_realloc(b, n, 1))
			return _ID3V2READ_WARN(d, "not enough memory");
		b->len = ffutf8_from_cp(b->ptr, b->cap, in.ptr, in.len, d->codepage);
		break;

	case ID3V2_UTF16BOM: {
		ffsize len = in.len;
		r = ffutf_bom(in.ptr, &len);
		if (!(r == FFUNICODE_UTF16LE || r == FFUNICODE_UTF16BE))
			return _ID3V2READ_WARN(d, "invalid BOM");
		ffstr_shift(&in, len);

		n = ffutf8_from_utf16(NULL, 0, in.ptr, in.len, r);
		if (NULL == ffvec_realloc(b, n, 1))
			return _ID3V2READ_WARN(d, "not enough memory");
		b->len = ffutf8_from_utf16(b->ptr, b->cap, in.ptr, in.len, r);
		break;
	}

	case ID3V2_UTF16BE:
		n = ffutf8_from_utf16(NULL, 0, in.ptr, in.len, FFUNICODE_UTF16BE);
		if (NULL == ffvec_realloc(b, n, 1))
			return _ID3V2READ_WARN(d, "not enough memory");
		b->len = ffutf8_from_utf16(b->ptr, b->cap, in.ptr, in.len, FFUNICODE_UTF16BE);
		break;

	default:
		return _ID3V2READ_WARN(d, "invalid encoding");
	}

	ffstr_setstr(out, b);
	return 0;
}

/** "LNG"[3] "DESCRIPTION" \0 "TEXT" */
static void _id3v2read_comment_parse(ffstr data, unsigned encoding, ffstr *body)
{
	int r;
	if (data.len < FFS_LEN("LNG\0"))
		goto end;

	ffstr_shift(&data, FFS_LEN("LNG"));
	switch (encoding) {
	case ID3V2_UTF8:
	case ID3V2_ANSI:
		if ((r = ffstr_findchar(&data, '\0')) >= 0)
			ffstr_shift(&data, r + 1);
		break;

	case ID3V2_UTF16BOM:
	case ID3V2_UTF16BE:
		if ((r = ffutf16_findchar(data.ptr, data.len, '\0')) >= 0)
			ffstr_shift(&data, r + 2);
		break;
	}

end:
	*body = data;
}

/** Read next ID3v2 tag field.
IMPORTANT: some input data may be kept in cache (read with _avp_stream_view(&d->stm)).
Return >0: enum ID3V2READ_R
	<=0: enum MMTAG */
static inline int id3v2read_process(struct id3v2read *d, ffstr *input, ffstr *name, ffstr *value)
{
	enum {
		R_INIT, R_GATHER, R_HDR, R_HDR_EXT,
		R_FR, R_FR_HDR, R_FR_DATA,
		R_UNSYNC,
		R_DATA, R_TRKTOTAL, R_PADDING,
	};
	int r;
	ffuint n;

	for (;;) {
		switch (d->state) {
		case R_INIT:
			d->state = R_GATHER,  d->nextstate = R_HDR,  d->gather_size = sizeof(struct id3v2_hdr) + 4;
			// fallthrough

		case R_GATHER:
			if (d->total_size && d->offset + d->gather_size > d->total_size)
				return _ID3V2READ_ERR(d, "corrupt data");
			if (_avp_stream_realloc(&d->stm, d->gather_size))
				return _ID3V2READ_ERR(d, "not enough memory");
			r = _avp_stream_gather(&d->stm, *input, d->gather_size, &d->chunk);
			ffstr_shift(input, r);
			if (d->chunk.len < d->gather_size)
				return ID3V2READ_MORE;
			d->chunk.len = d->gather_size;
			d->state = d->nextstate;
			continue;

		case R_HDR:
			if ((r = _id3v2r_hdr_read(d, d->chunk, &n))) {
				if (r == ID3V2READ_MORE) {
					d->state = R_GATHER,  d->nextstate = R_HDR_EXT,  d->gather_size = n;
					continue;
				}
				return r;
			}

			ffstr_shift(&d->chunk, sizeof(struct id3v2_hdr));
			_avp_stream_consume(&d->stm, sizeof(struct id3v2_hdr));
			d->offset += sizeof(struct id3v2_hdr);
			d->state = R_FR;
			continue;

		case R_HDR_EXT:
			ffstr_shift(&d->chunk, d->gather_size);
			_avp_stream_consume(&d->stm, d->gather_size);
			d->offset += d->gather_size;
			d->state = R_FR;
			continue;

		case R_FR:
			if (d->offset == d->total_size)
				return ID3V2READ_DONE;

			d->gather_size = ffmin(sizeof(struct id3v2_framehdr) + 4+1, d->total_size - d->offset);
			d->state = R_GATHER,  d->nextstate = R_FR_HDR;
			continue;

		case R_FR_HDR:
			if (d->chunk.ptr[0] == '\0') {
				n = ffmin(_avp_stream_used(&d->stm), d->total_size - d->offset);
				_avp_stream_consume(&d->stm, n);
				d->offset += n;
				d->state = R_PADDING;
				continue;
			}

			if ((r = _id3v2r_frame_read(d, d->chunk, &n)) > 0)
				return r;
			d->tag = -r;
			d->state = R_GATHER,  d->nextstate = R_FR_DATA,  d->gather_size = n;
			continue;

		case R_FR_DATA:
			if ((d->frame_flags & ID3V2_FRAME_UNSYNC) || (d->hdr_flags & ID3V2_HDR_UNSYNC)) {
				d->unsync_buf.len = 0;
				if (NULL == ffvec_grow(&d->unsync_buf, d->gather_size, 1))
					return _ID3V2READ_ERR(d, "not enough memory");
				d->state = R_UNSYNC;
				continue;
			}

			d->state = R_DATA;
			continue;

		case R_UNSYNC:
			_id3v2read_unsync(&d->unsync_buf, d->chunk);
			d->chunk = *(ffstr*)&d->unsync_buf;
			d->state = R_DATA;
			// fallthrough

		case R_DATA:
			_avp_stream_consume(&d->stm, d->gather_size);
			d->offset += d->gather_size;
			ffstr_setz(name, d->frame_id);
			d->state = R_FR;

			if (d->as_is) {
				*value = d->chunk;
				return -(int)d->tag;
			}

			ffstr_shift(&d->chunk, d->body_off);

			switch (d->tag) {
			case MMTAG_COMMENT:
			case MMTAG_LYRICS:
				_id3v2read_comment_parse(d->chunk, d->txtenc, &d->chunk);  break;

			case MMTAG_PICTURE:
				*value = d->chunk;
				return -MMTAG_PICTURE;
			}

			*value = d->chunk;
			if ((r = _id3v2read_text_decode(d, d->chunk, value)))
				return r;

			if (value->len && value->ptr[value->len - 1] == '\0')
				value->len--;

			switch (d->tag) {
			case MMTAG_UNKNOWN:
				if (!ffmem_cmp(d->frame_id, "TXXX", 4)) { // TXXX ... name \0 value
					ffstr k, v;
					if (ffstr_splitby(value, '\0', &k, &v) > 0) {
						*name = k;
						*value = v;
					}
				}
				break;

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

			return -(int)d->tag;

		case R_TRKTOTAL:
			ffstr_setz(name, d->frame_id);
			*value = d->chunk;
			d->state = R_FR;
			return -MMTAG_TRACKTOTAL;

		case R_PADDING:
			if (d->offset == d->total_size)
				return ID3V2READ_DONE;
			if (input->len == 0)
				return ID3V2READ_MORE;
			n = ffmin(d->total_size - d->offset, input->len);
			ffstr_shift(input, n);
			d->offset += n;
			break;
		}
	}
}

#undef _ID3V2READ_ERR
#undef _ID3V2READ_WARN

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

static int _id3v2write_addframe(struct id3v2write *w, const char *id, ffstr prefix, ffstr data, int encoding)
{
	ffsize n = sizeof(struct id3v2_framehdr) + !!(encoding >= 0) + prefix.len + data.len;

	if (NULL == ffvec_grow(&w->buf, n, 1))
		return -1;
	char *p = ffslice_end(&w->buf, 1);

	// write frame header
	struct id3v2_framehdr *fr = (struct id3v2_framehdr*)p;
	ffmem_copy(fr->id, id, 4);
	_id3v2_int7_write(fr->size, n - sizeof(*fr));
	fr->flags[0] = 0,  fr->flags[1] = 0;
	p += sizeof(*fr);

	if (encoding >= 0)
		*p++ = encoding;

	p = ffmem_copy(p, prefix.ptr, prefix.len);
	ffmem_copy(p, data.ptr, data.len);

	w->buf.len += n;
	return 0;
}

/** Prepare TRCK frame data: "TRACKNO [/ TRACKTOTAL]" */
static ffstr _id3v2_trackno(struct id3v2write *w)
{
	char *p = w->trackno;
	if (w->trackno[0] != '\0')
		p += ffsz_len(w->trackno);
	else
		*p++ = '0';

	if (w->tracktotal[0] != '\0') {
		*p++ = '/';
		p = ffmem_copy(p, w->tracktotal, ffsz_len(w->tracktotal));
	}

	ffstr d = FFSTR_INITN(w->trackno, p - w->trackno);
	return d;
}

/** Add frame
Return 0 on success
 >0 if tag field isn't supported
 <0 on error */
static inline int id3v2write_add(struct id3v2write *w, ffuint id, ffstr data, unsigned as_is)
{
	ffstr prefix = {};
	char buf[32];
	int txt_enc = (!as_is) ? ID3V2_UTF8 : -1;

	switch (id) {
	case MMTAG_COMMENT:
	case MMTAG_LYRICS:
		if (!as_is) {
			ffmem_copy(buf, "eng\0", 4);
			ffstr_set(&prefix, buf, 4);
		}
		break;

	case MMTAG_PICTURE:
		if (!as_is)
			return 1; // not implemented
		break;

	case MMTAG_TRACKNO:
		ffsz_copyn(w->trackno, sizeof(w->trackno), data.ptr, data.len);
		if (w->tracktotal[0] == '\0')
			return 0; // waiting for MMTAG_TRACKTOTAL
		data = _id3v2_trackno(w);
		break;

	case MMTAG_TRACKTOTAL:
		ffsz_copyn(w->tracktotal, sizeof(w->tracktotal), data.ptr, data.len);
		if (w->trackno[0] == '\0')
			return 0; // waiting for MMTAG_TRACKNO
		data = _id3v2_trackno(w);
		id = MMTAG_TRACKNO;
		break;

	case MMTAG_REPLAYGAIN_TRACK_GAIN:
		prefix.ptr = buf;
		prefix.len = ffmem_ncopy(buf, sizeof(buf), "REPLAYGAIN_TRACK_GAIN", 21+1);
		break;
	}

	static const char id3v2_frame_str_v24[][4] = {
		[MMTAG_ALBUM] =			"TALB",
		[MMTAG_ALBUMARTIST] =	"TPE2",
		[MMTAG_ARTIST] =		"TPE1",
		[MMTAG_COMMENT] =		"COMM",
		[MMTAG_COMPOSER] =		"TCOM",
		[MMTAG_COPYRIGHT] =		"TCOP",
		[MMTAG_DATE] =			"TYER",
		[MMTAG_GENRE] =			"TCON",
		[MMTAG_LYRICS] =		"USLT",
		[MMTAG_PICTURE] =		"APIC",
		[MMTAG_PUBLISHER] =		"TPUB",
		[MMTAG_REPLAYGAIN_TRACK_GAIN] = "TXXX",
		[MMTAG_TITLE] =			"TIT2",
		[MMTAG_TRACKNO] =		"TRCK",
		// MMTAG_DISCNUMBER,
	};

	if (id >= FF_COUNT(id3v2_frame_str_v24)
		|| id3v2_frame_str_v24[id][0] == '\0')
		return 1; // tag id not supported

	int r = _id3v2write_addframe(w, id3v2_frame_str_v24[id], prefix, data, txt_enc);
	if (r != 0)
		return r;

	if (id == MMTAG_TRACKNO) {
		w->trackno[0] = w->tracktotal[0] = '\0';
	}
	return 0;
}

static inline int id3v2write_add_txxx(struct id3v2write *w, ffstr name, ffstr data)
{
	char buf[256];
	if (name.len >= sizeof(buf))
		return 1; // too large name
	*(char*)ffmem_copy(buf, name.ptr, name.len) = '\0';
	name.ptr = buf;
	name.len++;
	return _id3v2write_addframe(w, "TXXX", name, data, ID3V2_UTF8);
}

/** Finalize ID3v2 tag data */
static inline int id3v2write_finish(struct id3v2write *w, ffsize padding)
{
	if (w->trackno[0] != '\0' || w->tracktotal[0] != '\0') {
		if (0 != _id3v2write_addframe(w, "TRCK", FFSTR_Z(""), _id3v2_trackno(w), 1))
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
