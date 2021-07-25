/** avpack: utility functions
2021, Simon Zolin
*/

#pragma once

/** Gather data chunk of size from TOTAL to (TOTAL-1)*2 or return the whole input buffer */
static ffssize _avpack_gather_header(ffstr *buf, ffstr in, ffsize total, ffstr *out)
{
	if (total <= in.len && buf->len == 0) {
		ffstr_set2(out, &in);
		return total;
	}

	if (total <= buf->len) {
		ffstr_setstr(out, buf);
		return 0;
	}

	ffsize n = ffmin((total-1)*2 - buf->len, in.len);
	ffmem_copy(&buf->ptr[buf->len], in.ptr, n);
	buf->len += n;
	if (buf->len >= total)
		*out = *buf;
	else
		out->len = 0;
	return n;
}

/** Preserve trailer data chunk of size TOTAL-1 for future processing */
static ffssize _avpack_gather_trailer(ffstr *buf, ffstr in, ffsize total, ffsize hdr_shifted)
{
	if (buf->len != 0) {
		if (total-1 <= hdr_shifted) {
			buf->len = 0;
			return -(total-1);
		}
		ffstr_erase_left(buf, buf->len - (total-1));
		return 0;
	}

	ffsize n = ffmin(total-1, in.len + hdr_shifted);
	ffmem_copy(&buf->ptr[buf->len], &in.ptr[in.len - n], n);
	buf->len += n;
	return in.len;
}
