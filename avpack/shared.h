/** avpack: utility functions
2021, Simon Zolin
*/

#pragma once

#include <ffbase/vector.h>

/** Gather data chunk of size from TOTAL to (TOTAL-1)*2 or return the whole input buffer
Return N of bytes copied
 0: data already available
 <0: error */
static ffssize _avpack_gather_header(ffvec *buf, ffstr in, ffsize total, ffstr *out)
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
static ffssize _avpack_gather_trailer(ffvec *buf, ffstr in, ffsize total, ffsize hdr_shifted)
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
