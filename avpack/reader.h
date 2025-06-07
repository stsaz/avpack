/** avpack: audio format reader
2025, Simon Zolin */

/*
avpk_reader_find
avpk_open avpk_close
avpk_read
avpk_seek
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/vorbistag.h>


/** Get reader interface by file extension */
static inline const struct avpkr_if* avpk_reader_find(const char *ext, const struct avpkr_if *const rifs[], unsigned n)
{
	for (unsigned i = 0;  i < n;  i++) {
		const char *ext2 = rifs[i]->ext + ffsz_len(rifs[i]->ext) + 1;
		if (ffsz_ieq(ext, rifs[i]->ext)
			|| (*ext2 && ffsz_ieq(ext, ext2)))
			return rifs[i];
	}
	return NULL;
}

typedef struct avpk_reader avpk_reader;
struct avpk_reader {
	void *ctx;
	struct avpkr_if ifa;
	ffstr data;
	ffuint64 total_size;
	unsigned state;
	vorbistagread vtag;
};

static inline int avpk_open(avpk_reader *a, const struct avpkr_if *rif, struct avpk_reader_conf *c)
{
	if (!rif)
		return 1;

	a->ifa = *rif;
	a->ctx = ffmem_zalloc(a->ifa.context_size);
	a->ifa.open(a->ctx, c);
	a->total_size = c->total_size;
	return 0;
}

static inline void avpk_close(avpk_reader *a)
{
	if (a->ifa.close)
		a->ifa.close(a->ctx);
	ffmem_free(a->ctx);
}

static inline int avpk_read(avpk_reader *a, ffstr *in, union avpk_read_result *res)
{
	enum {
		I_VORBISTAG = 1,
	};

	int r;
	for (;;) {

		switch (a->state) {
		case I_VORBISTAG:
			r = vorbistagread_process(&a->vtag, &a->data, &res->tag.name, &res->tag.value);
			if (r >= 0) {
				res->tag.id = r;
				return AVPK_META;
			}
			a->state = 0;
			break;
		}

		ffmem_zero_obj(res);
		r = a->ifa.process(a->ctx, in, res);
		switch (r) {
		case AVPK_HEADER:
			if (!res->hdr.real_bitrate && res->hdr.duration)
				res->hdr.real_bitrate = a->total_size * 8 * res->hdr.sample_rate / res->hdr.duration;
			break;

		case AVPK_META:
		case AVPK_DATA:
		case AVPK_SEEK:
		case AVPK_MORE:
		case AVPK_FIN:
		case AVPK_ERROR:
		case AVPK_WARNING:
			break;

		case _AVPK_META_BLOCK:
			switch (a->ifa.format) {
			case AVPKF_FLAC:
			case AVPKF_OGG:
				a->data = *(ffstr*)&res->frame;
				a->state = I_VORBISTAG;
				continue;
			}
			// fallthrough
		default:
			continue;
		}

		return r;
	}
}

static inline void avpk_seek(avpk_reader *a, ffuint64 pos)
{
	if (a->ifa.seek)
		a->ifa.seek(a->ctx, pos);
}
