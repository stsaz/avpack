/** avpack: MPEG-1 reader
2021, Simon Zolin
*/

/*
mpeg1read_open
mpeg1read_close
mpeg1read_process
mpeg1read_error
mpeg1read_seek
mpeg1read_offset
mpeg1read_info
mpeg1read_cursample
*/

#pragma once

#include <avpack/mpeg1-fmt.h>
#include <ffbase/string.h>
#include <avpack/shared.h>

struct mpeg1read_info {
	ffuint sample_rate;
	ffuint channels;
	ffuint bitrate;
	ffuint64 total_samples;
	int vbr_scale; // -1:CBR; 100(worst)..0(best)
	ffuint delay;
};

typedef struct mpeg1read {
	ffuint state, nextstate;
	const char *error;
	ffuint gather_size;
	ffvec buf;
	ffstr chunk;
	ffuint64 off, frame1_off;
	ffuint64 cur_sample, seek_sample;
	ffbyte vbr_toc[100];
	ffbyte prev_hdr[4];
	int hdr :1;
	int unrecognized_data :1;

	struct mpeg1read_info info;
	ffuint64 total_size;
} mpeg1read;

static inline void mpeg1read_open(mpeg1read *m, ffuint64 total_size)
{
	m->seek_sample = (ffuint64)-1;
	m->total_size = total_size;
	ffvec_alloc(&m->buf, 4096, 1);
}

static inline void mpeg1read_close(mpeg1read *m)
{
	ffvec_free(&m->buf);
}

enum MPEG1READ_R {
	MPEG1READ_MORE,
	MPEG1READ_HEADER,
	MPEG1READ_DATA, // data packet

	/** Need input data at absolute file offset = mpeg1read_offset()
	Expecting mpeg1read_process() with more data at the specified offset */
	MPEG1READ_SEEK,
	MPEG1READ_ERROR,
};

/** Find MPEG header */
static int _mpeg1read_hdr_find(mpeg1read *m, ffstr *input, ffstr *output)
{
	int r, pos;
	ffstr chunk = {};

	for (;;) {

		r = _avpack_gather_header(&m->buf, *input, 4, &chunk);
		ffstr_shift(input, r);
		m->off += r;
		if (chunk.len == 0) {
			ffstr_null(output);
			return 0xfeed;
		}

		pos = mpeg1_find(chunk);
		if (pos != 0)
			m->unrecognized_data = 1;
		if (pos >= 0)
			break;

		r = _avpack_gather_trailer(&m->buf, *input, 4, r);
		// r<0: ffstr_shift() isn't suitable due to assert()
		input->ptr += r,  input->len -= r;
		m->off += r;
	}

	if (chunk.ptr == input->ptr) {
		ffstr_shift(input, 4 + pos);
		m->off += 4 + pos;
	}
	ffstr_set(output, &chunk.ptr[pos], 4);
	if (m->buf.len != 0) {
		ffstr_erase_left((ffstr*)&m->buf, pos);
		ffstr_set(output, m->buf.ptr, 4);
	}

	return 0;
}

static inline const char* mpeg1read_error(mpeg1read *m)
{
	return m->error;
}

#define _MPEG1R_ERR(m, e) \
	(m)->error = (e),  MPEG1READ_ERROR

static inline void _mpeg1read_info(mpeg1read *m, ffstr data)
{
	struct mpeg1read_info *i = &m->info;
	const ffuint DEC_DELAY = 528+1;
	int r;
	const void *h = data.ptr;
	ffuint frsz = mpeg1_size(h);
	const void *next = &data.ptr[frsz];
	ffuint padding = 0;

	struct mpeg1_info xing = {};
	if ((r = mpeg1_xing_read(&xing, data.ptr, frsz)) > 0) {
		if (xing.vbr_scale >= 0 && xing.toc[98] != 0)
			ffmem_copy(m->vbr_toc, xing.toc, sizeof(xing.toc));

		struct mpeg1_lame lame = {};
		if (mpeg1_lame_read(&lame, &data.ptr[r], frsz - r) > 0) {
			xing.delay = lame.enc_delay;
			if (lame.enc_padding > DEC_DELAY)
				padding = lame.enc_padding - DEC_DELAY;
		}

	} else if (mpeg1_vbri_read(&xing, data.ptr, frsz) > 0) {

	} else {
		i->sample_rate = mpeg1_sample_rate(h);
		i->channels = mpeg1_channels(h);
		i->bitrate = mpeg1_bitrate(h);
		i->total_samples = m->total_size * mpeg1_samples(h) / frsz;
		i->vbr_scale = -1;
		i->delay = DEC_DELAY;
		return;
	}

	i->sample_rate = mpeg1_sample_rate(next);
	i->channels = mpeg1_channels(next);
	if (xing.frames != 0)
		i->total_samples = xing.frames * mpeg1_samples(next);

	i->bitrate = mpeg1_bitrate(next);
	if (xing.vbr_scale >= 0 && i->total_samples != 0)
		i->bitrate = m->total_size * 8 * i->sample_rate / i->total_samples;

	i->vbr_scale = xing.vbr_scale;
	i->delay = xing.delay + DEC_DELAY;
	i->total_samples -= ffmin(i->delay + padding, i->total_samples);
}

/**
Return enum MPEG1READ_R */
/* MPEG read alrogithm:
. find 2 consecutive matching frames
. parse header, return stream info
. read header, gather frame size
. return frame data */
static inline int mpeg1read_process(mpeg1read *m, ffstr *input, ffstr *output)
{
	enum {
		R_HDR_FIND, R_HDR2,
		R_HDR,
		R_FRAME, R_FRAME_NEXT,
		R_GATHER, R_GATHER_MORE,
	};
	int r;
	const void *h;

	for (;;) {
		switch (m->state) {

		case R_GATHER_MORE:
			if (m->buf.len == 0) {
				if (m->chunk.len != ffvec_add2T(&m->buf, &m->chunk, char))
					return _MPEG1R_ERR(m, "not enough memory");
			}
			m->state = R_GATHER;
			// fallthrough
		case R_GATHER:
			r = ffstr_gather((ffstr*)&m->buf, &m->buf.cap, input->ptr, input->len, m->gather_size, &m->chunk);
			if (r < 0)
				return _MPEG1R_ERR(m, "not enough memory");
			ffstr_shift(input, r);
			m->off += r;
			if (m->chunk.len == 0)
				return MPEG1READ_MORE;
			m->state = m->nextstate;
			continue;

		case R_HDR_FIND:
			r = _mpeg1read_hdr_find(m, input, &m->chunk);
			if (r == 0xfeed)
				return MPEG1READ_MORE;

			if (m->unrecognized_data) {
				m->unrecognized_data = 0;
				// _mpeg1read_log(m, "unrecognized data before MPEG header");
			}

			m->state = R_GATHER_MORE,  m->nextstate = R_HDR2,  m->gather_size = mpeg1_size(m->chunk.ptr) + 4;
			continue;

		case R_HDR2: {
			r = mpeg1_size(m->chunk.ptr);
			const void *h2 = &m->chunk.ptr[r];
			if (!(mpeg1_valid(h2)
				&& mpeg1_match(m->chunk.ptr, h2))) {
				if (m->buf.len != 0) {
					ffstr_erase_left((ffstr*)&m->buf, 1);
				} else if (&m->chunk.ptr[m->chunk.len] == input->ptr) {
					ffuint n = m->chunk.len - 1;
					input->ptr += n,  input->len -= n;
					m->off -= n;
				}
				m->state = R_HDR_FIND;
				continue;
			}

			m->state = R_HDR;

			if (m->info.sample_rate == 0) {
				_mpeg1read_info(m, m->chunk);
				m->frame1_off = m->off - m->chunk.len;
				ffmem_copy(m->prev_hdr, m->chunk.ptr, 4);
				return MPEG1READ_HEADER;
			}

			continue;
		}

		case R_HDR:
			if (m->seek_sample != (ffuint64)-1) {

				if (m->total_size == 0
					|| m->seek_sample >= m->info.total_samples)
					return _MPEG1R_ERR(m, "can't seek");

				if (m->vbr_toc[98] != 0)
					m->off = m->frame1_off + mpeg1_xing_seek(m->vbr_toc, m->seek_sample, m->info.total_samples, m->total_size);
				else
					m->off = m->frame1_off + m->seek_sample * m->total_size / m->info.total_samples;
				m->cur_sample = m->seek_sample;
				m->seek_sample = (ffuint64)-1;
				m->buf.len = 0;
				m->state = R_HDR_FIND;
				return MPEG1READ_SEEK;
			}

			h = m->chunk.ptr;
			if (!mpeg1_match(h, m->prev_hdr)) {
				m->state = R_HDR_FIND;
				continue;
			}

			ffmem_copy(m->prev_hdr, h, 4);
			m->state = R_GATHER_MORE,  m->nextstate = R_FRAME,  m->gather_size = mpeg1_size(h);
			continue;

		case R_FRAME:
			ffstr_setstr(output, &m->chunk);
			m->state = R_FRAME_NEXT;
			return MPEG1READ_DATA;

		case R_FRAME_NEXT:
			m->cur_sample += mpeg1_samples(m->prev_hdr);
			if (m->buf.len != 0)
				ffstr_erase_left((ffstr*)&m->buf, m->gather_size);
			m->state = R_GATHER,  m->nextstate = R_HDR,  m->gather_size = 4;
			continue;
		}
	}
	// unreachable
}

static inline const struct mpeg1read_info* mpeg1read_info(mpeg1read *m)
{
	return &m->info;
}

static inline void mpeg1read_seek(mpeg1read *m, ffuint64 sample)
{
	m->seek_sample = sample;
}

#define mpeg1read_cursample(m)  ((m)->cur_sample)

#define mpeg1read_offset(m)  ((m)->off)
