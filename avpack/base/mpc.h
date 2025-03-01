/** avpack: .mpc format
2017,2021, Simon Zolin */

/*
mpc_sh_read
*/

/* .mpc format:
MPCK BLOCK(SH RG EI SO AP(FRAME...)... ST SE)...

block:
 byte id[2]
 varint size // id & size included
*/

#pragma once
#include <ffbase/base.h>

struct mpcread_info {
	ffuint sample_rate;
	ffuint channels;
	ffuint frame_samples, delay;
	ffuint64 total_samples;
};

/** Read MPC integer.
Format: (0x80 | x)... x
Return N of bytes read
  <0: need more data */
static int mpc_int_read(const void *p, ffsize len, ffuint64 *n)
{
	const ffbyte *d = p;
	ffuint i;
	ffuint64 r = 0;
	len = ffmin(len, 8);

	for (i = 0;  ;  i++) {
		if (i == len)
			return -1;

		r = (r << 7) | (d[i] & ~0x80);

		if (!(d[i] & 0x80)) {
			i++;
			break;
		}
	}

	*n = r;
	return i;
}

/* SH block:
crc[4]
ver //8
varint samples
varint delay
rate :3
max_band :5 // +1
channels :4 // +1
midside_stereo :1
block_frames_pwr :3 // block_frames = 2^(2*x)
*/
static inline int mpc_sh_read(struct mpcread_info *mi, const void *body, ffsize len)
{
	int n;
	ffuint i = 0;
	ffuint64 l;
	const ffbyte *d = (ffbyte*)body;

	if (4 + 1 > len)
		return 0;
	i += 4;

	if (d[i++] != 8)
		return -1; // unsupported version

	if (0 > (n = mpc_int_read(&d[i], len - i, &mi->total_samples)))
		return 0;
	i += n;

	if (0 > (n = mpc_int_read(&d[i], len - i, &l)))
		return 0;
	i += n;
	if (l < 0xffffffff)
		mi->delay = l;

	if (i + 2 > len)
		return 0;

	static const ffuint short sample_rates[] = { 44100, 48000, 37800, 32000 };
	mi->sample_rate = sample_rates[(d[i++] & 0xe0) >> 5];

	mi->channels = ((d[i] & 0xf0) >> 4) + 1;
	mi->frame_samples = (36 * 32) << (2 * (d[i] & 0x07));
	i++;

	return i;
}
