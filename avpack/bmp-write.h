/** avpack: .bmp writer
2016,2023, Simon Zolin
*/

/*
bmpwrite_create bmpwrite_close
bmpwrite_process
bmpwrite_error
bmpwrite_line
bmpwrite_seek_offset
bmpwrite_size
*/

#pragma once

#include <avpack/bmp-fmt.h>
#include <ffbase/vector.h>

typedef struct bmpwrite_info {
	ffuint width, height;
	ffuint bpp;
} bmpwrite_info;

typedef struct bmpwrite {
	ffuint state;
	ffuint linesize_al, linesize;
	ffuint data_off;
	ffvec buf;
	ffuint line;
	ffuint64 seek_off;
	const char *error;

	bmpwrite_info info;
	ffuint input_reverse :1;
} bmpwrite;

#define _BMPW_ERR(b, e) \
	(b)->error = (e),  BMPWRITE_ERROR

#define bmpwrite_error(b)  (b)->error

enum BMPWRITE_R {
	BMPWRITE_MORE,
	BMPWRITE_HEADER, // get info with bmpread_info()
	BMPWRITE_SEEK, // output data must be written at offset = bmpwrite_seek_offset()
	BMPWRITE_DATA,
	BMPWRITE_DONE,
	BMPWRITE_ERROR, // bmpwrite_error() returns error message
};

/**
flags: =1: input lines order is HEIGHT..1, as in .bmp */
static inline int bmpwrite_create(bmpwrite *b, bmpwrite_info *info, ffuint flags)
{
	b->input_reverse = !!(flags & 1);
	b->info = *info;
	return 0;
}

static inline void bmpwrite_close(bmpwrite *b)
{
	ffvec_free(&b->buf);
}

/** Write .bmp header.
Return header size */
static int bmp_hdr_write(bmpwrite *b, void *dst)
{
	struct bmp_hdr *h = (struct bmp_hdr*)dst;
	ffmem_zero_obj(h);
	ffmem_copy(h->bm, "BM", 2);
	*(ffuint*)h->width = ffint_le_cpu32(b->info.width);
	*(ffuint*)h->height = ffint_le_cpu32(b->info.height);
	*(ffuint*)h->bpp = ffint_le_cpu32(b->info.bpp);
	*(ffushort*)h->planes = ffint_le_cpu16(1);
	*(ffuint*)h->sizeimage = ffint_le_cpu32(b->info.height * b->linesize_al);

	ffuint hdrsize = sizeof(struct bmp_hdr);
	if (b->info.bpp == 32) {
		hdrsize = sizeof(struct bmp_hdr) + sizeof(struct bmp_hdr4);
		*(ffuint*)h->compression = ffint_le_cpu32(BMP_COMP_BITFIELDS);
		struct bmp_hdr4 *h4 = (struct bmp_hdr4*)(h + 1);
		ffmem_zero_obj(h4);
		*(ffuint*)h4->mask_rgba = ffint_be_cpu32(0x000000ff);
		*(ffuint*)(h4->mask_rgba+4) = ffint_be_cpu32(0x0000ff00);
		*(ffuint*)(h4->mask_rgba+8) = ffint_be_cpu32(0x00ff0000);
		*(ffuint*)(h4->mask_rgba+12) = ffint_be_cpu32(0xff000000);
		ffmem_copy(h4->cstype, "BGRs", 4);
	}

	*(ffuint*)h->infosize = ffint_le_cpu32(hdrsize - 14);
	*(ffuint*)h->headersize = ffint_le_cpu32(hdrsize);
	*(ffuint*)h->filesize = ffint_le_cpu32(hdrsize + b->info.height * b->linesize_al);

	return hdrsize;
}

static inline int bmpwrite_process(bmpwrite *b, ffstr *input, ffstr *output)
{
	enum { W_HDR, W_MORE, W_SEEK, W_DATA, W_PAD };

	for (;;) {
		switch (b->state) {

		case W_HDR: {
			b->linesize_al = ffint_align_ceil2(b->info.width * (b->info.bpp / 8), 4);
			b->linesize = b->info.width * b->info.bpp / 8;

			if (NULL == ffvec_alloc(&b->buf, sizeof(struct bmp_hdr) + sizeof(struct bmp_hdr4), 1))
				return _BMPW_ERR(b, "no memory");

			int r = bmp_hdr_write(b, b->buf.ptr);
			b->data_off = r;
			ffstr_set(output, b->buf.ptr, r);
			b->state = W_SEEK;
			return BMPWRITE_DATA;
		}

		case W_SEEK:
			b->state = W_DATA;
			if (b->input_reverse)
				continue;
			b->seek_off = b->data_off + (b->info.height - b->line - 1) * b->linesize_al;
			return BMPWRITE_SEEK;

		case W_DATA:
			if (input->len < b->linesize) {
				if (input->len != 0)
					return _BMPW_ERR(b, "incomplete input line");
				return BMPWRITE_MORE;
			}

			ffstr_set(output, input->ptr, b->linesize);
			ffstr_shift(input, b->linesize);
			b->line++;
			b->state = W_PAD;
			return BMPWRITE_DATA;

		case W_PAD:
			if (b->linesize_al == b->linesize) {
				b->state = W_MORE;
				continue;
			}
			ffmem_zero(b->buf.ptr, b->linesize_al - b->linesize);
			ffstr_set(output, b->buf.ptr, b->linesize_al - b->linesize);
			b->state = W_MORE;
			return BMPWRITE_DATA;

		case W_MORE:
			if (b->line == b->info.height)
				return BMPWRITE_DONE;
			b->state = W_SEEK;
			continue;
		}
	}
}

#define bmpwrite_line(b)  ((b)->line)

#define bmpwrite_seek_offset(b)  (b)->seek_off

/** Get file size */
static inline ffuint bmpwrite_size(bmpwrite *b)
{
	ffuint lnsize = b->info.width * (b->info.bpp / 8);
	return b->info.height * ffint_align_ceil2(lnsize, 4);
}
