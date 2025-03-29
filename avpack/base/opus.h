/** avpack: Opus format
2024, Simon Zolin */

/*
opus_hdr_read opus_hdr_write
opus_tags_read opus_tags_write
*/

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

static inline int opus_hdr_write(void *buf, size_t cap, ffuint channels, ffuint orig_sample_rate, ffuint preskip)
{
	if (sizeof(struct opus_hdr) + 3 > cap)
		return 0;

	struct opus_hdr *h = (void*)buf;
	ffmem_copy(h->id, "OpusHead", 8);
	h->ver = 1;
	h->channels = channels;
	*(ffuint*)h->orig_sample_rate = ffint_le_cpu32(orig_sample_rate);
	*(ffushort*)h->preskip = ffint_le_cpu16(preskip);
	ffmem_zero(h + 1, 3);
	return sizeof(struct opus_hdr) + 3;
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
