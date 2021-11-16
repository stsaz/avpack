/** avpack: ICY stream reader; ICY meta reader/writer
2021, Simon Zolin
*/

/*
icyread_open icyread_close
icyread_process
icymeta_read
icymeta_artist_title
icymeta_add
icymeta_fin
*/

/* ICY format:
HTTP request header:
	Icy-MetaData: 1

HTTP response header:
	icy-metaint: META_INTERVAL

body:
	(DATA*META_INTERVAL  [META])...

META:
	(NMETA)  (StreamTitle='artist - track';StreamUrl='';  [PADDING])
*/

#pragma once

#include <ffbase/vector.h>

#define ICY_HTTPHDR_REQUEST_META  "Icy-MetaData" // 1: client supports metadata
#define ICY_HTTPHDR_META_INT  "icy-metaint" // INTEGER: meta data interval

typedef struct icyread {
	ffuint meta_interval; // size of audio data between meta blocks (constant)
	ffuint ndata, nmeta; // data or meta size left to process
	ffvec meta; // full metadata gathered from partial data blocks
} icyread;

enum ICYREAD_R {
	ICYREAD_MORE,
	ICYREAD_META, // 'output' contains full meta data block (without the length byte)
	ICYREAD_DATA, // 'output' contains a chunk of audio data
	ICYREAD_ERROR,
};

/**
meta_interval: [Optional] meta interval */
static inline void icyread_open(icyread *c, ffuint meta_interval)
{
	c->meta_interval = c->ndata = meta_interval;
	if (meta_interval == 0)
		c->ndata = (ffuint)-1;
}

static inline void icyread_close(icyread *c)
{
	ffvec_free(&c->meta);
}

/** Get the next block from an ICY stream
Return enum ICYREAD_R */
static inline int icyread_process(icyread *c, ffstr *input, ffstr *output)
{
	if (input->len == 0)
		return ICYREAD_MORE;

	if (c->ndata == 0) {
		// reached meta block

		if (c->nmeta == 0) {
			// get meta size
			c->nmeta = (ffuint)(ffbyte)(input->ptr[0]) * 16;
			ffstr_shift(input, 1);
		}

		if (c->nmeta != 0) {
			// gather full meta data block
			int r = ffstr_gather((ffstr*)&c->meta, &c->meta.cap, input->ptr, input->len, c->nmeta, output);
			if (r < 0)
				return ICYREAD_ERROR;
			ffstr_shift(input, r);

			if (output->len == 0)
				return ICYREAD_MORE;

			c->nmeta = 0;
			c->meta.len = 0;
			c->ndata = c->meta_interval;
			return ICYREAD_META;
		}

		c->ndata = c->meta_interval;
	}

	ffuint n = ffmin(c->ndata, input->len);
	ffstr_set(output, input->ptr, n);
	ffstr_shift(input, n);

	if (c->meta_interval != 0)
		c->ndata -= n;

	return ICYREAD_DATA;
}


/** Parse ICY meta data.  Get next key-value pair (KEY='VAL';...).
val: value without enclosing quotes
Return 0 on sucess */
static inline int icymeta_read(ffstr *input, ffstr *key, ffstr *val)
{
	int r, i, valstart;

	r = ffstr_findchar(input, '=');
	if (r < 0)
		return -1; // expected '='

	ffstr_set(key, input->ptr, r);

	if ((ffuint)r+2 >= input->len && input->ptr[r+1] != '\'')
		return -1; // expected quote after '='
	valstart = r+2;
	i = r+2;

	for (;;) {
		r = ffs_findchar(&input->ptr[i], input->len - i, '\'');
		if (r < 0 || (ffuint)r+1 >= input->len)
			return -1; // no closing quote or no trailing ';'
		i += r+1;

		if (input->ptr[i] != ';')
			continue; // a quote inside a value

		ffstr_set(val, &input->ptr[valstart], i - 1 - valstart);
		ffstr_shift(input, i+1);
		return 0;
	}
}

/** Get artist and title from StreamTitle tag data */
static inline void icymeta_artist_title(ffstr data, ffstr *artist, ffstr *title)
{
	int r = ffstr_find(&data, " - ", 3);
	if (r >= 0) {
		ffstr_set(artist, data.ptr, r);
		ffstr_shift(&data, r+3);
	}
	*title = data;
}


/** Add name-value pair into meta data
Return N bytes written */
static inline ffsize icymeta_add(ffvec *meta, ffstr key, ffstr val)
{
	if (meta->len == 0 && 0 == ffvec_addchar(meta, '\0')) // reserve space for the length byte
		return 0;
	return ffvec_addfmt(meta, "%S='%S';"
		, &key, &val);
}

/** Finalize meta */
static inline int icymeta_fin(ffvec *meta)
{
	ffuint n = meta->len - 1;
	if (n & 0x0f) {
		ffuint pad = 16 - (n & 0x0f);
		if (NULL == ffvec_grow(meta, pad, 1))
			return -1;
		ffmem_zero(ffslice_end(meta, 1), pad);
		meta->len += pad;
		n += pad;
	}

	((char*)meta->ptr)[0] = (ffbyte)(n / 16);
	return 0;
}
