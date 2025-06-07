/** avpack */

#pragma once
#include <ffbase/string.h>

enum AVPK_R {
	AVPK_HEADER,
	AVPK_META,
	AVPK_DATA,
	AVPK_SEEK,
	AVPK_MORE,
	AVPK_FIN,
	AVPK_WARNING,
	AVPK_ERROR,
	_AVPK_META_BLOCK,
};

enum AVPK_FORMAT {
	AVPKF_AAC = 1,
	AVPKF_APE,
	AVPKF_AVI,
	AVPKF_CAF,
	AVPKF_FLAC,
	AVPKF_MKV,
	AVPKF_MP3,
	AVPKF_MP4,
	AVPKF_MPC,
	AVPKF_OGG,
	AVPKF_TS,
	AVPKF_WAV,
	AVPKF_WV,

	AVPKF_M3U,
	AVPKF_PLS,

	AVPKF_ID3,
};

enum AVPK_CODEC {
	AVPKC_AAC = 1,
	AVPKC_AC3,
	AVPKC_ALAC,
	AVPKC_APE,
	AVPKC_FLAC,
	AVPKC_MP3,
	AVPKC_MPC,
	AVPKC_OPUS,
	AVPKC_PCM,
	AVPKC_VORBIS,
	AVPKC_WAVPACK,

	AVPKC_AVC,
	AVPKC_HEVC,

	AVPKC_ASS,
	AVPKC_UTF8,
};

struct avpk_info {
	unsigned long long duration; // samples
	unsigned sample_rate;
	unsigned char sample_bits;
	unsigned char sample_float :1;
	unsigned char channels;
	unsigned char codec; // enum AVPK_CODEC
	unsigned real_bitrate;
	unsigned delay, padding;
};

struct avpk_frame {
	// can cast to ffstr
	ffsize len;
	const char *ptr;

	ffuint64 pos; // -1:undefined
	ffuint64 end_pos; // -1:undefined
	unsigned duration; // -1:undefined
};

typedef void (*avpk_log_t)(void *opaque, const char *fmt, va_list va);

union avpk_read_result {
	struct avpk_info hdr;

	struct {
		unsigned id;
		ffstr name, value;
	} tag;

	struct avpk_frame frame;

	ffuint64 seek_offset;

	struct {
		const char *message;
		ffuint64 offset;
	} error;
};

struct avpk_reader_conf {
	ffuint64 total_size;
	unsigned code_page;
	unsigned flags; // enum AVPKR_F
	avpk_log_t log;
	void *opaque;
};

enum AVPKR_F {
	AVPKR_F_AAC_FRAMES = 1, // return the whole ADTS frames (with header)
};

struct avpkr_if {
	char ext[8]; // ext1 \0 ext2 \0
	unsigned char format; // enum AVPK_FORMAT
	unsigned short context_size;
	void (*open)(void *ctx, struct avpk_reader_conf *conf);
	int (*process)(void *ctx, ffstr *in, union avpk_read_result *res);
	void (*seek)(void *ctx, ffuint64 sample);
	void (*close)(void *ctx);
};

#define AVPKR_IF_INIT(name, ext, fmt, T, op, pr, sk, cl) \
	static const struct avpkr_if name = { \
		ext, \
		fmt, \
		sizeof(T), \
		(void (*)(void *, struct avpk_reader_conf *))op, \
		(int (*)(void *, ffstr *, union avpk_read_result *))pr, \
		(void (*)(void *, ffuint64 ))sk, \
		(void (*)(void *))cl, \
	}
