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
aacadts_parse
aacadts_match
aacadts_find
*/

/* .aac format:
(HDR [CRC] DATA)...
*/

#pragma once

#include <ffbase/vector.h>
#include <avpack/shared.h>


enum AACREAD_R {
	AACREAD_MORE,
	AACREAD_DATA,
	AACREAD_FRAME,
	AACREAD_HEADER, // output data contains MPEG-4 ASC
	AACREAD_ERROR,
	AACREAD_WARN,
};

enum AACREAD_OPT {
	AACREAD_WHOLEFRAME = 1, // return the whole frames with header, not just their body
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
	ffuint64 off;
	ffuint frlen;
	ffvec buf;
	ffstr chunk;
	char asc[2];
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
	ffvec_alloc(&a->buf, 4096, 1);
}

static inline void aacread_close(aacread *a)
{
	ffvec_free(&a->buf);
}

static ffuint64 _aacadts_bit_read64(ffuint64 val, ffuint *off, ffuint n)
{
	val = (val >> (64 - *off - n)) & ((1ULL << n) - 1);
	*off += n;
	return val;
}

static ffuint _aacadts_bit_write32(ffuint val, ffuint *off, ffuint n)
{
	val = (val & ((1 << n) - 1)) << (32 - *off - n);
	*off += n;
	return val;
}

struct aacadts_hdr {
	ffuint aot;
	ffuint samp_freq_idx;
	ffuint chan_conf;
	ffuint framelen;
	ffuint datalen;
	ffuint have_crc;
};

/** Parse 7 bytes of ADTS frame header */
static int aacadts_parse(struct aacadts_hdr *h, const void *d)
{
	enum {
		H_SYNC = 12, // =0x0fff
		H_MPEG_ID = 1,
		H_LAYER = 2, // =0
		H_PROTECTION_ABSENT = 1,
		H_PROFILE = 2, // AOT-1
		H_SAMPLE_FREQ_INDEX = 4, // 0..12
		H_PRIVATE_BIT = 1,
		H_CHANNEL_CONFIG = 3,
		H_ORIGINAL = 1,
		H_HOME = 1,
		H_COPYRIGHT_ID = 1,
		H_COPYRIGHT_START = 1,
		H_FRAME_LENGTH = 13,
		H_FULLNESS = 11,
		H_NUM_RAW_BLOCKS = 2, // AAC frames in ADTS frame (aac-frames = raw-blocks - 1)
	};

	ffuint off = 0;
	ffuint64 v = ffint_be_cpu64(*(ffuint64*)d);
	if (0x0fff != _aacadts_bit_read64(v, &off, H_SYNC))
		return -1;
	off += H_MPEG_ID;
	if (0 != _aacadts_bit_read64(v, &off, H_LAYER))
		return -1;
	h->have_crc = !_aacadts_bit_read64(v, &off, H_PROTECTION_ABSENT);
	h->aot = _aacadts_bit_read64(v, &off, H_PROFILE) + 1;
	if ((h->samp_freq_idx = _aacadts_bit_read64(v, &off, H_SAMPLE_FREQ_INDEX)) >= 13)
		return -1;
	off += H_PRIVATE_BIT;
	if (0 == (h->chan_conf = _aacadts_bit_read64(v, &off, H_CHANNEL_CONFIG)))
		return -1;
	off += H_ORIGINAL;
	off += H_HOME;
	off += H_COPYRIGHT_ID;
	off += H_COPYRIGHT_START;
	h->framelen = _aacadts_bit_read64(v, &off, H_FRAME_LENGTH);
	h->datalen = h->framelen - ((h->have_crc) ? 9 : 7);
	if ((int)h->datalen < 0)
		return -1;
	return 0;
}

/** Compare 2 frame headers */
static int aacadts_match(const void *a, const void *b)
{
	// SSSS SSSS SSSS MLLA PPFF FFBC CC
	// 1111 1111 1111 1110 1111 1101 1100 0000
	ffuint mask = ffint_be_cpu32(0xfffefdc0);
	return (*(ffuint*)a & mask) == (*(ffuint*)b & mask);
}

/** Search for header
Return offset
 <0 on error */
static int aacadts_find(ffstr d, struct aacadts_hdr *h)
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
		if (0 == aacadts_parse(h, &d.ptr[i]))
			return i;
	}
	return -1;
}

/** Create MPEG-4 ASC data: "ASC  ASC_AAC" */
static int aacadts_mp4asc(char *dst, const struct aacadts_hdr *h)
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
	v |= _aacadts_bit_write32(h->aot, &off, ASC_AOT);
	v |= _aacadts_bit_write32(h->samp_freq_idx, &off, ASC_FREQ_IDX);
	v |= _aacadts_bit_write32(h->chan_conf, &off, ASC_CHAN_CONF);
	off += AAC_FRAME_LEN;
	off += AAC_DEPENDSONCORECODER;
	off += AAC_EXT;

	v = ffint_be_cpu32(v);
	ffmem_copy(dst, &v, 2);
	return (off+7) / 8;
}

/** Find header */
static int _aacread_hdr_find(aacread *a, ffstr *input)
{
	int r;
	ffstr chunk = {};
	struct aacadts_hdr h;

	for (;;) {

		r = _avpack_gather_header((ffstr*)&a->buf, *input, 7, &chunk);
		int hdr_shifted = r;
		ffstr_shift(input, r);
		a->off += r;
		if (chunk.len == 0) {
			ffstr_null(&a->chunk);
			return AACREAD_MORE;
		}

		r = aacadts_find(chunk, &h);
		if (r >= 0) {
			if (chunk.ptr != a->buf.ptr) {
				ffstr_shift(input, r);
				a->off += r;
			}
			ffstr_shift(&chunk, r);
			break;
		}

		r = _avpack_gather_trailer((ffstr*)&a->buf, *input, 7, hdr_shifted);
		// r<0: ffstr_shift() isn't suitable due to assert()
		input->ptr += r,  input->len -= r;
		a->off += r;
	}

	ffstr_set(&a->chunk, chunk.ptr, 7);
	if (a->buf.len != 0) {
		ffstr_erase_left((ffstr*)&a->buf, r);
		ffstr_set(&a->chunk, a->buf.ptr, 7);
	}

	a->frlen = h.framelen;
	return 0xdeed;
}

/** Return enum AACREAD_R */
static inline int aacread_process(aacread *a, ffstr *input, ffstr *output)
{
	enum {
		R_HDR_FIND, R_HDR2, R_HDR, R_CRC, R_DATA, R_FR,
		R_GATHER_MORE, R_GATHER,
	};
	int r;
	struct aacadts_hdr h;

	for (;;) {
		switch (a->state) {

		case R_GATHER_MORE:
			if (a->buf.len == 0) {
				if (a->chunk.len != ffvec_add2T(&a->buf, &a->chunk, char))
					return a->error = "not enough memory",  AACREAD_ERROR;
			}
			a->state = R_GATHER;
			// fallthrough
		case R_GATHER:
			r = ffstr_gather((ffstr*)&a->buf, &a->buf.cap, input->ptr, input->len, a->gather_size, &a->chunk);
			if (r < 0)
				return a->error = "not enough memory",  AACREAD_ERROR;
			ffstr_shift(input, r);
			a->off += r;
			if (a->chunk.len == 0)
				return AACREAD_MORE;
			a->state = a->nextstate;
			continue;

		case R_HDR_FIND:
			r = _aacread_hdr_find(a, input);
			if (r == AACREAD_MORE)
				return AACREAD_MORE;

			a->state = R_GATHER_MORE,  a->nextstate = R_HDR2,  a->gather_size = a->frlen + 7;
			continue;

		case R_HDR2: {
			const void *h2 = &a->chunk.ptr[a->frlen];
			if (!(0 == aacadts_parse(&h, h2)
				&& aacadts_match(a->chunk.ptr, h2))) {
				if (a->buf.len != 0) {
					ffstr_erase_left((ffstr*)&a->buf, 1);
				} else if (&a->chunk.ptr[a->chunk.len] == input->ptr) {
					ffuint n = a->chunk.len - 1;
					input->ptr += n,  input->len -= n;
					a->off -= n;
				}
				a->state = R_HDR_FIND;
				continue;
			}

			a->state = R_HDR;

			if (a->info.sample_rate == 0) {
				ffmem_copy(a->first_hdr, a->chunk.ptr, 7);
				a->info.codec = h.aot;
				a->info.channels = h.chan_conf;
				static const ffushort samp_freq[] = {
					96000/5, 88200/5, 64000/5, 48000/5,
					44100/5, 32000/5, 24000/5, 22050/5,
					16000/5, 12000/5, 11025/5, 8000/5,
					7350/5,
				};
				a->info.sample_rate = samp_freq[h.samp_freq_idx] * 5;
				r = aacadts_mp4asc(a->asc, &h);
				ffstr_set(output, a->asc, r);
				return AACREAD_HEADER;
			}

			continue;
		}

		case R_HDR:
			if (!(0 == aacadts_parse(&h, a->chunk.ptr)
				&& aacadts_match(a->first_hdr, a->chunk.ptr))) {
				a->state = R_HDR_FIND;
				return a->error = "lost synchronization",  AACREAD_WARN;
			}

			a->frlen = h.framelen;
			a->state = R_GATHER_MORE,  a->nextstate = R_DATA,  a->gather_size = h.framelen;
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
		case R_FR: {
			*output = a->chunk;
			ffuint st = a->state;
			if (a->buf.len != 0)
				ffstr_erase_left((ffstr*)&a->buf, a->frlen);
			a->state = R_GATHER,  a->nextstate = R_HDR,  a->gather_size = 7;
			if (st == R_FR)
				return AACREAD_FRAME;
			return AACREAD_DATA;
		}
		}
	}
}

static inline const struct aacread_info* aacread_info(aacread *a)
{
	return &a->info;
}

#define aacread_frame_samples(a)  1024
#define aacread_offset(a)  (a)->off
