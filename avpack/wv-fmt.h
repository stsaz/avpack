/** avpack: .wv format
2021, Simon Zolin */

/*
wv_hdr_read
*/

/* .wv format:
("wvpk" INFO DATA)... [APETAG] [ID3v1]
*/

#pragma once
#include <ffbase/base.h>

struct wv_hdr {
	ffuint size;
	ffuint total_samples, samples, index;
	ffuint bits;
	ffuint flt;
	ffuint channels;
	ffuint sample_rate;
};

struct wv_hdr_fmt {
	char id[4]; // "wvpk"
	ffbyte size[4];

	ffbyte version[2]; // "XX 04"
	ffbyte unused[2];
	ffbyte total_samples[4];
	ffbyte index[4];
	ffbyte samples[4];
	ffbyte flags[4]; // unused[5] sample_rate_index[4] unused[15] float unused[4] mono bytes_per_sample[2]
	ffbyte crc[4];
};

/** Parse header.
Return N of bytes read;
	* 0: need more data;
	* <0: error */
static inline int wv_hdr_read(struct wv_hdr *hdr, const char *data, ffsize len)
{
	if (sizeof(struct wv_hdr_fmt) > len)
		return 0;

	const struct wv_hdr_fmt *h = (struct wv_hdr_fmt*)data;
	if (ffmem_cmp(h->id, "wvpk", 4))
		return -1;

	hdr->size = ffint_le_cpu32_ptr(h->size) + 8;
	if (hdr->size < sizeof(struct wv_hdr_fmt))
		return -1;

	hdr->total_samples = ffint_le_cpu32_ptr(h->total_samples);
	hdr->index = ffint_le_cpu32_ptr(h->index);
	hdr->samples = ffint_le_cpu32_ptr(h->samples);

	ffuint f = ffint_le_cpu32_ptr(h->flags);
	hdr->channels = 2 - !!(f & 0x04);

	ffuint bytes_per_sample = (f & 0x03) + 1;
	hdr->bits = (bytes_per_sample * 8) - ((f >> 13) & 0x1f);
	hdr->flt = !!(f & 0x80);

	static const ffushort sample_rates_d5[] = {
		6000/5, 8000/5, 9600/5, 11025/5, 12000/5,
		16000/5, 22050/5, 24000/5, 32000/5, 44100/5,
		48000/5, 64000/5, 88200/5, 96000/5, 192000/5,
		44100/5,
	};
	ffuint i = (f >> 23) & 0x0f;
	hdr->sample_rate = (ffuint)sample_rates_d5[i] * 5;

	return sizeof(struct wv_hdr_fmt);
}
