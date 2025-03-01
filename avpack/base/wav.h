/** avpack: .wav format
2015,2021, Simon Zolin
*/

/*
wav_chunk_write
wav_fmt_read
wav_fmt_write
wav_bchunk_find
*/

/* .wav format:
RIFF(fmt data(DATA...))
*/

#pragma once

#include <avpack/mmtag.h>
#include <ffbase/base.h>

struct wav_chunkhdr {
	char id[4];
	ffbyte size[4];
};

static inline int wav_chunk_write(void *dst, const char *id, ffuint size)
{
	struct wav_chunkhdr *ch = (struct wav_chunkhdr*)dst;
	ffmem_copy(ch->id, id, 4);
	*(ffuint*)ch->size = ffint_le_cpu32(size);
	return 8;
}

enum WAV_FMT {
	WAV_FMT_PCM = 1,
	WAV_FMT_IEEE_FLOAT = 3,
	WAV_FMT_EXT = 0xfffe,
};

struct wav_fmt {
	ffbyte format[2]; // enum WAV_FMT
	ffbyte channels[2];
	ffbyte sample_rate[4];
	ffbyte byte_rate[4];
	ffbyte block_align[2];
	ffbyte bits[2];
};

struct wav_fmt_ext {
	//struct wav_fmt

	ffbyte size[2]; // 22
	ffbyte valid_bits_per_sample[2];
	ffbyte channel_mask[4];// 0x03 for stereo
	ffbyte subformat[16]; // [0..1]: enum WAV_FMT
};

enum WAV_FORMAT {
	WAV_8 = 8,
	WAV_16 = 16,
	WAV_24 = 24,
	WAV_32 = 32,
	WAV_FLOAT = 0x0100 | 32,
};

struct wav_info {
	ffuint format; // enum WAV_FORMAT
	ffuint sample_rate;
	ffuint channels;
	ffuint bitrate;
	ffuint64 total_samples;
};

/** Parse "fmt " chunk */
static inline int wav_fmt_read(const void *data, ffsize len, struct wav_info *info)
{
	const struct wav_fmt *f = data;
	if (sizeof(struct wav_fmt) > len)
		return -1;

	ffuint fmt = ffint_le_cpu16_ptr(f->format);

	if (fmt == WAV_FMT_EXT) {
		const struct wav_fmt_ext *ext = (struct wav_fmt_ext*)(f + 1);
		if (sizeof(struct wav_fmt) + sizeof(struct wav_fmt_ext) > len)
			return -1;

		fmt = ffint_le_cpu16_ptr(ext->subformat);
	}

	switch (fmt) {
	case WAV_FMT_PCM: {
		ffuint bits = ffint_le_cpu16_ptr(f->bits);
		switch (bits) {
		case 8:
		case 16:
		case 24:
		case 32:
			info->format = bits;
			break;

		default:
			return -1;
		}
		break;
	}

	case WAV_FMT_IEEE_FLOAT:
		info->format = WAV_FLOAT;
		break;

	default:
		return -1;
	}

	info->channels = ffint_le_cpu16_ptr(f->channels);
	info->sample_rate = ffint_le_cpu32_ptr(f->sample_rate);
	if (info->channels == 0 || info->sample_rate == 0)
		return -1;
	info->bitrate = ffint_le_cpu32_ptr(f->byte_rate) * 8;
	return 0;
}

static inline int wav_fmt_write(void *data, const struct wav_info *info)
{
	struct wav_fmt *f = data;
	ffuint bits = info->format & 0xff;
	ffuint fmt = (info->format == WAV_FLOAT) ? WAV_FMT_IEEE_FLOAT : WAV_FMT_PCM;
	*(ffushort*)f->format = ffint_le_cpu16(fmt);
	*(ffuint*)f->sample_rate = ffint_le_cpu32(info->sample_rate);
	*(ffushort*)f->channels = ffint_le_cpu16(info->channels);
	*(ffushort*)f->bits = ffint_le_cpu16(bits);
	*(ffushort*)f->block_align = ffint_le_cpu16(bits/8 * info->channels);
	*(ffuint*)f->byte_rate = ffint_le_cpu32(info->sample_rate * bits/8 * info->channels);
	return sizeof(struct wav_fmt);
}


/* Supported chunks:

RIFF WAVE
 "fmt "
 data
 LIST INFO
  *
*/

enum {
	WAV_T_RIFF = 1,
	WAV_T_FMT,
	WAV_T_LIST,
	WAV_T_DATA,

	WAV_T_TAG = 0x80,
};

enum {
	WAV_F_WHOLE = 0x0100,
	WAV_F_LAST = 0x0200,
	WAV_F_PADD = 0x1000,
};

#define WAV_MINSIZE(n)  ((n) << 24)
#define WAV_GET_MINSIZE(flags)  ((flags & 0xff000000) >> 24)

struct wav_bchunk {
	char id[4];
	ffuint flags;
};

static inline int wav_bchunk_find(const void *data, const struct wav_bchunk *ctx)
{
	for (ffuint i = 0;  ;  i++) {
		if (!ffmem_cmp(data, ctx[i].id, 4))
			return i;
		if (ctx[i].flags & WAV_F_LAST)
			return -1;
	}
}

static const struct wav_bchunk wav_ctx_global[] = {
	{ "RIFF", WAV_T_RIFF | WAV_MINSIZE(4) | WAV_F_LAST },
};
static const struct wav_bchunk wav_ctx_riff[] = {
	{ "fmt ", WAV_T_FMT | WAV_F_WHOLE },
	{ "LIST", WAV_T_LIST | WAV_MINSIZE(4) },
	{ "data", WAV_T_DATA | WAV_F_LAST },
};
static const struct wav_bchunk wav_ctx_list[] = {
	{ "IART", WAV_T_TAG | MMTAG_ARTIST | WAV_F_WHOLE },
	{ "ICOP", WAV_T_TAG | MMTAG_COPYRIGHT | WAV_F_WHOLE },
	{ "ICRD", WAV_T_TAG | MMTAG_DATE | WAV_F_WHOLE },
	{ "IGNR", WAV_T_TAG | MMTAG_GENRE | WAV_F_WHOLE },
	{ "INAM", WAV_T_TAG | MMTAG_TITLE | WAV_F_WHOLE },
	{ "IPRD", WAV_T_TAG | MMTAG_ALBUM | WAV_F_WHOLE },
	{ "IPRT", WAV_T_TAG | MMTAG_TRACKNO | WAV_F_WHOLE },
	{ "ISFT", WAV_T_TAG | MMTAG_VENDOR | WAV_F_WHOLE | WAV_F_LAST },
};
