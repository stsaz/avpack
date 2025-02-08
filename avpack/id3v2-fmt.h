/** avpack: ID3v2 tag
2021, Simon Zolin */

/*
id3v2_hdr_read id3v2_hdr_write
id3v2_frame_read id3v2_frame_write
id3v2_data_decode
*/

/** ID3v2 format:
HEADER [EXT-HDR]
(FRAME-HEADER  [TEXT-ENCODING]  DATA...)...
PADDING
*/

#pragma once
#include <ffbase/string.h>

struct id3v2_taghdr {
	char id3[3]; // "ID3"
	ffbyte ver[2]; // e.g. \4\0 for v2.4
	ffbyte flags; // enum ID3V2_HDR_F
	ffbyte size[4]; // 7-bit bytes
	// ext_len[4]
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
	// text_encoding[1] // enum ID3V2_TXTENC - for ID starting with 'T' or 'COM', or 'APIC'
};

enum ID3V2_FRAME_F {
	ID3V2_FRAME_DATALEN = 1, // 4 bytes follow the frame header
	ID3V2_FRAME_UNSYNC = 2,
};

struct id3v22_framehdr {
	char id[3];
	ffbyte size[3];
	// text_encoding[1] // enum ID3V2_TXTENC
};

enum ID3V2_TXTENC {
	ID3V2_ANSI,
	ID3V2_UTF16BOM,
	ID3V2_UTF16BE,
	ID3V2_UTF8,
};


/** Encode integer so that it doesn't contain 0xff byte */
static inline void _id3v2_int7_write(void *dst, unsigned i)
{
	unsigned i7 = (i & 0x0000007f)
		| ((i & (0x0000007f << 7)) << 1)
		| ((i & (0x0000007f << 14)) << 2)
		| ((i & (0x0000007f << 21)) << 3);
	*(unsigned*)dst = ffint_be_cpu32(i7);
}

/** Decode integer */
static inline unsigned _id3v2_int7_read(unsigned i)
{
	if (i & 0x80808080)
		return ~0U;
	return ((i & 0x7f000000) >> 3)
		| ((i & 0x007f0000) >> 2)
		| ((i & 0x00007f00) >> 1)
		| (i & 0x0000007f);
}

/** Replace: FF 00 -> FF */
static inline unsigned id3v2_data_decode(void *dst, ffstr in)
{
	char *p = dst;
	unsigned skip0 = 0;

	for (size_t i = 0;  i < in.len;  i++) {

		if (skip0) {
			skip0 = 0;
			*p++ = 0xff;
			if (in.ptr[i] == 0)
				continue;
		}

		if ((ffbyte)in.ptr[i] == 0xff) {
			skip0 = 1;
			continue;
		}

		*p++ = in.ptr[i];
	}
	return p - (char*)dst;
}


struct id3v2_hdr {
	unsigned version;
	unsigned flags;
	unsigned size;
};

/**
Return >0: full header size;
	<0: error */
static inline int id3v2_hdr_read(struct id3v2_hdr *h, ffstr data)
{
	if (sizeof(struct id3v2_taghdr) + 4 > data.len)
		return -1;
	const struct id3v2_taghdr *th = (struct id3v2_taghdr*)data.ptr;
	unsigned n;

	if (!(!ffmem_cmp(data.ptr, "ID3", 3)
		&& ~0U != (n = _id3v2_int7_read(ffint_be_cpu32_ptr(th->size)))))
		return -1;

	h->size = sizeof(struct id3v2_taghdr) + n;
	h->version = th->ver[0];
	h->flags = th->flags;

	switch (h->version) {
	case 3:
		if ((h->flags & ~(ID3V2_HDR_UNSYNC | ID3V2_HDR_EXT)) != 0)
			return -1; // header flags not supported

		if (h->flags & ID3V2_HDR_EXT)
			return sizeof(struct id3v2_taghdr) + 4 + ffint_be_cpu32_ptr(th + 1);
		break;

	case 2:
	case 4:
		if ((h->flags & ~ID3V2_HDR_UNSYNC) != 0)
			return -1; // header flags not supported
		break;

	default:
		return -1; // version not supported
	}

	return sizeof(struct id3v2_taghdr);
}

static inline unsigned id3v2_hdr_write(void *dst, unsigned data_len)
{
	char *p = dst;
	ffmem_copy(p, "ID3\x04\x00", 5);
	p[5] = 0;
	_id3v2_int7_write(p + 6, data_len);
	return 10;
}


struct id3v2_frame {
	char id[5];
	unsigned flags;
	unsigned encoding;
	unsigned size;
};

/**
Return N of bytes read;
	<0: error */
static inline int id3v2_frame_read(struct id3v2_frame *f, ffstr data, unsigned version)
{
	const struct id3v2_framehdr *h = (struct id3v2_framehdr*)data.ptr;
	unsigned id, i, hdr_len = sizeof(struct id3v2_framehdr), n;
	i = hdr_len;
	ffmem_copy(f->id, data.ptr, 4);
	n = ffint_be_cpu32_ptr(data.ptr + 4);
	f->flags = h->flags[1];

	switch (version) {
	case 2:
		i = hdr_len = sizeof(struct id3v22_framehdr);
		f->id[3] = '\0';
		n = ffint_be_cpu24_ptr(data.ptr + 3);
		f->flags = 0;
		break;

	case 3:
		if (n & 0x80000000)
			return -1;

		if (f->flags)
			return -1;
		break;

	case 4:
		if (~0U == (n = _id3v2_int7_read(n)))
			return -1;

		if (f->flags & ~(ID3V2_FRAME_DATALEN | ID3V2_FRAME_UNSYNC))
			return -1;

		if (f->flags & ID3V2_FRAME_DATALEN) {
			if (4 > n || i + 4 > data.len)
				return -1;
			i += 4;
		}
		break;
	}

	f->encoding = ID3V2_ANSI;
	id = *(unsigned*)f->id;
	if (data.ptr[0] == 'T'
		|| (version != 2
			&& (id == *(unsigned*)"APIC"
				|| id == *(unsigned*)"COMM"
				|| id == *(unsigned*)"USLT"))
		|| (version == 2
			&& (id == *(unsigned*)"COM\0"
				|| id == *(unsigned*)"PIC\0"))) {
		if (i - hdr_len + 1 > n || i + 1 > data.len)
			return -1;
		f->encoding = (ffbyte)data.ptr[i];
		i++;
	}

	f->size = hdr_len + n;
	return i;
}

static inline unsigned id3v2_frame_write(void *dst, const char *id, int encoding, unsigned data_len)
{
	if (!dst)
		return 10 + !!(encoding >= 0);

	char *p = dst;
	ffmem_copy(p, id, 4);
	_id3v2_int7_write(p + 4, !!(encoding >= 0) + data_len);
	p[8] = 0,  p[9] = 0;

	if (encoding >= 0) {
		p[10] = encoding;
		return 11;
	}

	return 10;
}
