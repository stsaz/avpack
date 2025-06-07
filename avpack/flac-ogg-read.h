/** avpack: .ogg(FLAC) reader
2019,2021, Simon Zolin
*/

/*
flacoggread_open flacoggread_close
flacoggread_process
flacoggread_error
flacoggread_info
*/

/* .ogg(FLAC) format:
OGG_PKT(HDR fLaC INFO) OGG_PKT(VORBIS_CMT) [OGG_PKT(HDR BLOCK)]... OGG_PKT(FRAME)...
*/

#include <avpack/decl.h>
#include <avpack/base/flac.h>
#include <avpack/vorbistag.h>

typedef struct flacoggread {
	ffuint state;
	const char *error;
	struct flac_info info;

	vorbistagread vtag;
	int tag;
	ffstr tagname, tagval;
} flacoggread;

#define _FLACOGGR_ERR(f, e) \
	(f)->error = e,  FLACOGGREAD_ERROR

static inline const char* flacoggread_error(flacoggread *f)
{
	return f->error;
}

static inline void flacoggread_open(flacoggread *f)
{
	(void)f;
}

static inline void flacoggread_close(flacoggread *f)
{
	(void)f;
}

enum FLACOGGREAD_R {
	FLACOGGREAD_MORE = AVPK_MORE,
	FLACOGGREAD_HEADER = AVPK_HEADER,
	FLACOGGREAD_TAG = AVPK_META,
	FLACOGGREAD_DATA = AVPK_DATA,
	FLACOGGREAD_ERROR = AVPK_ERROR,
	FLACOGGREAD_HEADER_FIN = 100,
};

struct flacogg_hdr {
	ffbyte type; // =0x7f
	char head[4]; // ="FLAC"
	ffbyte ver[2]; // =1.0
	ffbyte hdr_packets[2]; // 0:unknown
	char sync[4]; // ="fLaC"
	struct flac_hdr meta_hdr;
	struct flac_streaminfo info;
};

static int _flacoggr_hdr_read(flacoggread *f, ffstr *in)
{
	if (in->len < sizeof(struct flacogg_hdr))
		return _FLACOGGR_ERR(f, "bad header");

	const struct flacogg_hdr *h = (struct flacogg_hdr*)in->ptr;

	if (!!ffmem_cmp(in->ptr, "\x7f""FLAC""\x01\x00", 7)
		|| !!ffmem_cmp(h->sync, FLAC_SYNC, 4))
		return _FLACOGGR_ERR(f, "bad header");

	ffuint islast;
	ffstr d = FFSTR_INITN(h->sync, in->len - FF_OFF(struct flacogg_hdr, sync));
	if (flac_info_read(d, &f->info, &islast) <= 0)
		return _FLACOGGR_ERR(f, "bad info block");

	in->len = 0;
	return 0;
}

static int _flacoggr_tag(flacoggread *f, ffstr *in)
{
	int r = vorbistagread_process(&f->vtag, in, &f->tagname, &f->tagval);
	switch (r) {
	case VORBISTAGREAD_DONE:
	case VORBISTAGREAD_ERROR:
		in->len = 0;
		return 0xdeed;
	}

	f->tag = r;
	return 0;
}

/**
out: output data (FLAC frame)
Return enum FLACOGGREAD_R */
static inline int flacoggread_process(flacoggread *f, ffstr *in, ffstr *out)
{
	enum {
		R_HDR, R_META, R_VTAGS, R_DATA,
	};

	for (;;) {
		switch (f->state) {

		case R_HDR:
			if (in->len == 0)
				return FLACOGGREAD_MORE;

			if (0 != _flacoggr_hdr_read(f, in))
				return FLACOGGREAD_ERROR;

			f->state = R_META;
			return FLACOGGREAD_HEADER;

		case R_META:
			if (in->len == 0)
				return FLACOGGREAD_MORE;

			switch ((ffbyte)in->ptr[0]) {
			case 0x84:
				if (in->len < 4)
					break;
				ffstr_shift(in, 4);
				f->state = R_VTAGS;
				continue;

			case 0xff:
				f->state = R_DATA;
				return FLACOGGREAD_HEADER_FIN;
			}

			in->len = 0;
			return FLACOGGREAD_MORE;

		case R_VTAGS:
			if (0xdeed == _flacoggr_tag(f, in)) {
				f->state = R_META;
				return FLACOGGREAD_MORE;
			}
			return FLACOGGREAD_TAG;

		case R_DATA:
			if (in->len == 0)
				return FLACOGGREAD_MORE;
			*out = *in;
			in->len = 0;
			return FLACOGGREAD_DATA;
		}
	}
}

static inline const struct flac_info* flacoggread_info(flacoggread *f)
{
	return &f->info;
}

/**
Return enum MMTAG */
static inline int flacoggread_tag(flacoggread *f, ffstr *name, ffstr *val)
{
	*name = f->tagname;
	*val = f->tagval;
	return f->tag;
}

#undef _FLACOGGR_ERR
