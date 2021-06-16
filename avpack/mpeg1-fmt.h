/** avpack: MPEG-1 format
2016,2021, Simon Zolin
*/

/*
mpeg1_valid
mpeg1_bitrate
mpeg1_sample_rate
mpeg1_channels
mpeg1_samples
mpeg1_size
mpeg1_find
mpeg1_xing_read
mpeg1_xing_seek
mpeg1_xing_write
mpeg1_lame_read
mpeg1_vbri_read
*/

#pragma once

#include <ffbase/string.h>
#include <ffbase/stringz.h>
#include <ffbase/vector.h>

enum MPEG1_VER {
	MPEG1_V1 = 3,
	MPEG1_V2 = 2,
	MPEG1_V2_5 = 0,
};

enum MPEG1_LAYER {
	MPEG1_L3 = 1,
	MPEG1_L2,
	MPEG1_L1,
};

enum MPEG1_CHANNEL {
	MPEG1_STEREO,
	MPEG1_JOINT,
	MPEG1_DUAL,
	MPEG1_MONO,
};

// _MPEG1_MASK_VER  0x18
// _MPEG1_MASK_LAYER  0x06
// _MPEG1_MASK_BR  0xf0
// _MPEG1_MASK_SR  0x0c
// _MPEG1_MASK_CHAN  0xc0

struct mpeg1_hdr {
	ffbyte sync1; // =0xff

	/* sync2[3] =0x07
	ver[2]: 0,2,3: enum MPEG1_VER
	layer[2]: 1..3: enum MPEG1_LAYER
	noprotect[1]: 0: protected by CRC */
	ffbyte vl;

	/* bitrate[4]: 1..14
	sample_rate[2]: 0..2
	padding[1]: for L3 +1 byte in frame
	priv[1] */
	ffbyte bsr;

	/* channel[2]: enum MPEG1_CHANNEL
	modeext[2]: mode extension (for Joint Stereo)
	copyright[1]
	original[1]
	emphasis[2] */
	ffbyte chan;
};

/** Return TRUE if valid MPEG header */
static inline int mpeg1_valid(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	return b[0] == 0xff && (b[1] & 0xe0) == 0xe0
		&& (b[1] & 0x18) != 0x08 // ver != 1
		&& (b[1] & 0x06) != 0 // layer != 0
		&& (b[2] & 0xf0) != 0 && (b[2] & 0xf0) != 0xf0 // br != 0 or 15
		&& (b[2] & 0x0c) != 0x0c; // srate != 3
}

static inline int mpeg1_match(const void *h, const void *h2)
{
	//bits in each MPEG header that must not change across frames within the same stream
	ffuint mask = 0xfffe0c00; // 1111 1111  1111 1110  0000 1100  0000 0000
	return (ffint_be_cpu32_ptr(h) & mask) == (ffint_be_cpu32_ptr(h2) & mask);
}

/** Get bitrate (bps) */
static inline ffuint mpeg1_bitrate(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	static const ffbyte kbyterate[2][3][16] = {
		{ // MPEG-1
		{ 0,32/8,40/8,48/8, 56/8, 64/8, 80/8, 96/8,112/8,128/8,160/8,192/8,224/8,256/8,320/8,0 }, //L3
		{ 0,32/8,48/8,56/8, 64/8, 80/8, 96/8,112/8,128/8,160/8,192/8,224/8,256/8,320/8,384/8,0 }, //L2
		{ 0,32/8,64/8,96/8,128/8,160/8,192/8,224/8,256/8,288/8,320/8,352/8,384/8,416/8,448/8,0 }, //L1
		},
		{ // MPEG-2
		{ 0, 8/8,16/8,24/8,32/8,40/8,48/8, 56/8, 64/8, 80/8, 96/8,112/8,128/8,144/8,160/8,0 }, //L3
		{ 0, 8/8,16/8,24/8,32/8,40/8,48/8, 56/8, 64/8, 80/8, 96/8,112/8,128/8,144/8,160/8,0 }, //L2
		{ 0,32/8,48/8,56/8,64/8,80/8,96/8,112/8,128/8,144/8,160/8,176/8,192/8,224/8,256/8,0 }, //L1
		}
	};
	int v2 = (b[1] & 0x18) != 0x18;
	int l = (b[1] & 0x06) >> 1;
	if (l == 0)
		return 0;
	int br = (b[2] & 0xf0) >> 4;
	return (ffuint)kbyterate[v2][l-1][br] * 8 * 1000;
}

/** Get sample rate (Hz) */
static inline ffuint mpeg1_sample_rate(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	static const ffushort sample_rate[4][4] = {
		{ 44100/4, 48000/4, 32000/4, 0 }, // MPEG-2.5
		{ 0, 0, 0, 0 },
		{ 44100/2, 48000/2, 32000/2, 0 }, // MPEG-2
		{ 44100, 48000, 32000, 0 }, // MPEG-1
	};
	int v = (b[1] & 0x18) >> 3;
	int sr = (b[2] & 0x0c) >> 2;
	return sample_rate[v][sr];
}

/** Get channels */
static inline ffuint mpeg1_channels(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	return ((b[3] & 0xc0) == 0xc0) ? 1 : 2;
}

static inline ffuint mpeg1_samples(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	static const ffbyte frsamps[2][4] = {
		{ 0, 1152/8, 1152/8, 384/8 }, // MPEG-1
		{ 0, 576/8, 1152/8, 384/8 }, // MPEG-2
	};
	int v2 = (b[1] & 0x18) != 0x18;
	int l = (b[1] & 0x06) >> 1;
	return frsamps[v2][l] * 8;
}

/** Get size of MPEG frame data */
static inline ffuint mpeg1_size(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	int l = (b[1] & 0x06) >> 1;
	int pad = (b[2] & 0x02) >> 1;
	return mpeg1_samples(h)/8 * mpeg1_bitrate(h) / mpeg1_sample_rate(h)
		+ ((l != MPEG1_L1) ? pad : pad * 4);
}

/** Search for header
Return offset
 <0 on error */
static inline int mpeg1_find(ffstr data)
{
	for (ffsize i = 0;  i != data.len;  i++) {

		if ((ffbyte)data.ptr[i] != 0xff) {
			ffssize r = ffs_findchar(&data.ptr[i], data.len - i, 0xff);
			if (r < 0)
				break;
			i += r;
		}
		if (i + 4 > data.len)
			break;
		if (mpeg1_valid(&data.ptr[i]))
			return i;
	}

	return -1;
}


enum MPEG1_XING_FLAGS {
	MPEG1_XING_FRAMES = 1,
	MPEG1_XING_BYTES = 2,
	MPEG1_XING_TOC = 4,
	MPEG1_XING_VBRSCALE = 8,
};

struct mpeg1_xing {
	char id[4]; // "Xing"(VBR) or "Info"(CBR)
	ffbyte flags[4]; // enum MPEG1_XING_FLAGS
	// ffbyte frames[4];
	// ffbyte bytes[4];
	// ffbyte toc[100];
	// ffbyte vbr_scale[4]; // 100(worst)..0(best)
};

static inline int mpeg1_xing_offset(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	static const ffbyte xingoff[][2] = {
		{17, 32}, // MPEG-1: MONO, 2-CH
		{9, 17}, // MPEG-2
	};
	int v2 = (b[1] & 0x18) != 0x18;
	return 4 + xingoff[v2][mpeg1_channels(h) - 1];
}

static inline ffuint mpeg1_xing_size(ffuint flags)
{
	ffuint n = 4 + 4;
	if (flags & MPEG1_XING_FRAMES)
		n += 4;
	if (flags & MPEG1_XING_BYTES)
		n += 4;
	if (flags & MPEG1_XING_TOC)
		n += 100;
	if (flags & MPEG1_XING_VBRSCALE)
		n += 4;
	return n;
}

struct mpeg1_info {
	ffuint frames;
	ffuint bytes;
	ffuint delay;
	int vbr_scale; //-1:cbr; vbr: 100(worst)..0(best)
	ffbyte toc[100];
};

/** Parse Xing tag
Return the number of bytes read
 <0 on error */
static inline int mpeg1_xing_read(struct mpeg1_info *info, const void *data, ffsize len)
{
	const char *d = data;
	ffuint i = mpeg1_xing_offset(data);

	if (i+4+4 > len)
		return -1;

	if (!ffmem_cmp(&d[i], "Xing", 4))
		info->vbr_scale = 0;
	else if (!ffmem_cmp(&d[i], "Info", 4))
		info->vbr_scale = -1;
	else
		return -1;
	i += 4;

	ffuint flags = ffint_be_cpu32_ptr(&d[i]);
	i += 4;
	if (i + mpeg1_xing_size(flags) > len)
		return -1;

	if (flags & MPEG1_XING_FRAMES) {
		info->frames = ffint_be_cpu32_ptr(&d[i]);
		i += 4;
	}

	if (flags & MPEG1_XING_BYTES) {
		info->bytes = ffint_be_cpu32_ptr(&d[i]);
		i += 4;
	}

	if (flags & MPEG1_XING_TOC) {
		ffmem_copy(info->toc, &d[i], 100);
		i += 100;
	}

	if (flags & MPEG1_XING_VBRSCALE) {
		if (info->vbr_scale == 0)
			info->vbr_scale = ffint_be_cpu32_ptr(&d[i]);
		i += 4;
	}

	return i;
}

/** Convert sample number to stream offset (in bytes) */
static inline ffuint64 mpeg1_xing_seek(const ffbyte *toc, ffuint64 sample, ffuint64 total_samples, ffuint64 total_size)
{
	FF_ASSERT(sample < total_samples);

	double d = sample * 100.0 / total_samples;
	ffuint i = (int)d;
	d -= i;
	ffuint i1 = toc[i];
	ffuint i2 = (i != 99) ? toc[i + 1] : 256;

	return (i1 + (i2 - i1) * d) * total_size / 256.0;
}

/** Write Xing tag
Note: struct mpeg1_info.toc isn't supported
Return N of valid bytes */
static inline int mpeg1_xing_write(const struct mpeg1_info *info, void *frame)
{
	char *d = frame;
	ffuint off = mpeg1_xing_offset(frame);
	ffuint i = off;

	if (info->vbr_scale >= 0)
		ffmem_copy(&d[i], "Xing", 4);
	else
		ffmem_copy(&d[i], "Info", 4);
	i += 4;

	ffuint flags = 0;
	i += 4;

	if (info->frames != 0) {
		*(ffuint*)&d[i] = ffint_be_cpu32(info->frames);
		i += 4;
		flags |= MPEG1_XING_FRAMES;
	}

	if (info->bytes != 0) {
		*(ffuint*)&d[i] = ffint_be_cpu32(info->bytes);
		i += 4;
		flags |= MPEG1_XING_BYTES;
	}

	if (info->vbr_scale != -1) {
		*(ffuint*)&d[i] = ffint_be_cpu32(info->vbr_scale);
		i += 4;
		flags |= MPEG1_XING_VBRSCALE;
	}

	*(ffuint*)&d[off + 4] = ffint_be_cpu32(flags);
	return i;
}


struct mpeg1_lame {
	char id[9]; //e.g. "LAME3.90a"
	ffuint enc_delay;
	ffuint enc_padding;
};

struct mpeg1_lamehdr {
	char id[9];
	ffbyte unsupported1[12];
	ffbyte delay_padding[3]; // delay[12]  padding[12]
	ffbyte unsupported2[12];
};

/** Parse LAME tag
Return the number of bytes read
 <0 on error */
static inline int mpeg1_lame_read(struct mpeg1_lame *lame, const void *data, ffsize len)
{
	if (sizeof(struct mpeg1_lamehdr) > len)
		return -1;

	const struct mpeg1_lamehdr *h = data;
	ffmem_copy(lame->id, h->id, sizeof(lame->id));
	ffuint n = ffint_be_cpu32_ptr(h->delay_padding); // DD DP PP XX
	lame->enc_delay = n >> 20;
	lame->enc_padding = (n >> 8) & 0x0fff;
	return sizeof(struct mpeg1_lamehdr);
}


struct mpeg1_vbri {
	ffbyte id[4]; // "VBRI"
	ffbyte ver[2];
	ffbyte delay[2];
	ffbyte quality[2];
	ffbyte bytes[4];
	ffbyte frames[4];

	//seekpt[N] = { toc_ent_frames * N => (toc[0] + ... + toc[N-1]) * toc_scale }
	ffbyte toc_ents[2];
	ffbyte toc_scale[2];
	ffbyte toc_ent_size[2];
	ffbyte toc_ent_frames[2];
	ffbyte toc[0];
};

/** Parse VBRI tag
Return the number of bytes read
 <0 on error */
static inline int mpeg1_vbri_read(struct mpeg1_info *info, const void *data, ffsize len)
{
	const int VBRI_OFF = 4+32;
	if (VBRI_OFF + sizeof(struct mpeg1_vbri) > len)
		return -1;

	const struct mpeg1_vbri *vbri = (struct mpeg1_vbri*)((char*)data + VBRI_OFF);
	if (!!ffmem_cmp(vbri->id, "VBRI", 4)
		|| 1 != ffint_be_cpu16_ptr(vbri->ver))
		return -1;

	info->frames = ffint_be_cpu32_ptr(vbri->frames);
	info->bytes = ffint_be_cpu32_ptr(vbri->bytes);
	info->vbr_scale = 0;
	ffuint sz = VBRI_OFF + sizeof(struct mpeg1_vbri)
		+ ffint_be_cpu16_ptr(vbri->toc_ents) * ffint_be_cpu16_ptr(vbri->toc_ent_size);
	return ffmin(sz, len);
}
