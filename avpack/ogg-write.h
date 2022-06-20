/** avpack: .ogg writer
2015,2021, Simon Zolin
*/

/*
oggwrite_create
oggwrite_close
oggwrite_process
*/

#pragma once

#include <avpack/ogg-fmt.h>
#include <ffbase/vector.h>

typedef struct oggwrite {
	ffuint state;
	int err;
	struct ogg_page page;
	ffvec buf;
	ffuint max_page_samples;
	ffuint64 page_startpos;
	ffuint64 page_endpos;

	struct {
		ffuint64 npkts;
		ffuint64 npages;
		ffuint64 total_ogg;
		ffuint64 total_payload;
	} stat;

	ffuint continued :1;
	ffuint done :1;
} oggwrite;

/** Create OGG stream
serialno: stream serial number (random)
max_page_samples: (optional) max. number of samples per page
Return 0 on success */
static inline int oggwrite_create(oggwrite *o, ffuint serialno, ffuint max_page_samples)
{
	if (NULL == ffvec_alloc(&o->buf, OGG_MAXPAGE, 1))
		return -1;
	o->page.serial = serialno;
	o->max_page_samples = max_page_samples;
	return 0;
}

static inline void oggwrite_close(oggwrite *o)
{
	ffvec_free(&o->buf);
}

enum OGGWRITE_R {
	OGGWRITE_MORE,
	OGGWRITE_DATA,
	OGGWRITE_DONE,
};

enum OGGWRITE_F {
	OGGWRITE_FFLUSH = 1, // finalize page after this packet
	OGGWRITE_FLAST = 2, // this packet is the last one
};

/** Add packet and return OGG page when ready.
endpos: position at which the packet ends (granule pos)
flags: enum OGGWRITE_F
Return enum OGGWRITE_R */
/* OGG write algorithm:
A page (containing >=1 packets) is returned BEFORE a new packet is added when:
. Page size is about to become larger than page size limit.
  This helps to avoid partial packets.
. Page time is about to exceed page time limit.
  This helps to achieve faster seeking to a position divisible by page granularity value.

A page is returned AFTER a new packet is added when:
. User ordered to flush the page.
. The last packet is written.

The first page has BOS flag set;  the last page has EOS flag set.
The page that starts with a continued partial packet has CONTINUED flag set.

The returned page has its granule position equal to ending position of the last finished packet.
If a page contains no finished packets, its granule position is -1.
*/
static inline int oggwrite_process(oggwrite *o, ffstr *input, ffstr *output, ffuint64 endpos, ffuint flags)
{
	int r;
	ffuint f = 0, partial = 0;

	if (o->done)
		return OGGWRITE_DONE;

	if (input->len == 0) {
		if (flags & OGGWRITE_FLAST) {
			o->page_endpos = endpos;
			goto fin;
		}
		return OGGWRITE_MORE;
	}

	if (o->page.nsegments != 0) {
		r = ogg_pkt_write(&o->page, NULL, NULL, input->len);
		if (r == 0
			|| (ffuint)r != input->len)
			goto flush;

		if (o->max_page_samples != 0
			&& endpos - o->page_startpos > o->max_page_samples)
			goto flush;
	}

	r = ogg_pkt_write(&o->page, o->buf.ptr, input->ptr, input->len);
	ffstr_shift(input, r);
	if (input->len != 0) {
		partial = 1;
		goto flush;
	}

	o->page_endpos = endpos;
	o->stat.npkts++;

	if (flags & OGGWRITE_FLAST)
		goto fin;

	if (flags & OGGWRITE_FFLUSH)
		goto flush;

	return OGGWRITE_MORE;

fin:
	f |= OGG_FLAST;
	o->done = 1;

flush:
	f |= (o->page.number == 0) ? OGG_FFIRST : 0;
	f |= (o->continued) ? OGG_FCONTINUED : 0;
	o->continued = partial;
	o->stat.total_payload += o->page.size;
	r = ogg_page_write(&o->page, o->buf.ptr, o->page_endpos, f, output);
	o->stat.total_ogg += r;
	o->stat.npages++;
	o->page_startpos = o->page_endpos;  o->page_endpos = (ffuint64)-1;
	return OGGWRITE_DATA;
}
