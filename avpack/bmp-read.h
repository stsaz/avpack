/** avpack: .bmp reader
2016,2021, Simon Zolin
*/

/*
bmpread_open bmpread_close
bmpread_process
bmpread_error
bmpread_info
bmpread_line
bmpread_seek_offset
*/

#pragma once

#include <avpack/bmp-fmt.h>
#include <ffbase/vector.h>

struct bmp_info {
	ffuint width, height;
	ffuint bpp;
};

typedef struct bmpread {
	ffuint state, nextstate;
	ffuint gather_size;
	const char *error;
	ffvec buf;
	ffstr chunk;
	struct bmp_info info;
	ffuint linesize, linesize_al;
	ffuint64 seek_off;
	ffuint line;
	ffuint data_off;
} bmpread;

static inline void bmpread_open(bmpread *b)
{
	(void)b;
}

static inline void bmpread_close(bmpread *b)
{
	ffvec_free(&b->buf);
}

enum BMPREAD_R {
	BMPREAD_MORE,
	BMPREAD_HEADER, // get info with bmpread_info()
	BMPREAD_SEEK, // provide input data at offset = bmpread_seek_offset()
	BMPREAD_LINE,
	BMPREAD_DONE,
	BMPREAD_ERROR, // bmpread_error() returns error message
};

#define _BMPR_ERR(b, e) \
	(b)->error = (e),  BMPREAD_ERROR

static inline const char* bmpread_error(bmpread *b)
{
	return b->error;
}

/** Read header.
Return header size (negative). */
static int bmp_hdr_read(bmpread *b, struct bmp_info *info, const void *data)
{
	const struct bmp_hdr *h = (struct bmp_hdr*)data;
	if (!!ffmem_cmp(h->bm, "BM", 2))
		return _BMPR_ERR(b, "bad header");

	ffuint hdrsize = ffint_le_cpu32_ptr(h->headersize);
	if (hdrsize < sizeof(struct bmp_hdr))
		return _BMPR_ERR(b, "bad header");

	info->width = ffint_le_cpu32_ptr(h->width);
	info->height = ffint_le_cpu32_ptr(h->height);
	ffuint comp = ffint_le_cpu32_ptr(h->compression);

	info->bpp = ffint_le_cpu16_ptr(h->bpp);
	switch (info->bpp) {
	case 24:
		if (comp != BMP_COMP_NONE)
			return _BMPR_ERR(b, "unsupported compression method");
		break;

	case 32:
		if (comp != BMP_COMP_BITFIELDS)
			return _BMPR_ERR(b, "unsupported compression method");

		if (hdrsize < sizeof(struct bmp_hdr) + sizeof(struct bmp_hdr4))
			return _BMPR_ERR(b, "unsupported format");

		return 0x4;

	default:
		return _BMPR_ERR(b, "unsupported format");
	}

	return -hdrsize;
}

#define _BMPR_GATHER(b, _nextstate, len) \
	(b)->state = R_GATHER,  (b)->nextstate = _nextstate,  (b)->gather_size = len

/**
Return enum BMPREAD_R. */
static inline int bmpread_process(bmpread *b, ffstr *input, ffstr *output)
{
	(void)output;
	enum {
		R_INIT, R_HDR, R_HDR4, R_SEEK, R_DATA, R_DONE,
		R_GATHER,
	};
	ffssize r;

	for (;;) {
		switch (b->state) {

		case R_INIT:
			_BMPR_GATHER(b, R_HDR, sizeof(struct bmp_hdr));
			break;

		case R_HDR:
			r = bmp_hdr_read(b, &b->info, b->chunk.ptr);
			switch (r) {
			case 0x4:
				_BMPR_GATHER(b, R_HDR4, sizeof(struct bmp_hdr4));
				continue;
			case BMPREAD_ERROR:
				return BMPREAD_ERROR;
			}
			b->data_off = -r;
			b->linesize = b->info.width * b->info.bpp / 8;
			b->linesize_al = ffint_align_ceil2(b->linesize, 4);
			b->state = R_SEEK;
			return BMPREAD_HEADER;

		case R_HDR4: {
			const struct bmp_hdr4 *h4 = (struct bmp_hdr4*)b->chunk.ptr;
			if (0x000000ff != ffint_be_cpu32_ptr(h4->mask_rgba)
				|| 0x0000ff00 != ffint_be_cpu32_ptr(h4->mask_rgba + 4)
				|| 0x00ff0000 != ffint_be_cpu32_ptr(h4->mask_rgba + 8)
				|| 0xff000000 != ffint_be_cpu32_ptr(h4->mask_rgba + 12)
				|| !!ffmem_cmp(h4->cstype, "BGRs", 4))
				return _BMPR_ERR(b, "unsupported ver.4 header");
			b->state = R_SEEK;
			return BMPREAD_HEADER;
		}

		case R_SEEK:
			b->seek_off = b->data_off + (b->info.height - b->line - 1) * b->linesize_al;
			_BMPR_GATHER(b, R_DATA, b->linesize_al);
			return BMPREAD_SEEK;

		case R_DATA:
			ffstr_set(output, b->chunk.ptr, b->linesize);
			b->state = R_SEEK;
			if (++b->line == b->info.height)
				b->state = R_DONE;
			return BMPREAD_LINE;

		case R_DONE:
			return BMPREAD_DONE;

		case R_GATHER:
			r = ffstr_gather((ffstr*)&b->buf, &b->buf.cap, input->ptr, input->len, b->gather_size, &b->chunk);
			if (r < 0)
				return _BMPR_ERR(b, "no memory");
			ffstr_shift(input, r);
			if (b->chunk.len == 0)
				return BMPREAD_MORE;
			b->state = b->nextstate;
			b->buf.len = 0;
			continue;
		}
	}
}

#undef _BMPR_GATHER

static inline const struct bmp_info* bmpread_info(bmpread *b)
{
	return &b->info;
}

#define bmpread_line(b)  ((b)->line)

#define bmpread_seek_offset(b)  ((b)->seek_off)
