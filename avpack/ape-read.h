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

/* .ape format:
"MAC " INFO SEEK_TABLE DATA... [APETAG] [ID3v1]
*/

#pragma once

#include <avpack/id3v1.h>
#include <avpack/apetag.h>
#include <ffbase/vector.h>

struct ape_info {
	ffuint seekpoints;
	ffuint block_samples;
	ffuint lastframe_blocks;
};

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

struct ape_desc {
	char id[4]; // "MAC "
	ffbyte ver[2]; // = x.xx * 1000.  >=3.98
	ffbyte skip[2];

	ffbyte desc_size[4];
	ffbyte hdr_size[4];
	ffbyte seektbl_size[4];
	ffbyte wavhdr_size[4];
	ffbyte unused[3 * 4];
	ffbyte md5[16];
};

struct ape_hdr {
	ffbyte comp_level[2];
	ffbyte flags[2];

	ffbyte frame_blocks[4];
	ffbyte lastframe_blocks[4];
	ffbyte total_frames[4];

	ffbyte bps[2];
	ffbyte channels[2];
	ffbyte rate[4];
};

enum {
	APE_HDR_MIN = sizeof(struct ape_desc) + sizeof(struct ape_hdr),
};

/**
Return header length */
static int _aper_hdr_read(aperead *a, ffstr d)
{
	if (ffmem_cmp(d.ptr, "MAC ", 4)) {
		a->error = "bad header";
		return -1;
	}

	ffuint ver = ffint_le_cpu16_ptr(&d.ptr[4]);
	if (ver < 3980) {
		a->error = "version not supported";
		return -1;
	}

	const struct ape_desc *ds = (struct ape_desc*)d.ptr;
	ffuint desc_size = ffint_le_cpu32_ptr(ds->desc_size);
	ffuint hdr_size = ffint_le_cpu32_ptr(ds->hdr_size);
	if (desc_size < sizeof(struct ape_desc)
		|| hdr_size < sizeof(struct ape_hdr)
		|| (ffuint64)desc_size + hdr_size > d.len)
		return 0;

	a->info.seekpoints = ffint_le_cpu32_ptr(ds->seektbl_size) / 4;

	const struct ape_hdr *h = (struct ape_hdr*)&d.ptr[desc_size];
	a->info.block_samples = ffint_le_cpu32_ptr(h->frame_blocks);
	a->info.lastframe_blocks = ffint_le_cpu32_ptr(h->lastframe_blocks);

	return desc_size + hdr_size;
}

/** Parse seek table (file offsets of ape blocks)
Return 0 on success */
static int _aper_seektab_read(aperead *a, ffstr data)
{
	ffuint n = data.len / 4;
	ffuint *sp;
	if (NULL == (sp = (ffuint*)ffmem_alloc((n+1) * 4)))
		return _APER_ERR(a, "no memory");

	ffuint off_prev = 0;
	const ffuint *offsets = (ffuint*)data.ptr;
	ffuint i;
	for (i = 0;  i != n;  i++) {
		ffuint off = ffint_le_cpu32(offsets[i]);
		if (off_prev >= off)
			break; // offsets must grow
		sp[i] = off;
		off_prev = off;
	}

	if (off_prev >= a->total_size)
		goto err;
	sp[i] = a->total_size;
	a->seektab = sp;
	a->info.seekpoints = i;
	return 0;

err:
	ffmem_free(sp);
	return _APER_ERR(a, "bad frames table");
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
			r = _aper_hdr_read(a, a->chunk);
			if (r < 0)
				return APEREAD_ERROR;
			ffstr_set(output, a->chunk.ptr, r);
			a->state = R_GATHER,  a->nextstate = R_SEEKTAB,  a->gather_size = a->info.seekpoints * 4;
			return APEREAD_HEADER;

		case R_SEEKTAB:
			if (0 != (r = _aper_seektab_read(a, a->chunk)))
				return r;
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
				a->state = R_GATHER,  a->nextstate = R_HDR,  a->gather_size = APE_HDR_MIN;
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
			a->state = R_GATHER,  a->nextstate = R_HDR,  a->gather_size = APE_HDR_MIN;
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
