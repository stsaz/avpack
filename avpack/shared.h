/** avpack: utility functions
2021, Simon Zolin
*/

#pragma once

#include <ffbase/vector.h>

/** Gather data chunk of size from TOTAL to (TOTAL-1)*2 or return the whole input buffer
Return N of bytes copied
 0: data already available
 <0: error */
static inline ffssize _avpack_gather_header(ffvec *buf, ffstr in, ffsize total, ffstr *out)
{
	if (total <= in.len && buf->len == 0) {
		ffstr_setstr(out, &in);
		return 0;
	}

	if (total <= buf->len) {
		ffstr_setstr(out, buf);
		return 0;
	}

	if (NULL == ffvec_realloc(buf, (total-1)*2, 1))
		return -1;

	ffsize n = ffmin((total-1)*2 - buf->len, in.len);
	ffmem_copy(ffslice_end(buf, 1), in.ptr, n);
	buf->len += n;
	if (buf->len >= total)
		ffstr_setstr(out, buf);
	else
		out->len = 0;
	return n;
}

/** Preserve trailer data chunk of size TOTAL-1 for future processing
hdr_shifted: the return value from _avpack_gather_header()
Return N of bytes copied
 <0: N of bytes to unshift from input buffer */
static inline ffssize _avpack_gather_trailer(ffvec *buf, ffstr in, ffsize total, ffsize hdr_shifted)
{
	if (buf->len != 0) {
		if (total-1 <= hdr_shifted) {
			buf->len = 0;
			return -(total-1);
		}
		ffstr_erase_left((ffstr*)buf, buf->len - (total-1));
		return 0;
	}

	ffsize n = ffmin(total-1, in.len);
	ffmem_copy(ffslice_end(buf, 1), &in.ptr[in.len - n], n);
	buf->len += n;
	return in.len;
}


struct avp_stream {
	char *ptr;
	ffuint r, w;
	ffuint cap, mask;
};

static inline int _avp_stream_realloc(struct avp_stream *s, ffsize newcap)
{
	newcap = ffint_align_power2(newcap);
	if (newcap <= s->cap)
		return 0;

	char *p;
	if (NULL == (p = (char*)ffmem_alloc(newcap)))
		return -1;

	ffuint used = s->w - s->r;
	ffuint i = s->r & s->mask;
	ffmem_copy(p, s->ptr + i, used);
	s->r = 0;
	s->w = used;

	ffmem_free(s->ptr);
	s->ptr = p;
	s->cap = newcap;
	s->mask = newcap - 1;
	return 0;
}

static inline void _avp_stream_free(struct avp_stream *s)
{
	ffmem_free(s->ptr);
	s->ptr = NULL;
}

static inline void _avp_stream_reset(struct avp_stream *s)
{
	s->r = s->w = 0;
}

static inline ffstr _avp_stream_view(struct avp_stream *s)
{
	ffuint used = s->w - s->r;
	ffuint i = s->r & s->mask;
	ffstr view = FFSTR_INITN(s->ptr + i, used);
	return view;
}

/** Gather a contiguous region of at least 'need' bytes of data
output: buffer view, valid until the next call to this function
Return N of input bytes consumed */
static inline ffuint _avp_stream_gather(struct avp_stream *s, ffstr input, ffsize need, ffstr *output)
{
	FF_ASSERT(need <= s->cap);
	ffuint i, n = 0, used = s->w - s->r;

	if (used < need) {
		i = s->r & s->mask;
		if (i + need > s->cap) {
			// not enough space in tail: move tail to front
			ffmem_move(s->ptr, s->ptr + i, used); // "...DD" -> "DD"
			s->r -= i;
			s->w -= i;
		}

		// append input data to tail
		i = s->w & s->mask;
		ffuint unused_seq = s->cap - i; // "...DDU"
		n = ffmax(need - used, unused_seq);
		n = ffmin(n, input.len);
		ffmem_copy(s->ptr + i, input.ptr, n);
		s->w += n;

		used = s->w - s->r;
	}

	i = s->r & s->mask;
	ffstr_set(output, s->ptr + i, used);
	return n;
}

static inline void _avp_stream_consume(struct avp_stream *s, ffsize n)
{
	ffuint used = s->w - s->r;
	(void)used;
	FF_ASSERT(used >= n);
	s->r += n;
}
