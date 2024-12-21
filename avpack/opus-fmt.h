/** avpack: Opus format
2024, Simon Zolin */

#pragma once
#include <ffbase/base.h>

struct opus_hdr {
	char id[8]; // "OpusHead"
	ffbyte ver; //1
	ffbyte channels;
	ffbyte preskip[2];
	ffbyte orig_sample_rate[4];
	// unused[3]
};

static inline int opus_hdr_read(const char *d, size_t len, uint *channels, uint *preskip)
{
	const struct opus_hdr *h = (void*)d;
	if (sizeof(struct opus_hdr) > len
		|| ffmem_cmp(d, "OpusHead", 8)
		|| h->ver != 1
		|| 0 == (*channels = h->channels))
		return 0;
	*preskip = ffint_le_cpu16_ptr(h->preskip);
	return sizeof(struct opus_hdr);
}

static inline int opus_tags_read(const char *p, size_t len)
{
	if (len >= 8
		&& !ffmem_cmp(p, "OpusTags", 8))
		return 8;
	return 0;
}

static inline int opus_tags_write(char *buf, size_t cap, size_t tags_len)
{
	uint n = 8 + tags_len;
	if (n > cap) return 0;

	ffmem_copy(buf, "OpusTags", 8);
	return n;
}
