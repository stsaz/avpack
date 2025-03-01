/** avpack: ADTS format
2021, Simon Zolin */

/*
adts_hdr_read
adts_hdr_match
*/

/* .aac format:
(HDR [CRC] DATA)...
*/

#pragma once
#include <ffbase/base.h>

struct adts_hdr {
	ffuint aot;
	ffuint sample_rate, samp_freq_idx;
	ffuint chan_conf;
	ffuint framelen, datalen;
	ffuint have_crc;
};

static ffuint64 adts_bit_read64(ffuint64 val, ffuint *off, ffuint n)
{
	*off += n;
	return (val >> (64 - *off)) & ((1ULL << n) - 1);
}

static ffuint adts_bit_write32(ffuint val, ffuint *off, ffuint n)
{
	*off += n;
	return (val & ((1 << n) - 1)) << (32 - *off);
}

/** Parse 7 bytes of ADTS frame header */
static inline int adts_hdr_read(struct adts_hdr *h, const void *d, ffuint len)
{
	if (7 > len)
		return 0;

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
	if (0x0fff != adts_bit_read64(v, &off, H_SYNC))
		return -1;
	off += H_MPEG_ID;
	if (0 != adts_bit_read64(v, &off, H_LAYER))
		return -1;
	h->have_crc = !adts_bit_read64(v, &off, H_PROTECTION_ABSENT);
	h->aot = adts_bit_read64(v, &off, H_PROFILE) + 1;

	if ((h->samp_freq_idx = adts_bit_read64(v, &off, H_SAMPLE_FREQ_INDEX)) >= 13)
		return -1;

	off += H_PRIVATE_BIT;
	if (0 == (h->chan_conf = adts_bit_read64(v, &off, H_CHANNEL_CONFIG)))
		return -1;
	off += H_ORIGINAL;
	off += H_HOME;
	off += H_COPYRIGHT_ID;
	off += H_COPYRIGHT_START;
	h->framelen = adts_bit_read64(v, &off, H_FRAME_LENGTH);
	h->datalen = h->framelen - ((h->have_crc) ? 9 : 7);
	if ((int)h->datalen < 0)
		return -1;

	static const ffushort samp_freq_d5[] = {
		96000/5, 88200/5, 64000/5, 48000/5, 44100/5,
		32000/5, 24000/5, 22050/5, 16000/5, 12000/5,
		11025/5, 8000/5, 7350/5
	};
	h->sample_rate = (ffuint)samp_freq_d5[h->samp_freq_idx] * 5;
	return 7;
}

/** Compare 2 frame headers */
static inline int adts_hdr_match(const void *a, const void *b)
{
	// SSSS SSSS SSSS MLLA PPFF FFBC CC
	// 1111 1111 1111 1110 1111 1101 1100 0000
	ffuint mask = ffint_be_cpu32(0xfffefdc0);
	return (*(ffuint*)a & mask) == (*(ffuint*)b & mask);
}
