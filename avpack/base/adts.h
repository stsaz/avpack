/** avpack: ADTS format
2021, Simon Zolin */

/*
adts_hdr_read adts_hdr_write
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

static inline ffuint adts_bit_write32(ffuint val, ffuint *off, ffuint n)
{
	*off += n;
	return (val & ((1 << n) - 1)) << (32 - *off);
}

static ffuint64 adts_bit_write64(ffuint64 val, ffuint *off, ffuint n)
{
	*off += n;
	return (val & ((1ULL << n) - 1)) << (64 - *off);
}

static const ffushort adts_sample_freq_d5[] = {
	96000/5, 88200/5, 64000/5, 48000/5, 44100/5,
	32000/5, 24000/5, 22050/5, 16000/5, 12000/5,
	11025/5, 8000/5, 7350/5
};

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

	h->sample_rate = (ffuint)adts_sample_freq_d5[h->samp_freq_idx] * 5;
	return 7;
}

static inline int adts_hdr_write(struct adts_hdr *h, void *d, ffsize cap, ffuint data_len)
{
	if (8 > cap)
		return 0;

	ffuint sfi = h->samp_freq_idx;
	if (h->sample_rate != (ffuint)adts_sample_freq_d5[h->samp_freq_idx] * 5) {
		for (sfi = 0;;  sfi++) {
			if (sfi == FF_COUNT(adts_sample_freq_d5))
				return 0; // sample rate not supported
			if (h->sample_rate == (ffuint)adts_sample_freq_d5[sfi] * 5) {
				h->samp_freq_idx = sfi;
				break;
			}
		}
	}

	// SSSS SSSS SSSS MLLA PPFF FFBC CCOH ISLL LLLL LLLL LLLF FFFF FFFF FFBB
	// 1111 1111 1111 0001 01?? ??00 1000 00?? ???? ???? ???1 1111 1111 1100
	// f    f    f    1    4    0    8    0    0    0    1    f    f    c
	ffuint64 v = 0xfff14080001ffc00;
	ffuint off = 18;
	v |= adts_bit_write64(sfi, &off, 4); // H_SAMPLE_FREQ_INDEX
	off++;
	v |= adts_bit_write64(h->chan_conf, &off, 3); // H_CHANNEL_CONFIG
	off += 4;
	v |= adts_bit_write64(data_len + 7, &off, 13); // H_FRAME_LENGTH
	*(ffuint64*)d = ffint_be_cpu64(v);
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
