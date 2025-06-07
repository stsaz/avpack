/** avpack: Vorbis format
2024, Simon Zolin */

#pragma once
#include <ffbase/base.h>

struct vorbis_info {
	ffbyte ver[4]; //0
	ffbyte channels;
	ffbyte rate[4];
	ffbyte br_max[4];
	ffbyte br_nominal[4];
	ffbyte br_min[4];
	ffbyte blocksize;
	ffbyte framing_bit; //1
};

static inline int vorbis_info_read(const char *d, size_t len, unsigned *channels, unsigned *rate, unsigned *br_nominal)
{
	unsigned n = 7 + sizeof(struct vorbis_info);
	const struct vorbis_info *vi = (void*)(d + 7);
	if (n > len
		|| ffmem_cmp(d, "\x01vorbis", 7)
		|| 0 != ffint_le_cpu32_ptr(vi->ver)
		|| 0 == (*channels = vi->channels)
		|| 0 == (*rate = ffint_le_cpu32_ptr(vi->rate))
		|| vi->framing_bit != 1)
		return 0;

	*br_nominal = ffint_le_cpu32_ptr(vi->br_nominal);
	return n;
}

static inline int vorbis_tags_read(const char *d, size_t len)
{
	if (len >= 7+1
		&& !ffmem_cmp(d, "\x03vorbis", 7))
		return 7;
	return 0;
}

static inline unsigned vorbis_tags_write(char *buf, size_t cap, size_t tags_len)
{
	unsigned n = 8 + tags_len;
	if (n > cap) return 0;

	ffmem_copy(buf, "\x03vorbis", 7);
	buf[7 + tags_len] = 1;
	return n;
}
