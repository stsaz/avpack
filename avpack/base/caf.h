/** avpack: .caf format
2020,2021, Simon Zolin
*/

/*
caf_hdr_read
caf_pakt_read
caf_desc_read
caf_varint
caf_mp4_esds_read
kuki_alac_read
*/

/* .caf format:
HDR ADESC [COOKIE] [TAGS] PACKET_INFO ADATA(EDIT_COUNT PACKETS...)
*/

#pragma once

#include <ffbase/stringz.h>

enum CAF_FORMAT {
	// bits = 0x??
	CAF_FMT_LE = 0x0100,
	CAF_FMT_FLOAT = 0x0200,
};

typedef struct caf_info {
	ffuint codec;
	ffuint sample_rate;
	ffuint channels;
	ffuint format; // enum CAF_FORMAT
	ffuint packet_bytes;
	ffuint packet_frames;
	ffuint64 total_packets;
	ffuint64 total_frames;
	ffuint bitrate;
	ffstr codec_conf;
} caf_info;

struct caf_hdr {
	char caff[4]; // "caff"
	ffbyte ver[2]; // =1
	ffbyte flags[2]; // =0
};

static inline int caf_hdr_read(const void *data)
{
	static const ffbyte hdrv1[] = "caff\x00\x01\x00\x00";
	if (!!ffmem_cmp(data, hdrv1, sizeof(struct caf_hdr)))
		return -1;
	return 0;
}

struct caf_chunk {
	char type[4];
	ffbyte size[8]; // may be -1 for Audio Data chunk
};

struct _caf_info {
	// "info"
	ffbyte entries[4];
	// ("key" 0x00 "value" 0x00)...
};

struct caf_pakt {
	// "pakt"
	ffbyte npackets[8];
	ffbyte nframes[8];
	ffbyte latency_frames[4];
	ffbyte remainder_frames[4];
	// ffbyte pkt_size[2]...
};

static inline int caf_pakt_read(caf_info *info, const void *data)
{
	const struct caf_pakt *p = (struct caf_pakt*)data;
	info->total_packets = ffint_be_cpu64_ptr(p->npackets);
	info->total_frames = ffint_be_cpu64_ptr(p->nframes);
	return sizeof(struct caf_pakt);
}

struct caf_desc {
	// "desc"
	ffbyte srate[8]; // float64
	char fmt[4];
	ffbyte flags[4]; // 1:float 2:little-endian
	ffbyte pkt_size[4];
	ffbyte pkt_frames[4];
	ffbyte channels[4];
	ffbyte bits[4];
};

static inline int caf_desc_read(caf_info *info, const void *data)
{
	const struct caf_desc *d = (struct caf_desc*)data;
	ffint64 i = ffint_be_cpu64_ptr(d->srate);
	info->sample_rate = *(double*)&i;
	info->channels = ffint_be_cpu32_ptr(d->channels);
	ffuint flags = ffint_be_cpu32_ptr(d->flags);
	info->format = ffint_be_cpu32_ptr(d->bits);
	if (flags & 1)
		info->format |= CAF_FMT_FLOAT;
	if (flags & 2)
		info->format |= CAF_FMT_LE;
	info->packet_bytes = ffint_be_cpu32_ptr(d->pkt_size);
	info->packet_frames = ffint_be_cpu32_ptr(d->pkt_frames);
	static const char fmts[][4] = {
		"aac ",
		"alac",
		"lpcm",
	};
	static const unsigned char fmts_int[] = {
		AVPKC_AAC,
		AVPKC_ALAC,
		AVPKC_PCM,
	};
	int r = ffcharr_findsorted(fmts, FF_COUNT(fmts), sizeof(fmts[0]), d->fmt, 4);
	if (r != -1)
		info->codec = fmts_int[r];
	return 0;
}

/** Read integer.

Format:
0xxxxxxx
1xxxxxxx 0xxxxxxx

Return N of bytes read;
 0 if done;
 -1 on error */
static inline int caf_varint(const void *data, ffsize len, ffuint *dst)
{
	const ffbyte *d = (ffbyte*)data;
	if (len == 0)
		return 0;

	ffuint d0 = d[0];
	if (d0 & 0x80) {
		if (len < 2)
			return -1;

		if (d[1] & 0x80)
			return -1;

		*dst = (d0 & 0x7f) << 7;
		*dst |= d[1];
		return 2;
	}

	*dst = d0;
	return 1;
}


struct caf_mp4_acodec {
	ffuint type; //enum CAF_MP4_ESDS_DEC_TYPE
	ffuint stm_type;
	ffuint max_brate;
	ffuint avg_brate;
	const char *conf;
	ffuint conflen;
};

/* "esds" box:
(TAG SIZE ESDS) {
	(TAG SIZE DEC_CONF) {
		(TAG SIZE DEC_SPEC)
	}
	(TAG SIZE SL)
} */

enum CAF_MP4_ESDS_TAGS {
	CAF_MP4_ESDS_TAG = 3,
	CAF_MP4_ESDS_DEC_TAG = 4,
	CAF_MP4_ESDS_DECSPEC_TAG = 5,
	CAF_MP4_ESDS_SL_TAG = 6,
};
struct caf_mp4_esds_tag {
	ffbyte tag; //enum CAF_MP4_ESDS_TAGS
	ffbyte size[4]; //"NN" | "80 80 80 NN"
};
struct caf_mp4_esds {
	ffbyte unused[3];
};
struct caf_mp4_esds_dec {
	ffbyte type; //enum CAF_MP4_ESDS_DEC_TYPE
	ffbyte stm_type;
	ffbyte unused[3];
	ffbyte max_brate[4];
	ffbyte avg_brate[4];
};
struct caf_mp4_esds_decspec {
	ffbyte data[2]; //Audio Specific Config
};
struct caf_mp4_esds_sl {
	ffbyte val;
};

/** Get next esds block.
@size: input: minimum block size;  output: actual block size
Return block tag;  0 on error. */
static int caf_mp4_esds_block(const char **pd, const char *end, ffuint *size)
{
	const struct caf_mp4_esds_tag *tag = (struct caf_mp4_esds_tag*)*pd;
	ffuint sz;

	if (*pd + 2 > end)
		return 0;

	if (tag->size[0] != 0x80) {
		*pd += 2;
		sz = tag->size[0];

	} else {
		*pd += sizeof(struct caf_mp4_esds_tag);
		if (*pd > end)
			return 0;
		sz = tag->size[3];
	}

	if (sz < *size)
		return 0;
	*size = sz;
	return tag->tag;
}

/**
Return 0 on success;
 <0 on error */
static inline int caf_mp4_esds_read(const char *data, ffuint len, struct caf_mp4_acodec *ac)
{
	const char *d = data;
	const char *end = data + len;
	int r = -1;
	ffuint size;

	size = sizeof(struct caf_mp4_esds);
	if (CAF_MP4_ESDS_TAG == caf_mp4_esds_block(&d, end, &size)) {
		d += sizeof(struct caf_mp4_esds);

		size = sizeof(struct caf_mp4_esds_dec);
		if (CAF_MP4_ESDS_DEC_TAG == caf_mp4_esds_block(&d, end, &size)) {
			const struct caf_mp4_esds_dec *dec = (struct caf_mp4_esds_dec*)d;
			d += sizeof(struct caf_mp4_esds_dec);
			ac->type = dec->type;
			ac->stm_type = dec->stm_type;
			ac->max_brate = ffint_be_cpu32_ptr(dec->max_brate);
			ac->avg_brate = ffint_be_cpu32_ptr(dec->avg_brate);

			size = sizeof(struct caf_mp4_esds_decspec);
			if (CAF_MP4_ESDS_DECSPEC_TAG == caf_mp4_esds_block(&d, end, &size)) {
				const struct caf_mp4_esds_decspec *spec = (struct caf_mp4_esds_decspec*)d;
				d += size;
				ac->conf = (char*)spec->data,  ac->conflen = size;
				r = 0;
			}
		}
	}

	return r;
}

/*
len[4] // =12
[8] "frmaalac"

len[4] // =36
[4] "alac"
unused[4]

conf[24]
*/
static inline int kuki_alac_read(ffstr in, ffstr *out)
{
	if (12+12+24 > in.len || !!ffmem_cmp(in.ptr+4, "frmaalac", 8))
		return -1;
	ffstr_set(out, in.ptr + 12+12, 24);
	return 0;
}


enum CAF_T {
	CAF_T_DESC,
	CAF_T_INFO,
	CAF_T_KUKI,
	CAF_T_PAKT,
	CAF_T_DATA,
};

enum CAF_F {
	CAF_FWHOLE = 0x0100,
	CAF_FEXACT = 0x0200,
};

struct caf_binchunk {
	char type[4];
	ffushort flags; // enum CAF_F, enum CAF_T
	ffbyte minsize;
};

#define CAF_CHUNK_TYPE(f)  ((ffbyte)(f))

static const struct caf_binchunk caf_chunks[] = {
	{ "desc", CAF_T_DESC | CAF_FWHOLE | CAF_FEXACT, sizeof(struct caf_desc) },
	{ "info", CAF_T_INFO | CAF_FWHOLE, sizeof(struct _caf_info) },
	{ "kuki", CAF_T_KUKI | CAF_FWHOLE, 0 },
	{ "pakt", CAF_T_PAKT | CAF_FWHOLE, sizeof(struct caf_pakt) },
	{ "data", CAF_T_DATA, 0 },
};
