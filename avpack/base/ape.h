/** avpack: .ape format
2021, Simon Zolin */

/*
ape_hdr_read
ape_seektab_read
*/

/* .ape format:
"MAC " DESC HDR SEEK_TABLE DATA... [APETAG] [ID3v1]
*/

#pragma once
#include <ffbase/base.h>

struct ape_info {
	ffuint bits;
	ffuint channels;
	ffuint sample_rate;

	ffuint version;
	ffuint comp_level;
	ffuint seekpoints;
	ffuint block_samples;
	ffuint lastframe_blocks;
	ffuint64 total_samples;
	ffbyte md5[16];
};

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

enum APE_FLAGS {
	APE_F8BIT = 1,
	APE_FPEAK = 4,
	APE_F24BIT = 8,
	APE_FNSEEKEL = 0x10,
	APE_FNOWAVHDR = 0x20,
};

struct ape_hdr {
	ffbyte comp_level[2]; // 1000..5000
	ffbyte flags[2]; // enum APE_FLAGS

	ffbyte frame_blocks[4];
	ffbyte lastframe_blocks[4];
	ffbyte total_frames[4];

	ffbyte bps[2];
	ffbyte channels[2];
	ffbyte rate[4];
};

/**
Return header length */
static inline int ape_hdr_read(struct ape_info *ai, const char *data, ffsize len)
{
	if (!ai)
		return sizeof(struct ape_desc) + sizeof(struct ape_hdr);

	if (sizeof(struct ape_desc) > len)
		return 0;
	if (ffmem_cmp(data, "MAC ", 4)) 
		return -1;

	const struct ape_desc *ds = (struct ape_desc*)data;
	ai->version = ffint_le_cpu16_ptr(ds->ver);
	if (ai->version < 3980)
		return -1; // version not supported

	ffuint desc_size = ffint_le_cpu32_ptr(ds->desc_size);
	ffuint hdr_size = ffint_le_cpu32_ptr(ds->hdr_size);
	if (desc_size < sizeof(struct ape_desc)
		|| hdr_size < sizeof(struct ape_hdr))
		return -1;
	if ((ffuint64)desc_size + hdr_size > len)
		return 0;

	ai->seekpoints = ffint_le_cpu32_ptr(ds->seektbl_size) / 4;
	memcpy(ai->md5, ds->md5, 16);

	const struct ape_hdr *h = (struct ape_hdr*)(data + desc_size);
	ai->block_samples = ffint_le_cpu32_ptr(h->frame_blocks);
	ai->lastframe_blocks = ffint_le_cpu32_ptr(h->lastframe_blocks);
	if (ai->lastframe_blocks > ai->block_samples)
		return -1;
	ai->comp_level = ffint_le_cpu16_ptr(h->comp_level);

	ffuint total_frames = ffint_le_cpu32_ptr(h->total_frames);
	if (total_frames)
		ai->total_samples = (total_frames - 1) * ai->block_samples + ai->lastframe_blocks;

	ffuint bps = ffint_le_cpu16_ptr(h->bps);
	if (bps > 32)
		return -1;
	ai->bits = bps;
	ai->channels = ffint_le_cpu16_ptr(h->channels);
	ai->sample_rate = ffint_le_cpu32_ptr(h->rate);

	return desc_size + hdr_size;
}

/** Parse seek table (file offsets of 'ape' blocks).
Return N of seek points */
static inline int ape_seektab_read(ffuint *dst, const char *data, ffsize len, ffuint64 total_size)
{
	ffuint i, n = len / 4, off_prev = 0;
	const ffuint *offsets = (ffuint*)data;
	for (i = 0;  i < n;  i++) {
		ffuint off = ffint_le_cpu32(offsets[i]);
		if (off_prev >= off)
			break; // offsets must grow
		dst[i] = off;
		off_prev = off;
	}

	if (off_prev >= total_size)
		return -1;
	dst[i] = total_size;
	return i;
}
