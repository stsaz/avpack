/** avpack: AAC-ADTS (.aac) reader
2021, Simon Zolin
*/

/*
aacread_open aacread_close
aacread_process
aacread_error
aacread_offset
aacread_info
aacread_frame_samples
aacadts_find
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/adts.h>
#include <ffbase/stream.h>


enum AACREAD_R {
	AACREAD_MORE = AVPK_MORE,
	AACREAD_DATA = AVPK_DATA,
	AACREAD_FRAME = AVPK_DATA,
	AACREAD_HEADER = AVPK_HEADER, // output data contains MPEG-4 ASC
	AACREAD_SEEK = AVPK_SEEK,
	AACREAD_ERROR = AVPK_ERROR,
	AACREAD_WARN = AVPK_WARNING,
};

enum AACREAD_OPT {
	AACREAD_WHOLEFRAME = AVPKR_F_AAC_FRAMES, // return the whole frames with header, not just their body
};

struct aacread_info {
	ffuint codec;
	ffuint channels;
	ffuint sample_rate;
};

typedef struct aacread {
	ffuint state, nextstate;
	ffuint gather_size;
	const char *error;
	ffuint64 pos, seek, size, duration, off;
	ffuint frlen;
	ffstream stream;
	ffstr chunk;
	char asc[2];
	unsigned asc_ready :1;
	ffbyte first_hdr[7];

	struct aacread_info info;
	ffuint options; // enum AACREAD_OPT
} aacread;

static inline const char* aacread_error(aacread *a)
{
	return a->error;
}

static inline void aacread_open(aacread *a)
{
	ffstream_realloc(&a->stream, 4096);
}

static inline void aacread_open2(aacread *a, struct avpk_reader_conf *conf)
{
	a->size = conf->total_size;
	a->options = conf->flags;
	ffstream_realloc(&a->stream, 4096);
}

static inline void aacread_close(aacread *a)
{
	ffstream_free(&a->stream);
}

/** Search for header
Return offset
 <0 on error */
static int aacadts_find(ffstr d, struct adts_hdr *h)
{
	for (ffsize i = 0;  i != d.len;  i++) {

		if ((ffbyte)d.ptr[0] != 0xff) {
			ffssize r = ffs_findchar(&d.ptr[i], d.len - i, 0xff);
			if (r < 0)
				break;
			i += r;
		}

		if (i + 7 > d.len)
			break;
		if (adts_hdr_read(h, &d.ptr[i], 7) > 0)
			return i;
	}
	return -1;
}

/** Create MPEG-4 ASC data: "ASC  ASC_AAC" */
static int aacadts_mp4asc(char *dst, const struct adts_hdr *h)
{
	/** MPEG-4 Audio Specific Config, a bit-array */
	enum ASC {
		ASC_AOT = 5,
		ASC_FREQ_IDX = 4,
		ASC_CHAN_CONF = 4,
	};

	/** "ASC  ASC_AAC  [EXT=SBR  [EXT=PS]]" */
	enum ASC_AAC {
		AAC_FRAME_LEN = 1, //0: each packet contains 1024 samples;  1: 960 samples
		AAC_DEPENDSONCORECODER = 1,
		AAC_EXT = 1,
	};

	ffuint off = 0, v = 0;
	v |= adts_bit_write32(h->aot, &off, ASC_AOT);
	v |= adts_bit_write32(h->samp_freq_idx, &off, ASC_FREQ_IDX);
	v |= adts_bit_write32(h->chan_conf, &off, ASC_CHAN_CONF);
	off += AAC_FRAME_LEN;
	off += AAC_DEPENDSONCORECODER;
	off += AAC_EXT;

	v = ffint_be_cpu32(v);
	ffmem_copy(dst, &v, 2);
	return (off+7) / 8;
}

/** Find header */
static int _aacread_hdr_find(aacread *a, ffstr *input, ffstr *out)
{
	int r, pos;
	ffstr chunk = {};
	struct adts_hdr h;

	for (;;) {

		r = ffstream_gather(&a->stream, *input, 7, &chunk);
		ffstr_shift(input, r);
		a->off += r;
		if (chunk.len < 7)
			return 0xfeed;

		pos = aacadts_find(chunk, &h);
		if (pos >= 0)
			break;

		ffstream_consume(&a->stream, chunk.len - (7-1));
	}

	ffstream_consume(&a->stream, pos);
	ffstr_shift(&chunk, pos);
	*out = chunk;
	a->frlen = h.framelen;
	return 0;
}

/** Return enum AACREAD_R */
static inline int aacread_process(aacread *a, ffstr *input, ffstr *output)
{
	enum {
		R_HDR_FIND, R_HDR2, R_HDR, R_CRC, R_DATA, R_FR,
		R_GATHER,
		R_SEEK = 10,
	};
	int r;
	struct adts_hdr h;

	for (;;) {
		switch (a->state) {
		case R_GATHER:
			r = ffstream_gather(&a->stream, *input, a->gather_size, &a->chunk);
			ffstr_shift(input, r);
			a->off += r;
			if (a->chunk.len < a->gather_size)
				return AACREAD_MORE;
			a->chunk.len = a->gather_size;
			a->state = a->nextstate;
			continue;

		case R_HDR_FIND:
			r = _aacread_hdr_find(a, input, &a->chunk);
			if (r == 0xfeed)
				return AACREAD_MORE;

			a->state = R_GATHER,  a->nextstate = R_HDR2,  a->gather_size = a->frlen + 7;
			continue;

		case R_HDR2: {
			const void *h2 = &a->chunk.ptr[a->frlen];
			if (!(adts_hdr_read(&h, h2, 7) > 0
				&& adts_hdr_match(a->chunk.ptr, h2))) {
				ffstream_consume(&a->stream, 1);
				a->state = R_HDR_FIND;
				continue;
			}

			a->state = R_HDR;

			unsigned hdr = (a->info.sample_rate == 0);
			a->info.codec = h.aot;
			a->info.channels = h.chan_conf;
			a->info.sample_rate = h.sample_rate;

			if (hdr) {
				ffmem_copy(a->first_hdr, a->chunk.ptr, 7);
				r = aacadts_mp4asc(a->asc, &h);
				ffstr_set(output, a->asc, r);
				return AACREAD_HEADER;
			}

			continue;
		}

		case R_HDR:
			if (!(adts_hdr_read(&h, a->chunk.ptr, a->chunk.len) > 0
				&& adts_hdr_match(a->first_hdr, a->chunk.ptr))) {
				a->state = R_HDR_FIND;
				return a->error = "lost synchronization",  AACREAD_WARN;
			}

			a->frlen = h.framelen;
			a->state = R_GATHER,  a->nextstate = R_DATA,  a->gather_size = h.framelen;
			if (a->options & AACREAD_WHOLEFRAME)
				a->nextstate = R_FR;
			else if (h.have_crc)
				a->nextstate = R_CRC;
			continue;

		case R_CRC:
			ffstr_shift(&a->chunk, 2);
			// fallthrough
		case R_DATA:
			ffstr_shift(&a->chunk, 7);
			// fallthrough
		case R_FR:
			a->pos += 1024;
			ffstream_consume(&a->stream, a->gather_size);
			a->state = R_GATHER,  a->nextstate = R_HDR,  a->gather_size = 7;
			if (a->seek > a->pos - 1024)
				continue;
			*output = a->chunk;
			return AACREAD_DATA;

		case R_SEEK:
			ffstream_reset(&a->stream);
			a->state = R_HDR_FIND;
			a->off = a->pos * a->size / a->duration;
			return AACREAD_SEEK;
		}
	}
}

static inline int aacread_process2(aacread *a, ffstr *input, union avpk_read_result *res)
{
	if (a->asc_ready) {
		a->asc_ready = 0;
		ffstr_set(&res->frame, a->asc, 2);
		res->frame.pos = ~0ULL;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = ~0U;
		return AVPK_DATA;
	}

	int r = aacread_process(a, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_HEADER:
		ffmem_zero_obj(&res->frame);
		res->hdr.codec = AVPKC_AAC;
		res->hdr.sample_rate = a->info.sample_rate;
		res->hdr.channels = a->info.channels;
		res->hdr.duration = 0;
		a->asc_ready = 1;
		break;

	case AVPK_DATA:
		res->frame.pos = a->pos - 1024;
		res->frame.end_pos = ~0ULL;
		res->frame.duration = 1024;
		break;

	case AVPK_SEEK:
		res->seek_offset = a->off;
		break;

	case AVPK_ERROR:
	case AVPK_WARNING:
		res->error.message = a->error;
		res->error.offset = a->off;
		break;
	}
	return r;
}

static inline const struct aacread_info* aacread_info(aacread *a)
{
	return &a->info;
}

static inline void aacread_seek(aacread *a, ffuint64 sample)
{
	a->seek = ffint_align_floor2(sample, 1024);
	if (!a->size || !a->duration)
		return;
	a->pos = a->seek;
	a->seek = 0;
	a->state = 10; // R_SEEK
}

#define aacread_frame_samples(a)  1024
#define aacread_pos(a)  ((a)->pos - 1024)
#define aacread_offset(a)  (a)->off

AVPKR_IF_INIT(avpk_aac, "aac", AVPKF_AAC, aacread, aacread_open2, aacread_process2, aacread_seek, aacread_close);
