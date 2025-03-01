/** avpack: .avi format
2016,2021, Simon Zolin
*/

/*
avi_strh_read
avi_strf_read
avi_chunk_find
*/

/* .avi format:
AVI (hdrl(strl...) INFO(xxxx...) movi(xxxx(DATA...)...) idx1(xxxx()...))
*/

#pragma once

#include <ffbase/string.h>
#include <avpack/mmtag.h>

struct avi_chunk {
	char id[4];
	ffbyte size[4];
};

// AVI_STRH_TYPE
#define AVI_STRH_AUDIO  "auds"
#define AVI_STRH_VIDEO  "vids"
#define AVI_STRH_TEXT  "txts"

struct avi_strh {
	ffbyte type[4]; // AVI_STRH_TYPE
	ffbyte unused[16];
	ffbyte scale[4];
	ffbyte rate[4];
	ffbyte delay[4];
	ffbyte length[4];
};

// MOVI_TYPE
#define AVI_MOVI_AUDIO  "wb"
#define AVI_MOVI_VIDEO  "dc"

enum AVI_STRF_FMT {
	AVI_A_PCM = 0x0001,
	AVI_A_MP3 = 0x0055,
	AVI_A_AAC = 0x00FF,
};

struct avi_strf_audio {
	ffbyte format[2]; // enum AVI_STRF_FMT
	ffbyte channels[2];
	ffbyte sample_rate[4];
	ffbyte byte_rate[4];
	ffbyte block_align[2];
	ffbyte bit_depth[2];
	ffbyte exsize[0]; // [2]
};

struct avi_strf_mp3 {
	ffbyte id[2];
	ffbyte flags[4];
	ffbyte blocksize[2];
	ffbyte blockframes[2];
	ffbyte delay[2];
};

struct avi_audio_info {
	ffuint type; // 1:audio
	ffuint codec; // enum AVI_STRF_FMT
	ffuint bits;
	ffuint channels;
	ffuint sample_rate;
	ffuint bitrate;
	ffuint delay;
	ffuint blocksize;
	ffstr codec_conf;

	ffuint duration_msec;
	ffuint scale;
	ffuint rate;
};

/** Process strh chunk */
static int avi_strh_read(struct avi_audio_info *ai, const char *data)
{
	const struct avi_strh *strh = (struct avi_strh*)data;
	if (!!ffmem_cmp(strh->type, AVI_STRH_AUDIO, 4))
		return 0;
	ai->type = 1;
	ai->scale = ffint_le_cpu32_ptr(strh->scale);
	ai->rate = ffint_le_cpu32_ptr(strh->rate);
	ai->duration_msec = ffint_le_cpu32_ptr(strh->length) * 1000 * ai->scale / ai->rate;
	// delay = ffint_le_cpu32_ptr(strh->delay);
	return ai->type;
}

/** Process strf chunk */
static int avi_strf_read(struct avi_audio_info *ai, const char *data, ffsize len)
{
	const struct avi_strf_audio *f = (struct avi_strf_audio*)data;
	ai->codec = ffint_le_cpu16_ptr(f->format);
	ai->bits = ffint_le_cpu16_ptr(f->bit_depth);
	ai->channels = ffint_le_cpu16_ptr(f->channels);
	ai->sample_rate = ffint_le_cpu32_ptr(f->sample_rate);
	ai->bitrate = ffint_le_cpu32_ptr(f->byte_rate) * 8;

	if (sizeof(struct avi_strf_audio)+2 > len)
		return 0;

	ffuint exsize = ffint_le_cpu16_ptr(f->exsize);
	if (exsize > len)
		return -1;
	data += sizeof(struct avi_strf_audio) + 2;

	switch (ai->codec) {

	case AVI_A_MP3: {
		if (exsize < sizeof(struct avi_strf_mp3))
			break;

		const struct avi_strf_mp3 *mp3 = (struct avi_strf_mp3*)data;
		ai->delay = ffint_le_cpu16_ptr(mp3->delay);
		ai->blocksize = ffint_le_cpu16_ptr(mp3->blocksize);
		break;
	}

	case AVI_A_AAC:
		ffstr_set(&ai->codec_conf, data, exsize);
		break;
	}

	return 0;
}

enum {
	AVI_MASK_CHUNKID = 0x000000ff,
};

enum {
	AVI_T_UKN,
	AVI_T_ANY,
	AVI_T_AVI,
	AVI_T_STRH,
	AVI_T_STRF,
	AVI_T_INFO,
	AVI_T_MOVI,
	AVI_T_MOVI_CHUNK,

	_AVI_T_TAG,
};

enum AVI_F {
	AVI_F_WHOLE = 0x0100,
	AVI_F_LAST = 0x0200,
	AVI_F_LIST = 0x0400,
	AVI_F_PADD = 0x1000,
};

#define AVI_MINSIZE(n)  ((n) << 24)
#define AVI_GET_MINSIZE(flags)  ((flags & 0xff000000) >> 24)

struct avi_binchunk {
	char id[4];
	ffuint flags; // enum AVI_F
	const struct avi_binchunk *ctx;
};

/** Search chunk in the context.
Return -1 if not found */
static int avi_chunk_find(const struct avi_binchunk *ctx, const char *name)
{
	for (ffuint i = 0;  ;  i++) {

		if (!ffmem_cmp(name, ctx[i].id, 4))
			return i;

		if (ctx[i].flags & AVI_F_LAST) {
			if (ctx[i].id[0] == '*')
				return i;
			break;
		}
	}

	return -1;
}

/* Supported chunks:

RIFF "AVI "
 LIST hdrl
  LIST strl
   strh
   strf
 LIST INFO
  *
 LIST movi
  NNwb
*/

static const struct avi_binchunk
	avi_ctx_riff[],
	avi_ctx_avi[],
	avi_ctx_avi_list[],
	avi_ctx_hdrl[],
	avi_ctx_hdrl_list[],
	avi_ctx_strl[],
	avi_ctx_info[],
	avi_ctx_movi[];

static const struct avi_binchunk avi_ctx_global[] = {
	{ "RIFF", AVI_T_ANY | AVI_MINSIZE(4) | AVI_F_LIST | AVI_F_LAST, avi_ctx_riff },
};
static const struct avi_binchunk avi_ctx_riff[] = {
	{ "AVI ", AVI_T_AVI | AVI_F_LAST, avi_ctx_avi },
};
static const struct avi_binchunk avi_ctx_avi[] = {
	{ "LIST", AVI_T_ANY | AVI_MINSIZE(4) | AVI_F_LIST | AVI_F_LAST, avi_ctx_avi_list },
};
static const struct avi_binchunk avi_ctx_avi_list[] = {
	{ "hdrl", AVI_T_ANY, avi_ctx_hdrl },
	{ "INFO", AVI_T_INFO, avi_ctx_info },
	{ "movi", AVI_T_MOVI | AVI_F_LAST, avi_ctx_movi },
};

static const struct avi_binchunk avi_ctx_hdrl[] = {
	{ "LIST", AVI_T_ANY | AVI_MINSIZE(4) | AVI_F_LIST | AVI_F_LAST, avi_ctx_hdrl_list },
};
static const struct avi_binchunk avi_ctx_hdrl_list[] = {
	{ "strl", AVI_T_ANY | AVI_F_LAST, avi_ctx_strl },
};
static const struct avi_binchunk avi_ctx_strl[] = {
	{ "strh", AVI_T_STRH | AVI_MINSIZE(sizeof(struct avi_strh)), NULL },
	{ "strf", AVI_T_STRF | AVI_F_WHOLE | AVI_MINSIZE(sizeof(struct avi_strf_audio)) | AVI_F_LAST, NULL },
};

static const struct avi_binchunk avi_ctx_info[] = {
	{ "IART", (_AVI_T_TAG + MMTAG_ARTIST) | AVI_F_WHOLE, NULL },
	{ "ICOP", (_AVI_T_TAG + MMTAG_COPYRIGHT) | AVI_F_WHOLE, NULL },
	{ "ICRD", (_AVI_T_TAG + MMTAG_DATE) | AVI_F_WHOLE, NULL },
	{ "IGNR", (_AVI_T_TAG + MMTAG_GENRE) | AVI_F_WHOLE, NULL },
	{ "INAM", (_AVI_T_TAG + MMTAG_TITLE) | AVI_F_WHOLE, NULL },
	{ "IPRD", (_AVI_T_TAG + MMTAG_ALBUM) | AVI_F_WHOLE, NULL },
	{ "IPRT", (_AVI_T_TAG + MMTAG_TRACKNO) | AVI_F_WHOLE, NULL },
	{ "ISFT", (_AVI_T_TAG + MMTAG_VENDOR) | AVI_F_WHOLE | AVI_F_LAST, NULL },
};

static const struct avi_binchunk avi_ctx_movi[] = {
	{ "*", AVI_T_MOVI_CHUNK | AVI_F_LAST, NULL },
};
