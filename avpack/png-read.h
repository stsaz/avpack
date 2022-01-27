/** avpack: .png reader
2018,2021, Simon Zolin
*/

/*
pngread_open pngread_close
pngread_process
pngread_error
pngread_info
*/

/* .png format:
SIGN IHDR IDAT... IEND
*/

#pragma once

#include <ffbase/vector.h>

struct png_info {
	ffuint width, height;
	ffuint bpp;
};

typedef struct pngread {
	ffuint state, nextstate;
	ffuint gather_size;
	const char *error;
	ffvec buf;
	ffstr chunk;
	struct png_info info;
} pngread;

static inline void pngread_open(pngread *p)
{
	(void)p;
}

static inline void pngread_close(pngread *p)
{
	ffvec_free(&p->buf);
}

struct png_chunk {
	ffbyte datalen[4];
	ffbyte type[4];
	ffbyte data[0];
	// ffbyte crc[4];
};

enum PNG_CLR {
	PNG_PLT = 1,
	PNG_CLR = 2,
	PNG_ALPHA = 4,
};
struct png_ihdr {
	ffbyte width[4];
	ffbyte height[4];
	ffbyte bit_depth;
	ffbyte color; // bitmask enum PNG_CLR
	ffbyte unused[3];
};

/** Read IHDR data. */
static inline int png_ihdr_read(struct png_info *info, ffstr data)
{
	const struct png_ihdr *ihdr = (struct png_ihdr*)data.ptr;
	info->width = ffint_be_cpu32_ptr(ihdr->width);
	info->height = ffint_be_cpu32_ptr(ihdr->height);
	switch (ihdr->color) {
	case PNG_CLR:
		info->bpp = 24;
		break;
	case PNG_CLR | PNG_ALPHA:
		info->bpp = 32;
		break;
	default:
		info->bpp = 8;
	}
	return 0;
}

#define _PNGR_ERR(p, e) \
	(p)->error = (e),  PNGREAD_ERROR

static inline const char* pngread_error(pngread *p)
{
	return p->error;
}

#define _PNGR_GATHER(p, _nextstate, len) \
	(p)->state = R_GATHER,  (p)->nextstate = _nextstate,  (p)->gather_size = len

enum PNGREAD_R {
	PNGREAD_MORE,
	PNGREAD_HEADER,
	PNGREAD_DONE,
	PNGREAD_ERROR,
};

/**
Return enum PNGREAD_R. */
static inline int pngread_process(pngread *p, ffstr *input, ffstr *output)
{
	enum PNG_R {
		R_INIT, R_GATHER,
		R_SIGN, R_IHDR_HDR, R_IHDR,
		R_ERR,
	};
	static const ffbyte png_sign[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	int r;

	for (;;) {
		switch (p->state) {

		case R_INIT:
			_PNGR_GATHER(p, R_SIGN, sizeof(png_sign) + sizeof(struct png_chunk));
			continue;

		case R_SIGN:
			if (!!ffmem_cmp(p->chunk.ptr, png_sign, sizeof(png_sign)))
				return _PNGR_ERR(p, "bad PNG signature");

			ffstr_shift(&p->chunk, sizeof(png_sign));
			p->state = R_IHDR_HDR;
			continue;

		case R_IHDR_HDR: {
			const struct png_chunk *d = (struct png_chunk*)p->chunk.ptr;
			ffuint len = ffint_be_cpu32_ptr(d->datalen);
			if (!!ffmem_cmp(d->type, "IHDR", 4)
				|| len < sizeof(struct png_ihdr))
				return _PNGR_ERR(p, "bad header");

			_PNGR_GATHER(p, R_IHDR, len);
			continue;
		}

		case R_IHDR:
			png_ihdr_read(&p->info, p->chunk);
			p->state = R_ERR;
			ffstr_setstr(output, &p->chunk);
			return PNGREAD_HEADER;

		case R_ERR:
			return _PNGR_ERR(p, "data reading isn't implemented");

		case R_GATHER:
			r = ffstr_gather((ffstr*)&p->buf, &p->buf.cap, input->ptr, input->len, p->gather_size, &p->chunk);
			if (r < 0)
				return _PNGR_ERR(p, "no memory");
			ffstr_shift(input, r);
			if (p->chunk.len == 0)
				return PNGREAD_MORE;
			p->state = p->nextstate;
			p->buf.len = 0;
			continue;
		}
	}
}

#undef _PNGR_GATHER

static inline const struct png_info* pngread_info(pngread *p)
{
	return &p->info;
}
