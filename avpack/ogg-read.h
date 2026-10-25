/** avpack: .ogg reader
2021, Simon Zolin
*/

/*
oggread_open
oggread_close
oggread_process
oggread_info
oggread_seek
oggread_offset
oggread_page_pos
oggread_error
*/

#pragma once
#include <avpack/decl.h>
#include <avpack/base/ogg.h>
#include <ffbase/stream.h>
#include <ffbase/vector.h>

#define OGGREAD_SCAN_MAX  (1*1024*1024)

typedef void (*ogg_log_t)(void *udata, const char *fmt, va_list va);

struct _oggread_seekpoint {
	ffuint64 sample;
	ffuint64 off;
};

struct oggread_info {
	ffuint64 total_samples;
	ffuint serial;
};

typedef struct oggread {
	ffuint state;
	const char *err;
	ffuint64 off
		, total_size
		, page_off;

	ffuint next_state;
	ffuint gather_size;
	ffstream stream;
	ffstr chunk;

	ffvec pkt_data; // holds partial packet data
	ffuint64 page_startpos, page_endpos, page_endpos_adjusted;
	ffuint page_num, page_counter;
	ffuint pkt_num;
	ffuint seg_off;
	ffuint body_off;

	struct oggread_info info;

	struct _oggread_seekpoint seekpt0; // first audio page
	struct _oggread_seekpoint seekpt[2];
	ffuint64 last_seek_off;
	ffuint64 seek_sample, page_last_endpos;
	ffuint page_last_serial;

	ffuint page_continued :1
		, pkt_incomplete :1 // expecting continued packet on next page
		, unrecognized_data :1
		, info_done :1
		, total_adjusted :1
		, hdr_seen :1
		, hdr_done :1
		, eof :1
		;

	ogg_log_t log;
	void *udata;
} oggread;

static inline const char* oggread_error(oggread *o)
{
	return o->err;
}

#define _OGGR_ERR(o, msg) \
	(o)->err = msg, OGGREAD_ERROR

/** Initialize .ogg reader
total_size: optional */
static inline void oggread_open(oggread *o, ffuint64 total_size)
{
	o->total_size = (total_size == 0) ? (ffuint64)-1 : total_size;
	o->seek_sample = (ffuint64)-1;
	ffstream_realloc(&o->stream, 4096);
}

static inline void oggread_open2(oggread *o, struct avpk_reader_conf *conf)
{
	oggread_open(o, !(conf->flags & AVPKR_F_NO_SEEK) ? conf->total_size : 0);
	o->log = conf->log;
	o->udata = conf->opaque;
}

static inline void _oggread_log(oggread *o, const char *fmt, ...)
{
	if (o->log == NULL)
		return;

	va_list va;
	va_start(va, fmt);
	o->log(o->udata, fmt, va);
	va_end(va);
}

static inline void oggread_close(oggread *o)
{
	ffstream_free(&o->stream);
	ffvec_free(&o->pkt_data);
}

/** Find page header.
Return full header size. */
static int _oggread_hdr_find(oggread *o, const struct ogg_hdr **h, ffstr *input)
{
	int r, pos;
	ffstr chunk;

	for (;;) {
		r = ffstream_gather(&o->stream, *input, sizeof(struct ogg_hdr), &chunk);
		ffstr_shift(input, r);
		o->off += r;
		if (chunk.len < sizeof(struct ogg_hdr)) {
			ffstr_null(&o->chunk);
			return 0;
		}

		pos = ogg_hdr_find(chunk);
		if (pos != 0)
			o->unrecognized_data = 1;
		if (pos >= 0)
			break;

		ffstream_consume(&o->stream, chunk.len - (sizeof(struct ogg_hdr)-1));
	}

	ffstream_consume(&o->stream, pos);
	ffstr_shift(&chunk, pos);
	o->page_off = o->off - ffstream_used(&o->stream);

	const struct ogg_hdr *hdr = (struct ogg_hdr*)(chunk.ptr);
	*h = hdr;
	ffstr_set(&o->chunk, hdr, sizeof(struct ogg_hdr));
	return sizeof(struct ogg_hdr) + hdr->nsegments;
}

/** Process page info */
static int _oggread_page(oggread *o, ffstr page)
{
	const struct ogg_hdr *h = (struct ogg_hdr*)page.ptr;
	o->page_continued = !!(h->flags & OGG_FCONTINUED);
	o->page_num = ffint_le_cpu32_ptr(h->number);
	o->page_counter++;
	o->page_startpos = o->page_endpos;
	ffuint64 page_endpos = ffint_le_cpu64_ptr(h->granulepos);
	if (page_endpos != (ffuint64)-1)
		o->page_endpos = page_endpos;

	o->page_endpos_adjusted = o->page_endpos;
	if (o->total_adjusted) {
		if (o->page_startpos != 0 && o->page_startpos != ~0ULL)
			o->page_startpos -= o->seekpt0.sample;
		if (o->page_endpos_adjusted != 0 && o->page_endpos_adjusted != ~0ULL)
			o->page_endpos_adjusted -= o->seekpt0.sample;
	}

	_oggread_log(o, "page #%u/%xu  end-pos:%D  packets:%u  flags:%xu  size:%u  offset:%xU"
		, o->page_num, ffint_le_cpu32_ptr(h->serial), page_endpos
		, ogg_pkt_num(h), (int)h->flags
		, page.len, o->page_off);

	ffuint crc = ogg_checksum(page.ptr, page.len);
	ffuint hcrc = ffint_le_cpu32_ptr(h->crc);
	if (crc != hcrc) {
		_oggread_log(o, "Bad page CRC:%xu, computed CRC:%xu", hcrc, crc);
	}

	return 0;
}

enum OGGREAD_R {
	OGGREAD_MORE = AVPK_MORE,
	OGGREAD_HEADER = AVPK_HEADER, // packet from page with pos=0
	OGGREAD_DATA = AVPK_DATA, // data packet

	/** Need input data at absolute file offset = oggread_offset()
	Expecting oggread_process() with more data at the specified offset */
	OGGREAD_SEEK = AVPK_SEEK,
	OGGREAD_DONE = AVPK_FIN,
	OGGREAD_ERROR = AVPK_ERROR,
};

/** Get file offset by audio position */
static ffuint64 _oggread_seek_offset(const struct _oggread_seekpoint *sp, ffuint64 target, ffuint64 last_seek_off)
{
	ffuint64 samples = sp[1].sample - sp[0].sample;
	ffuint64 size = sp[1].off - sp[0].off;
	ffuint64 off = (target - sp[0].sample) * size / samples;
	off = sp[0].off + off - ffmin(4*1024, off);
	if (off == last_seek_off)
		off++;
	return off;
}

/** Adjust the search window after the current offset has become too large */
static int _oggread_seek_adjust_edge(oggread *o, struct _oggread_seekpoint *sp)
{
	_oggread_log(o, "seek: no new page at offset %xU", o->last_seek_off);
	sp[1].off = o->last_seek_off; // narrow the search window to make some progress
	if (sp[1].off - sp[0].off > 4096) {
		o->off = sp[0].off + (sp[1].off - sp[0].off) / 2; // binary search
	} else {
		o->off = sp[0].off; // small search window: try the leftmost page
	}

	if (o->off == o->last_seek_off) {
		return -0xdeed;
	}

	o->last_seek_off = o->off;
	return OGGREAD_SEEK;
}

/** Find and process header
Return full header size */
static int _oggread_seek_hdr(oggread *o, ffstr *input)
{
	const struct ogg_hdr *h;
	int r = _oggread_hdr_find(o, &h, input);
	if (r != 0) {
		_oggread_log(o, "seek: page#%u  endpos:%U"
			, ffint_le_cpu32_ptr(h->number), ffint_le_cpu64_ptr(h->granulepos));
	}

	ffuint64 page_off = o->off - ffstream_used(&o->stream);
	if (page_off >= o->seekpt[1].off) { // there's no new page within the right end of the search window
		return _oggread_seek_adjust_edge(o, o->seekpt);
	}

	if (r == 0) {
		if (o->eof) {
			o->eof = 0;
			return _oggread_seek_adjust_edge(o, o->seekpt);
		}
		return OGGREAD_MORE;
	}

	if (ffint_le_cpu32_ptr(h->serial) != o->info.serial) {
		ffstream_consume(&o->stream, o->chunk.len);
		return -0xca11; // skip page with unknown serial
	}

	ffuint64 page_endpos = ffint_le_cpu64_ptr(h->granulepos);
	if (page_endpos == (ffuint64)-1) {
		ffstream_consume(&o->stream, o->chunk.len);
		return -0xca11; // skip page with pos=-1
	}

	if (page_endpos < o->seekpt[0].sample
		|| page_endpos > o->seekpt[1].sample)
		return _OGGR_ERR(o, "seek error");

	return r;
}

/** Adjust search boundaries */
static int _oggread_seek_adj(oggread *o, const void *page_hdr, struct _oggread_seekpoint *sp)
{
	const struct ogg_hdr *h = (struct ogg_hdr*)page_hdr;
	ffuint64 page_endpos = ffint_le_cpu64_ptr(h->granulepos);
	ffuint page_size = ogg_page_size(h);

	_oggread_log(o, "seek: tgt:%xU cur:%xU [%xU..%xU](%xU)  off:%xU [%xU..%xU](%xU)"
		, o->seek_sample, page_endpos
		, sp[0].sample, sp[1].sample, sp[1].sample - sp[0].sample
		, o->page_off
		, sp[0].off, sp[1].off, sp[1].off - sp[0].off);

	if (o->seek_sample >= page_endpos) {
		sp[0].sample = page_endpos;
		sp[0].off = o->page_off + page_size;
	} else {
		sp[1].sample = page_endpos; // start-pos of the _next_ page
		sp[1].off = o->page_off; // offset of the _current_ page
	}

	if (sp[0].off >= sp[1].off)
		return 1;
	return 0;
}

/** Get next packet */
static int _oggread_pkt(oggread *o, ffstr *output)
{
	ffstr out = {};
	int r = ogg_pkt_next(o->chunk.ptr, &o->seg_off, &o->body_off, &out);
	if (r == -1) {
		return 0xfeed;
	}

	_oggread_log(o, " packet #%u.%u  size:%u  %s"
		, o->page_num, o->pkt_num, (int)out.len
		, (r == -2) ? "incomplete" : "");

	if (o->page_continued) {
		o->page_continued = 0;
		if (!o->pkt_incomplete) {
			_oggread_log(o, "unexpected continued page; skip the packet");
			o->pkt_num++;
			return 0xca11;
		}
	} else if (o->pkt_incomplete) {
		_oggread_log(o, "expected continued page; clear previous packet data");
		o->pkt_incomplete = 0;
		o->pkt_data.len = 0;
	}

	if (r == -2 || o->pkt_incomplete) {
		if (out.len != ffvec_add2T(&o->pkt_data, &out, char))
			return _OGGR_ERR(o, "not enough memory");

		if (!o->pkt_incomplete) {
			o->pkt_incomplete = 1;
			return -1;
		}
		o->pkt_incomplete = 0;

		ffstr_set2(output, &o->pkt_data);
		o->pkt_data.len = 0;

	} else {
		*output = out;
	}

	o->pkt_num++;
	if (o->page_endpos == 0)
		return OGGREAD_HEADER;
	return OGGREAD_DATA;
}

static void _oggr_reset(oggread *o, ffuint64 start_off)
{
	ffstream_reset(&o->stream);
	o->unrecognized_data = 0;
	o->page_off = start_off;
}

/** Decode OGG stream
Return enum OGGREAD_R */
/* .ogg read algorithm:
. Determine stream length:
  . Seek to the end of file
  . Get ending position of the last page
  . Seek to the beginning
. Find OGG header, determine full header size
. Gather full header data, determine page size
. Gather full page data
. Read and return audio packets...

Seeking:
. Estimate the file offset from audio position; seek
. Find header; gather full header
  . If no header is found, adjust the right search boundary; repeat
  . If search window is empty, the next page is our target
. Get page end-position; adjust search boundaries
*/
static inline int oggread_process(oggread *o, ffstr *input, ffstr *output)
{
	enum {
		R_INIT, R_LASTHDR,
		R_SEEK, R_SEEK_HDR, R_SEEK_ADJUST, R_SEEK_DONE,
		R_HDR, R_FULLHDR, R_PAGE, R_PKT,
		R_GATHER, R_DONE,
	};
	int r;
	const struct ogg_hdr *h;

	ffstr empty = {};
	if ((o->eof = !input))
		input = &empty;

	for (;;) {
		switch (o->state) {

		case R_GATHER:
			if (0 != ffstream_realloc(&o->stream, o->gather_size))
				return _OGGR_ERR(o, "not enough memory");
			r = ffstream_gather(&o->stream, *input, o->gather_size, &o->chunk);
			ffstr_shift(input, r);
			o->off += r;
			if (o->chunk.len < o->gather_size)
				return OGGREAD_MORE;
			o->chunk.len = o->gather_size;
			o->state = o->next_state;
			continue;


		case R_INIT:
			if (o->total_size == (ffuint64)-1) {
				o->state = R_HDR;
				continue;
			}

			o->state = R_LASTHDR;
			if (o->total_size > OGG_MAXPAGE) {
				o->off = o->total_size - OGG_MAXPAGE;
				return OGGREAD_SEEK;
			}
			// fallthrough

		case R_LASTHDR: {
			r = _oggread_hdr_find(o, &h, input);
			if (r == 0) {
				if (o->off == o->total_size) {
					o->state = R_HDR;
					o->off = 0;
					_oggr_reset(o, o->off);
					return OGGREAD_SEEK;
				}
				return OGGREAD_MORE;
			}

			ffuint64 gpos = ffint_le_cpu64_ptr(h->granulepos);
			if (gpos != (ffuint64)-1) {
				o->page_last_endpos = gpos;
				o->page_last_serial = ffint_le_cpu32_ptr(h->serial);
			}
			_oggread_log(o, "page#%u  endpos:%U"
				, ffint_le_cpu32_ptr(h->number), gpos);
			ffstream_consume(&o->stream, o->chunk.len);
			continue;
		}


		case R_SEEK:
			o->off = _oggread_seek_offset(o->seekpt, o->seek_sample, o->last_seek_off);
			o->last_seek_off = o->off;
			ffstream_reset(&o->stream);
			o->state = R_SEEK_HDR;
			return OGGREAD_SEEK;

		case R_SEEK_HDR:
			r = _oggread_seek_hdr(o, input);
			switch (r) {
			case -0xca11:
				continue;
			case -0xdeed:
				o->state = R_SEEK_DONE;
				continue;
			case OGGREAD_SEEK:
				ffstream_reset(&o->stream);
				// fallthrough
			case OGGREAD_ERROR:
			case OGGREAD_MORE:
				return r;
			default:
				break;
			}

			o->gather_size = r;
			o->state = R_GATHER;  o->next_state = R_SEEK_ADJUST;
			continue;

		case R_SEEK_ADJUST:
			if (0 == _oggread_seek_adj(o, o->chunk.ptr, o->seekpt)) {
				o->state = R_SEEK;
				continue;
			}
			// fallthrough

		case R_SEEK_DONE:
			o->page_endpos = o->seekpt[0].sample;
			o->seek_sample = (ffuint64)-1;
			o->state = R_HDR;
			o->off = o->seekpt[0].off;
			o->last_seek_off = 0;
			_oggr_reset(o, o->off);
			return OGGREAD_SEEK;


		case R_HDR:
			r = _oggread_hdr_find(o, &h, input);
			if (r == 0) {
				if (o->off == o->total_size && o->hdr_done)
					return AVPK_FIN;

				if (o->off >= o->page_off + OGGREAD_SCAN_MAX) {
					o->state = R_DONE;
					o->err = "page scan limit reached";
					return (!o->hdr_done) ? AVPK_ERROR : AVPK_WARNING;
				}

				return OGGREAD_MORE;
			}

			if (o->unrecognized_data) {
				o->unrecognized_data = 0;
				_oggread_log(o, "unrecognized data before OGG page header");
			}

			o->gather_size = r;
			o->state = R_GATHER;  o->next_state = R_FULLHDR;
			o->info.serial = ffint_le_cpu32_ptr(h->serial);
			continue;

		case R_FULLHDR:
			o->gather_size = ogg_page_size(o->chunk.ptr);
			o->state = R_GATHER;  o->next_state = R_PAGE;
			continue;

		case R_PAGE:
			_oggread_page(o, o->chunk);

			if (!o->info_done && o->total_size == ~0ULL) {
				o->info_done = 1;

			} else if (!o->info_done) {
				// Seeking is available.  Get the end-pos of the first data page to determine total length.
				if (o->page_endpos == 0) {
					ffstream_consume(&o->stream, o->gather_size);
					o->state = R_HDR;
					continue; // Fast-forward to the first data page
				}

				o->info_done = 1;
				// seekpt0.sample == 0
				o->seekpt0.off = o->page_off;

				o->info.total_samples = o->page_last_endpos;
				if (o->page_endpos > 96000) {
					if (o->info.serial == o->page_last_serial) {
						// Assume our stream is just a fragment of a single larger stream
						o->info.total_samples -= o->page_endpos - 1;
						o->seekpt0.sample = o->page_endpos - 1; // start-pos of the _next_ page; _oggread_seek_adj() handles this correctly
						o->total_adjusted = 1;
					}
					// Note: for a chunk of concatenated streams there's no fast way to get real total length
				}

				o->page_counter = 0;
				o->state = R_HDR;
				o->off = 0;
				_oggr_reset(o, o->off);
				return OGGREAD_SEEK; // Return to the header page
			}

			if (!o->hdr_done && o->page_endpos != 0)
				o->hdr_done = 1;

			o->seg_off = 0;
			o->body_off = 0;
			o->pkt_num = 1;
			o->state = R_PKT;
			// fallthrough

		case R_PKT:
			if (o->hdr_done && o->seek_sample != (ffuint64)-1) {
				if (o->total_size == (ffuint64)-1
					|| o->page_last_endpos == 0)
					return _OGGR_ERR(o, "can't seek");

				o->seekpt[0].sample = o->seekpt0.sample;
				o->seekpt[0].off = o->seekpt0.off;
				o->seekpt[1].sample = o->page_last_endpos;
				o->seekpt[1].off = o->total_size;

				if (o->seek_sample <= o->seekpt0.sample) {
					o->state = R_SEEK_DONE;
					continue; // The target is at the start of the stream
				}
				o->state = R_SEEK;
				continue;
			}

			r = _oggread_pkt(o, output);
			switch (r) {
			case 0xca11:
				break;
			case 0xfeed:
				ffstream_consume(&o->stream, o->gather_size);
				o->state = R_HDR;
				break;

			case OGGREAD_DATA:
				if (!o->hdr_seen || !o->hdr_done)
					return _OGGR_ERR(o, "no OGG header found");
				return OGGREAD_DATA;

			case OGGREAD_HEADER:
				o->hdr_seen = 1;
				return OGGREAD_HEADER;

			case OGGREAD_ERROR:
				return OGGREAD_ERROR;
			}
			continue;

		case R_DONE:
			return AVPK_FIN;
		}
	}
	// unreachable
}

#undef _OGGR_ERR

static inline const struct oggread_info* oggread_info(oggread *o)
{
	return &o->info;
}

static inline void oggread_seek(oggread *o, ffuint64 sample)
{
	o->seek_sample = sample;
	if (o->total_adjusted)
		o->seek_sample += o->seekpt0.sample;
}

/** Get the starting position of the current page */
#define oggread_page_pos(o)  ((o)->page_startpos)

#define oggread_page_offset(o)  ((o)->page_off)
#define oggread_page_num(o)  ((o)->page_num)
#define oggread_pkt_num(o)  ((o)->pkt_num - 1)

#define oggread_pkt_last(o) ((o)->seg_off == ((struct ogg_hdr*)(o)->chunk.ptr)->nsegments)

/** Get an absolute file offset to seek */
#define oggread_offset(o)  ((o)->off)

static inline int oggread_process2(oggread *o, ffstr *input, union avpk_read_result *res)
{
	int r = oggread_process(o, input, (ffstr*)&res->frame);
	switch (r) {
	case AVPK_DATA:
		res->frame.pos = ~0ULL;
		if (oggread_pkt_num(o) == 1 && o->page_endpos != ~0ULL)
			res->frame.pos = o->page_startpos;

		res->frame.end_pos = ~0ULL;
		if (oggread_pkt_last(o))
			res->frame.end_pos = o->page_endpos_adjusted;

		res->frame.duration = ~0U;
		break;

	case AVPK_SEEK:
		res->seek_offset = o->off;
		break;

	case AVPK_WARNING:
	case AVPK_ERROR:
		res->error.message = o->err;
		res->error.offset = o->off;
		break;
	}
	return r;
}
