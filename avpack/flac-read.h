/** avpack: .flac reader
2015,2021, Simon Zolin
*/

/*
flacread_open flacread_close
flacread_process
flacread_info
flacread_error
flacread_tag
flacread_seek
flacread_offset
flacread_cursample
flacread_samples
flacread_finish
*/

/* .flac format:
fLaC (HDR STREAMINFO) [HDR BLOCK]... (FRAME_HDR SUBFRAME... FRAME_FOOTER)...
*/

#pragma once

#include <avpack/flac-fmt.h>
#include <avpack/vorbistag.h>
#include <avpack/shared.h>
#include <ffbase/vector.h>

typedef void (*flac_log_t)(void *udata, ffstr msg);

typedef struct flacread {
	ffuint state, nextstate;
	const char *error;
	struct avp_stream stream;
	ffstr chunk;
	ffsize gather;
	ffuint64 off;
	ffuint64 total_size;
	struct flac_frame frame;
	ffbyte first_framehdr[4];
	ffuint fin;
	ffuint last_hdr_block;

	struct flac_info info;

	vorbistagread vtag;
	ffuint tag;
	ffstr tagname, tagval;

	ffuint frame1_off;
	ffuint64 seek_sample;
	ffuint64 last_seek_off;
	struct flac_seektab sktab;
	struct flac_seekpt seekpt[2];
	ffuint seek_init;

	flac_log_t log;
	void *udata;
} flacread;

enum FLACREAD_R {
	FLACREAD_MORE,
	FLACREAD_DATA,
	FLACREAD_SEEK,
	FLACREAD_DONE,
	FLACREAD_HEADER,
	FLACREAD_TAG,
	FLACREAD_HEADER_FIN,
	FLACREAD_ERROR,
};

#define _FLACR_ERR(f, e) \
	(f)->error = e,  FLACREAD_ERROR

static inline const char* flacread_error(flacread *f)
{
	return f->error;
}

/**
total_size: Optional */
static inline void flacread_open(flacread *f, ffuint64 total_size)
{
	f->total_size = total_size;
	f->seek_sample = (ffuint64)-1;
	_avp_stream_realloc(&f->stream, 64*1024);
}

static inline void flacread_close(flacread *f)
{
	_avp_stream_free(&f->stream);
	ffmem_free(f->sktab.ptr);
}

static inline void _flacr_log(flacread *f, const char *fmt, ...)
{
	if (f->log == NULL)
		return;

	ffstr s = {};
	ffsize cap = 0;
	va_list va;
	va_start(va, fmt);
	ffstr_growfmtv(&s, &cap, fmt, va);
	va_end(va);

	f->log(f->udata, s);
	ffstr_free(&s);
}

/** Find FLAC header */
static int _flacr_hdr_find(flacread *f, ffstr *input, ffuint *islastblock)
{
	int pos;
	ffstr chunk = {};

	for (;;) {
		int r = _avp_stream_gather(&f->stream, *input, FLAC_HDR_MINSIZE, &chunk);
		ffstr_shift(input, r);
		f->off += r;
		if (chunk.len < FLAC_HDR_MINSIZE) {
			return -0xfeed;
		}

		pos = ffstr_find(&chunk, FLAC_SYNC, 4);
		if (pos >= 0) {
			ffstr h = chunk;
			ffstr_shift(&h, pos);
			r = flac_info_read(h, &f->info, islastblock);
			if (r <= 0) {
				f->error = "invalid FLAC header";
				return -0xbad;
			}
			pos += r;
			break;
		}

		_avp_stream_consume(&f->stream, chunk.len - (FLAC_HDR_MINSIZE-1));
	}

	if (f->total_size != 0 && f->info.total_samples != 0)
		f->info.bitrate = f->total_size * 8 * f->info.sample_rate / f->info.total_samples;

	return pos;
}

/**
Return N of bytes read from input
 <0 on error */
static int _flacr_frame_get(flacread *f, ffstr d, struct flac_frame *frame, ffstr *output)
{
	ffssize r = flac_frame_find(d.ptr, d.len, frame, f->first_framehdr);
	if (r < 0)
		return -0xfeed;
	ffstr_shift(&d, r);

	if (frame->num != (ffuint)-1)
		frame->pos = frame->num * f->info.minblock;

	if (*(ffuint*)f->first_framehdr == 0)
		ffmem_copy(f->first_framehdr, d.ptr, 4);

	ffsize n = 1;
	for (;;) {

		struct flac_frame f2;
		ffssize r2 = flac_frame_find(d.ptr + n, d.len - n, &f2, f->first_framehdr);
		if (r2 < 0)
			return -0xfeed2;
		n += r2 + 1;

		if (f2.num != (ffuint)-1) {
			if (frame->num < f2.num)
				break;
		} else {
			if (frame->pos + frame->samples <= f2.pos)
				break;
		}
	}

	ffstr_set(output, d.ptr, n - 1);
	return r + n - 1;
}

static inline void flacread_seek(flacread *f, ffuint64 sample)
{
	f->seek_sample = sample;
}

static int _flacr_seek_prepare(flacread *f)
{
	if (!f->seek_init) {
		f->seek_init = 1;

		if (f->total_size == 0
			|| f->info.total_samples == 0)
			return _FLACR_ERR(f, "can't seek");

		if (f->sktab.len == 0) {
			if (NULL == (f->sktab.ptr = (struct flac_seekpt*)ffmem_calloc(2, sizeof(struct flac_seekpt))))
				return _FLACR_ERR(f, "not enough memory");
			f->sktab.ptr[0].sample = 0;
			f->sktab.ptr[0].off = 0;
			f->sktab.ptr[1].sample = f->info.total_samples;
			f->sktab.ptr[1].off = f->total_size - f->frame1_off;
			f->sktab.len = 2;
		} else {
			flac_seektab_finish(&f->sktab, f->total_size - f->frame1_off);
		}
	}

	int r;
	if (0 > (r = flac_seektab_find(f->sktab.ptr, f->sktab.len, f->seek_sample)))
		return _FLACR_ERR(f, "can't seek");

	f->seekpt[0] = f->sktab.ptr[r];
	f->seekpt[0].off += f->frame1_off;
	f->seekpt[1] = f->sktab.ptr[r + 1];
	f->seekpt[1].off += f->frame1_off;
	return 0;
}

/** Get file offset by audio position */
static ffuint64 _flacr_seek_offset(const struct flac_seekpt *sp, ffuint64 target, ffuint64 last_seek_off)
{
	ffuint64 samples = sp[1].sample - sp[0].sample;
	ffuint64 size = sp[1].off - sp[0].off;
	ffuint64 off = (target - sp[0].sample) * size / samples;
	off = sp[0].off + off - ffmin(4*1024, off);
	if (off == last_seek_off)
		off++;
	return off;
}

/** Adjust the search window after the current offset has become too large
Return >0: new file offset
  <0: found the target frame */
static ffint64 _flacr_seek_adjust_edge(flacread *f, struct flac_seekpt *sp, ffuint64 last_seek_off)
{
	ffuint64 off;
	sp[1].off = last_seek_off; // narrow the search window to make some progress
	if (sp[1].off - sp[0].off > 64*1024)
		off = sp[0].off + (sp[1].off - sp[0].off) / 2; // binary search
	else
		off = sp[0].off + 1; // small search window: try to find a 2nd frame

	if (off == last_seek_off)
		return -1;

	_flacr_log(f, "seek: no new frame at offset %xU, trying %xU", last_seek_off, off);
	return off;
}

/** Adjust the search window
Return <0: found the target frame */
static int _flacr_seek_adjust(flacread *f, struct flac_seekpt *sp, ffuint64 pos, ffuint64 off, ffuint64 target)
{
	_flacr_log(f, "seek: tgt:%xU cur:%xU [%xU..%xU](%xU)  off:%xU [%xU..%xU](%xU)"
		, target, pos
		, sp[0].sample, sp[1].sample, sp[1].sample - sp[0].sample
		, off
		, sp[0].off, sp[1].off, sp[1].off - sp[0].off);

	int i = (target < pos);
	sp[i].sample = pos;
	sp[i].off = off;

	if (sp[0].off + 1 >= sp[1].off)
		return -1;
	return 0;
}

/* .flac read algorithm:
. Find "flac" sync-word and info block
. Read meta blocks: seek-table, tags, picture
. Read frames:
  . Find first frame header
  . Find next frame header
  . Return the first frame data
*/
/**
Return enum FLACREAD_R */
static inline int flacread_process(flacread *f, ffstr *input, ffstr *output)
{
	enum {
		I_INFO, I_META_NEXT, I_META, I_TAGS, I_PIC, I_SEEK_TBL,
		I_FRAME, I_FRAME_CHK, I_DONE,
		I_SEEK_OFF, I_SEEK_FRAME,
		I_GATHER, I_GATHER_SOME,
	};
	const ffuint MAX_NOFRAME = 100 * 1024*1024;
	int r;

	for (;;) {
		switch (f->state) {

		case I_INFO:
			r = _flacr_hdr_find(f, input, &f->last_hdr_block);
			switch (r) {
			case -0xbad:
				return FLACREAD_ERROR;
			case -0xfeed:
				return FLACREAD_MORE;
			}

			_avp_stream_consume(&f->stream, r);
			f->state = I_META_NEXT;
			return FLACREAD_HEADER;

		case I_GATHER:
		case I_GATHER_SOME:
			if (0 != _avp_stream_realloc(&f->stream, f->gather))
				return _FLACR_ERR(f, "not enough memory");
			r = _avp_stream_gather(&f->stream, *input, f->gather, &f->chunk);
			ffstr_shift(input, r);
			f->off += r;
			if (f->chunk.len < f->gather && f->state == I_GATHER)
				return FLACREAD_MORE;
			if (f->state == I_GATHER)
				_avp_stream_consume(&f->stream, f->gather);
			f->state = f->nextstate;
			continue;

		case I_META_NEXT:
			if (f->last_hdr_block) {
				f->gather = 16*1024;
				f->state = I_FRAME;
				f->frame1_off = (ffuint)f->off;
				return FLACREAD_HEADER_FIN;
			}
			f->state = I_GATHER,  f->nextstate = I_META,  f->gather = sizeof(struct flac_hdr);
			continue;

		case I_META: {
			ffuint blksize;
			r = flac_hdr_read(f->chunk.ptr, &blksize, &f->last_hdr_block);

			const ffuint MAX_META = 16 * 1024*1024;
			ffuint next_field_off = f->off - f->chunk.len + sizeof(struct flac_hdr) + blksize;
			if (next_field_off > MAX_META)
				return _FLACR_ERR(f, "too large meta");

			f->state = I_META_NEXT;

			switch (r) {
			case FLAC_TSEEKTABLE:
				f->state = I_GATHER,  f->nextstate = I_SEEK_TBL,  f->gather = blksize;
				continue;

			case FLAC_TTAGS:
				f->state = I_GATHER,  f->nextstate = I_TAGS,  f->gather = blksize;
				continue;

			case FLAC_TPIC:
				f->state = I_GATHER,  f->nextstate = I_PIC,  f->gather = blksize;
				continue;
			}

			f->off = next_field_off;
			return FLACREAD_SEEK; // skip meta block
		}

		case I_TAGS:
			r = vorbistagread_process(&f->vtag, &f->chunk, &f->tagname, &f->tagval);
			switch (r) {
			case VORBISTAGREAD_DONE:
				f->state = I_META_NEXT;
				continue;
			case VORBISTAGREAD_ERROR:
				f->state = I_META_NEXT;
				_flacr_log(f, "bad Vorbis tags");
				continue;
			}

			f->tag = r;
			return FLACREAD_TAG;

		case I_PIC:
			if (0 != (r = flac_meta_pic(f->chunk, &f->tagval))) {
				f->state = I_META_NEXT;
				_flacr_log(f, "bad picture");
			}
			f->tag = MMTAG_PICTURE;
			ffstr_setz(&f->tagname, "picture");
			f->state = I_META_NEXT;
			return FLACREAD_TAG;

		case I_SEEK_TBL:
			f->state = I_META_NEXT;
			if (f->sktab.len != 0)
				continue; // read only the first seek table

			if (f->total_size == 0 || f->info.total_samples == 0)
				continue; // seeking not supported

			if (0 > flac_seektab_read(f->chunk, &f->sktab, f->info.total_samples)) {
				_flacr_log(f, "bad seek table");
			}
			continue;

		case I_FRAME:
			if (*(ffuint*)f->first_framehdr != 0 && f->seek_sample != (ffuint64)-1) {
				if (0 != _flacr_seek_prepare(f))
					return FLACREAD_ERROR;
				f->state = I_SEEK_OFF;
				continue;
			}

			f->state = I_GATHER_SOME,  f->nextstate = I_FRAME_CHK;
			continue;

		case I_FRAME_CHK:
			f->state = I_FRAME;
			r = _flacr_frame_get(f, f->chunk, &f->frame, output);
			if (r == -0xfeed || r == -0xfeed2) {
				if (!f->fin) {

					if (f->gather > MAX_NOFRAME)
						return _FLACR_ERR(f, "can't find frame");

					if (f->chunk.len >= f->gather)
						f->gather *= 2; // can't find 2 frames in our limited region: increase the size

					if (input->len != 0)
						continue;

					return FLACREAD_MORE;
				}
				if (r == -0xfeed || f->chunk.len == 0)
					return FLACREAD_DONE;
				*output = f->chunk; // use all we have
				f->state = I_DONE;
			} else {
				_avp_stream_consume(&f->stream, r);
			}

			if (f->seek_sample != (ffuint64)-1)
				continue; // f->first_framehdr is set, now we may seek

			_flacr_log(f, "frame #%d: pos:%U  size:%L, samples:%u"
				, f->frame.num, f->frame.pos, output->len, f->frame.samples);
			return FLACREAD_DATA;

		case I_DONE:
			return FLACREAD_DONE;


		case I_SEEK_OFF:
			f->off = _flacr_seek_offset(f->seekpt, f->seek_sample, f->last_seek_off);
			f->last_seek_off = f->off;
			_avp_stream_reset(&f->stream);
			f->state = I_GATHER_SOME,  f->nextstate = I_SEEK_FRAME;
			return FLACREAD_SEEK;

		case I_SEEK_FRAME: {
			ffuint64 frame_off;
			r = _flacr_frame_get(f, f->chunk, &f->frame, output);
			if (r == -0xfeed || r == -0xfeed2) {
				if (!f->fin) {

					if (f->gather > MAX_NOFRAME)
						return _FLACR_ERR(f, "can't find frame");

					if (f->chunk.len >= f->gather)
						f->gather *= 2; // can't find 2 frames in our limited region: increase the size

					if (input->len != 0) {
						f->state = I_GATHER_SOME,  f->nextstate = I_SEEK_FRAME;
						continue;
					}

					return FLACREAD_MORE;
				}
				f->fin = 0;
				*output = f->chunk; // use all we have
				frame_off = f->off - f->chunk.len;
			} else {
				frame_off = f->off - f->chunk.len + r - output->len;
			}

			if (r == -0xfeed || frame_off >= f->seekpt[1].off) {
				ffint64 o = _flacr_seek_adjust_edge(f, f->seekpt, f->last_seek_off);
				if (o >= 0) {
					f->off = o;
					f->last_seek_off = f->off;
					_avp_stream_reset(&f->stream);
					return FLACREAD_SEEK;
				}

			} else {
				if (!_flacr_seek_adjust(f, f->seekpt, f->frame.pos, frame_off, f->seek_sample)) {
					f->state = I_SEEK_OFF;
					continue;
				}
			}

			f->off = f->seekpt[0].off;
			f->seek_sample = (ffuint64)-1;
			_avp_stream_reset(&f->stream);
			f->state = I_FRAME;
			return FLACREAD_SEEK;
		}
		}
	}
}

#undef _FLACR_ERR

/** Get an absolute file offset to seek */
static inline ffuint64 flacread_offset(flacread *f)
{
	return f->off;
}

#define flacread_finish(f)  ((f)->fin = 1)

static inline const struct flac_info* flacread_info(flacread *f)
{
	return &f->info;
}

/**
Return enum MMTAG */
static inline int flacread_tag(flacread *f, ffstr *name, ffstr *val)
{
	*name = f->tagname;
	*val = f->tagval;
	return f->tag;
}

/** Get an absolute sample number */
#define flacread_cursample(f)  ((f)->frame.pos)

#define flacread_samples(f)  ((f)->frame.samples)
