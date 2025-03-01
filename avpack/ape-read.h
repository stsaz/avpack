/** avpack: .ape reader
2021, Simon Zolin
*/

/*
aperead_open aperead_close
aperead_process
aperead_info
aperead_seek
aperead_error
aperead_tag
aperead_offset
aperead_cursample
aperead_block_samples
aperead_align4
*/

#pragma once
#include <avpack/base/ape.h>
#include <avpack/id3v1.h>
#include <avpack/apetag.h>
#include <ffbase/vector.h>

typedef struct aperead {
	ffuint state;
	ffuint nextstate;
	const char *error;
	ffvec buf;
	ffstr chunk;
	ffuint gather_size;
	struct ape_info info;
	ffuint64 total_size;
	ffuint64 off;

	ffuint block_start, block_samples;
	ffuint iblock;
	ffuint align4;
	ffuint shift;
	ffuint *seektab;
	ffuint64 seek_sample;

	struct id3v1read id3v1;
	struct apetagread apetag;
	ffstr tagname, tagval;
	int tag;
} aperead;

enum APEREAD_R {
	/** New tag is read: use aperead_tag() */
	APEREAD_ID31 = 1,
	APEREAD_APETAG,

	/** Need more input data */
	APEREAD_MORE,

	/** Output contains file header */
	APEREAD_HEADER,

	/** Output contains audio data block */
	APEREAD_DATA,

	APEREAD_DONE,

	/** Need more input data at offset aperead_offset() */
	APEREAD_SEEK,

	APEREAD_ERROR,
	APEREAD_WARN,
};

#define _APER_ERR(a, msg) \
	(a)->error = (msg),  APEREAD_ERROR

static inline const char* aperead_error(aperead *a)
{
	return a->error;
}

static inline void aperead_open(aperead *a, ffuint64 total_size)
{
	a->total_size = total_size;
	a->seek_sample = (ffuint64)-1;
}

static inline void aperead_close(aperead *a)
{
	ffvec_free(&a->buf);
	apetagread_close(&a->apetag);
	ffmem_free(a->seektab);  a->seektab = NULL;
}

/**
Return enum APEREAD_R */
static inline int aperead_process(aperead *a, ffstr *input, ffstr *output)
{
	enum {
		R_FTR_SEEK, R_ID3V1, R_APETAG_FTR, R_APETAG, R_HDR_SEEK,
		R_HDR, R_SEEKTAB, R_BLOCK_GATHER, R_BLOCK,
		R_GATHER,
	};
	int r;

	for (;;) {
		switch (a->state) {
		case R_HDR:
			r = ape_hdr_read(&a->info, a->chunk.ptr, a->chunk.len);
			if (r <= 0)
				return _APER_ERR(a, "bad header");
			ffstr_set(output, a->chunk.ptr, r);
			a->state = R_GATHER,  a->nextstate = R_SEEKTAB,  a->gather_size = a->info.seekpoints * 4;
			return APEREAD_HEADER;

		case R_SEEKTAB:
			if (!(a->seektab = (ffuint*)ffmem_alloc((a->info.seekpoints + 1) * 4)))
				return _APER_ERR(a, "no memory");
			if (0 > (r = ape_seektab_read(a->seektab, a->chunk.ptr, a->chunk.len, a->total_size)))
				return _APER_ERR(a, "bad frames table");
			a->info.seekpoints = r;
			a->state = R_BLOCK_GATHER;
			if (NULL == ffvec_realloc(&a->buf, 4, 1))
				return _APER_ERR(a, "no memory");
			// fallthrough

		case R_BLOCK_GATHER: {
			if (a->seek_sample != (ffuint64)-1) {
				a->iblock = a->seek_sample / a->info.block_samples;
				if (a->iblock >= a->info.seekpoints)
					return _APER_ERR(a, "can't seek");
				a->seek_sample = (ffuint64)-1;
				a->shift = 0;
				ffuint align4 = (a->seektab[a->iblock] - a->seektab[0]) % 4;
				a->off = a->seektab[a->iblock] - align4;
				return APEREAD_SEEK;
			}

			if (a->shift) {
				a->shift = 0;
				ffuint off2 = a->seektab[a->iblock + 1];
				ffuint align4 = (off2 - a->seektab[0]) % 4;
				if (align4 != 0) {
					// preserve the trailing bytes in the buffer
					ffmem_move(a->buf.ptr, &a->chunk.ptr[a->chunk.len - 4], 4);
					a->buf.len = 4;
				}
				a->iblock++;
			}

			if (a->iblock >= a->info.seekpoints) {
				output->len = 0;
				return APEREAD_DONE;
			}

			ffuint off1 = a->seektab[a->iblock];
			ffuint off2 = a->seektab[a->iblock + 1];
			a->block_start = a->iblock * a->info.block_samples;
			a->block_samples = (a->iblock + 1 != a->info.seekpoints) ? a->info.block_samples : a->info.lastframe_blocks;
			a->align4 = (off1 - a->seektab[0]) % 4;
			off1 -= a->align4;
			if (a->iblock + 1 != a->info.seekpoints) {
				ffuint align4 = (off2 - a->seektab[0]) % 4;
				if (align4 != 0)
					off2 += 4 - align4;
			}
			a->state = R_GATHER,  a->nextstate = R_BLOCK,  a->gather_size = off2 - off1;
			continue;
		}

		case R_BLOCK:
			*output = a->chunk;
			a->shift = 1;
			a->state = R_BLOCK_GATHER;
			return APEREAD_DATA;

		case R_GATHER:
			r = ffstr_gather((ffstr*)&a->buf, &a->buf.cap, input->ptr, input->len, a->gather_size, &a->chunk);
			if (r < 0)
				return _APER_ERR(a, "not enough memory");
			ffstr_shift(input, r);
			a->off += r;
			if (a->chunk.len == 0)
				return APEREAD_MORE;
			a->state = a->nextstate;
			a->buf.len = 0;
			continue;


		case R_FTR_SEEK:
			if (a->total_size == 0) {
				a->state = R_GATHER,  a->nextstate = R_HDR,  a->gather_size = ape_hdr_read(NULL, NULL, 0);
				continue;
			}

			a->state = R_GATHER,  a->nextstate = R_ID3V1;
			a->gather_size = sizeof(struct apetaghdr) + sizeof(struct id3v1);
			a->gather_size = ffmin(a->gather_size, a->total_size);
			a->off = a->total_size - a->gather_size;
			return APEREAD_SEEK;

		case R_ID3V1: {
			if (sizeof(struct id3v1) > a->chunk.len) {
				a->state = R_APETAG_FTR;
				continue;
			}

			ffstr id3;
			ffstr_set(&id3, &a->chunk.ptr[a->chunk.len - sizeof(struct id3v1)], sizeof(struct id3v1));
			r = id3v1read_process(&a->id3v1, id3, &a->tagval);

			switch (r) {
			case ID3V1READ_DONE:
				a->total_size -= sizeof(struct id3v1);
				a->chunk.len -= sizeof(struct id3v1);
				a->off -= sizeof(struct id3v1);
				break;

			case ID3V1READ_NO:
				break;

			default:
				a->tag = -r;
				return APEREAD_ID31;
			}
		}
			// fallthrough

		case R_APETAG_FTR: {
			apetagread_open(&a->apetag);
			ffint64 seek;
			r = apetagread_footer(&a->apetag, a->chunk, &seek);

			switch (r) {
			case APETAGREAD_SEEK:
				a->off += seek;
				a->total_size = a->off;
				a->state = R_APETAG;
				return APEREAD_SEEK;

			default:
				a->state = R_HDR_SEEK;
				continue;
			}
			break;
		}

		case R_APETAG: {
			ffsize len = input->len;
			r = apetagread_process(&a->apetag, input, &a->tagname, &a->tagval);
			a->off += len - input->len;

			switch (r) {
			case APETAGREAD_MORE:
				return APEREAD_MORE;

			case APETAGREAD_DONE:
				apetagread_close(&a->apetag);
				a->state = R_HDR_SEEK;
				break;

			case APETAGREAD_ERROR:
				a->state = R_HDR_SEEK;
				a->error = apetagread_error(&a->apetag);
				return APEREAD_WARN;

			default:
				a->tag = -r;
				return APEREAD_APETAG;
			}
		}
			// fallthrough

		case R_HDR_SEEK:
			a->state = R_GATHER,  a->nextstate = R_HDR,  a->gather_size = ape_hdr_read(NULL, NULL, 0);
			a->off = 0;
			return APEREAD_SEEK;
		}
	}
}

static inline void aperead_seek(aperead *a, ffuint64 sample)
{
	a->seek_sample = sample;
}

/**
Return enum MMTAG */
static inline int aperead_tag(aperead *a, ffstr *name, ffstr *val)
{
	*name = a->tagname;
	*val = a->tagval;
	return a->tag;
}

#define aperead_offset(a)  ((a)->off)

#define aperead_cursample(a)  ((a)->block_start)
#define aperead_block_samples(a)  ((a)->block_samples)
#define aperead_align4(a)  ((a)->align4)

#undef _APER_ERR
