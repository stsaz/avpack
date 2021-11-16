/** avpack: .flac format
2015,2021, Simon Zolin
*/

/*
flac_hdr flac_hdr_write
flac_meta_size
flac_info_read flac_info_write
flac_padding_write
flac_seektab_read
flac_seektab_finish
flac_seektab_find
flac_seektab_init
flac_seektab_size
flac_seektab_add
flac_seektab_write
flac_meta_pic
flac_pic_write
flac_frame_read
flac_frame_find
*/

#pragma once

#include <ffbase/vector.h>


enum FLAC_TYPE {
	FLAC_TINFO,
	FLAC_TPADDING,
	FLAC_TSEEKTABLE = 3,
	FLAC_TTAGS,
	FLAC_TPIC = 6,
};

struct flac_hdr {
	ffbyte lastblock_type; // [1]; [7]: enum FLAC_TYPE
	ffbyte size[3];
};

struct flac_streaminfo {
	ffbyte minblock[2];
	ffbyte maxblock[2];
	ffbyte minframe[3];
	ffbyte maxframe[3];
	// rate :20;
	// channels :3; //=channels-1
	// bps :5; //=bps-1
	// total_samples :36;
	ffbyte info[8];
	ffbyte md5[16];
};

#define FLAC_SYNC  "fLaC"

enum {
	FLAC_HDR_MINSIZE = 4 + sizeof(struct flac_hdr) + sizeof(struct flac_streaminfo),
};

/** Process block header
Return enum FLAC_TYPE */
static inline int flac_hdr_read(const char *data, ffuint *blocksize, ffuint *islast)
{
	const struct flac_hdr *hdr = (struct flac_hdr*)data;
	*blocksize = ffint_be_cpu24_ptr(hdr->size);
	*islast = !!(hdr->lastblock_type & 0x80);
	// log("meta block '%u' (%u)", hdr->type, *blocksize);
	return hdr->lastblock_type & 0x7f;
}

static void _flac_int_hton24(void *dst, ffuint i)
{
	ffbyte *b = (ffbyte*)dst;
	b[0] = (ffbyte)(i >> 16);
	b[1] = (ffbyte)(i >> 8);
	b[2] = (ffbyte)i;
}

static inline ffuint flac_hdr_write(void *dst, ffuint type, ffuint islast, ffuint size)
{
	struct flac_hdr *hdr = (struct flac_hdr*)dst;
	hdr->lastblock_type = ((!!islast) << 7) | type;
	_flac_int_hton24(hdr->size, size);
	return sizeof(struct flac_hdr);
}

/** Return the size required for INFO, TAGS and PADDING blocks */
static inline ffuint flac_meta_size(ffuint tags_size, ffuint padding)
{
	return FLAC_HDR_MINSIZE
		+ sizeof(struct flac_hdr) + tags_size
		+ sizeof(struct flac_hdr) + padding;
}

struct flac_info {
	ffuint bits;
	ffuint channels;
	ffuint sample_rate;
	ffuint bitrate;

	ffuint minblock, maxblock;
	ffuint minframe, maxframe;
	ffuint64 total_samples;
	char md5[16];
};

/** Process FLAC header and STREAMINFO block.
Return bytes processed
 0 if more data is needed
 -1 on error */
static inline int flac_info_read(ffstr data, struct flac_info *info, ffuint *islast)
{
	if (FLAC_HDR_MINSIZE > data.len)
		return 0;

	if (0 != ffmem_cmp(data.ptr, FLAC_SYNC, 4))
		return -1;

	ffuint len;
	if (FLAC_TINFO != flac_hdr_read(&data.ptr[4], &len, islast))
		return -1;
	if (sizeof(struct flac_streaminfo) > len)
		return -1;

	const struct flac_streaminfo *sinfo = (struct flac_streaminfo*)&data.ptr[8];
	ffuint uinfo4 = ffint_be_cpu32_ptr(sinfo->info);
	info->sample_rate = (uinfo4 & 0xfffff000) >> 12;
	info->channels = ((uinfo4 & 0x00000e00) >> 9) + 1;

	ffuint bpsample = ((uinfo4 & 0x000001f0) >> 4) + 1;
	switch (bpsample) {
	case 8: case 16: case 24:
		info->bits = bpsample;
		break;
	default:
		return -1;
	}

	info->total_samples = (((ffuint64)(uinfo4 & 0x0000000f)) << 4) | ffint_be_cpu32_ptr(sinfo->info + 4);

	info->minblock = ffint_be_cpu16_ptr(sinfo->minblock);
	info->maxblock = ffint_be_cpu16_ptr(sinfo->maxblock);
	info->minframe = ffint_be_cpu24_ptr(sinfo->minframe);
	info->maxframe = ffint_be_cpu24_ptr(sinfo->maxframe);
	ffmem_copy(info->md5, sinfo->md5, sizeof(sinfo->md5));

	return 4 + sizeof(struct flac_hdr) + len;
}

/** Add sync-word and STREAMINFO block.
Return N of bytes written
 <0 on error */
static inline int flac_info_write(void *dst, ffsize cap, const struct flac_info *info)
{
	FF_ASSERT(cap >= FLAC_HDR_MINSIZE);

	char *out = (char*)dst;
	ffmem_copy(out, FLAC_SYNC, 4);

	flac_hdr_write(&out[4], FLAC_TINFO, 0, sizeof(struct flac_streaminfo));

	struct flac_streaminfo *sinfo = (struct flac_streaminfo*)(&out[4 + sizeof(struct flac_hdr)]);
	*(ffushort*)sinfo->minblock = ffint_be_cpu16(info->minblock);
	*(ffushort*)sinfo->maxblock = ffint_be_cpu16(info->maxblock);
	_flac_int_hton24(sinfo->minframe, info->minframe);
	_flac_int_hton24(sinfo->maxframe, info->maxframe);

	sinfo->info[0] = (ffbyte)(info->sample_rate >> 12);
	sinfo->info[1] = (ffbyte)(info->sample_rate >> 4);
	sinfo->info[2] = (ffbyte)((info->sample_rate << 4) & 0xf0);

	sinfo->info[2] |= (ffbyte)(((info->channels - 1) << 1) & 0x0e);

	sinfo->info[2] |= (ffbyte)(((info->bits - 1) >> 4) & 0x01);
	sinfo->info[3] = (ffbyte)(((info->bits - 1) << 4) & 0xf0);

	if ((info->total_samples >> 32) & ~0x0000000f)
		return -1;
	// 0x544332211 -> "?5 44 33 22 11"
	sinfo->info[3] |= (ffbyte)((info->total_samples >> 32) & 0x0f);
	*(ffuint*)(&sinfo->info[4]) = ffint_be_cpu32((ffuint)info->total_samples);

	ffmem_copy(sinfo->md5, info->md5, sizeof(sinfo->md5));
	return FLAC_HDR_MINSIZE;
}

/** Add padding block of the specified size */
static inline ffuint flac_padding_write(char *out, ffuint padding, ffuint last)
{
	flac_hdr_write(out, FLAC_TPADDING, last, padding);
	ffmem_zero(out + sizeof(struct flac_hdr), padding);
	return sizeof(struct flac_hdr) + padding;
}


struct flac_seekpt {
	ffuint64 sample;
	ffuint64 off;
};

struct flac_seektab {
	ffuint len;
	struct flac_seekpt *ptr;
};

struct flac_seekpoint {
	ffbyte sample_number[8];
	ffbyte stream_offset[8];
	ffbyte frame_samples[2];
};

#define FLAC_SEEKPOINT_PLACEHOLDER  ((ffuint64)-1)

/** Parse seek table.
Output table always has an entry for sample =0 and a reserved place for the last entry =total_samples.
The last entry can't be filled here because the total size of frames may not be known yet */
static inline int flac_seektab_read(ffstr data, struct flac_seektab *sktab, ffuint64 total_samples)
{
	ffuint npts, have_0pt = 0;
	const struct flac_seekpoint *st = (struct flac_seekpoint*)data.ptr;
	ffuint64 prev_sample = 0, prev_off = 0;

	npts = data.len / sizeof(struct flac_seekpoint);

	for (ffuint i = 0;  i != npts;  i++) {
		ffuint64 samp = ffint_be_cpu64_ptr(st[i].sample_number);
		ffuint64 off = ffint_be_cpu64_ptr(st[i].stream_offset);

		if (prev_sample >= samp || prev_off >= off) {
			if (samp == FLAC_SEEKPOINT_PLACEHOLDER) {
				npts = i; // skip placeholders
				break;
			}
			if (i == 0) {
				have_0pt = 1;
				continue;
			}
			return -1; // seek points must be sorted and unique
		}
		prev_sample = samp;
		prev_off = off;
	}

	if (have_0pt) {
		st++;
		npts--;
	}

	if (npts == 0)
		return 0; // no useful seek points
	if (prev_sample >= total_samples)
		return -1; // seek point is too big

	struct flac_seekpt *sp;
	if (NULL == (sp = (struct flac_seekpt*)ffmem_calloc(npts + 2, sizeof(struct flac_seekpt))))
		return -1;
	sktab->ptr = sp;
	ffuint k = 1; // skip zero point

	for (ffuint i = 0;  i != npts;  i++) {
		sp[k].sample = ffint_be_cpu64_ptr(st[i].sample_number);
		sp[k].off = ffint_be_cpu64_ptr(st[i].stream_offset);
		// log("seekpoint: sample:%U  off:%xU"
			// , sp[k].sample, sp[k].off);
		k++;
	}

	sp[k].sample = total_samples;
	// sp[k].off

	sktab->len = npts + 2;
	return sktab->len;
}

/** Validate file offset and complete the last seek point */
static inline void flac_seektab_finish(struct flac_seektab *sktab, ffuint64 frames_size)
{
	FF_ASSERT(sktab->len >= 2);
	if (sktab->ptr[sktab->len - 2].off >= frames_size) {
		ffmem_free(sktab->ptr);
		sktab->ptr = NULL;
		sktab->len = 0;
		return;
	}

	sktab->ptr[sktab->len - 1].off = frames_size;
}

/**
Return the index of lower-bound seekpoint
 -1 on error */
static inline int flac_seektab_find(const struct flac_seekpt *pts, ffsize npts, ffuint64 sample)
{
	ffsize n = npts;
	ffuint i = -1, start = 0;

	while (start != n) {
		i = start + (n - start) / 2;
		if (sample == pts[i].sample)
			return i;
		else if (sample < pts[i].sample)
			n = i--;
		else
			start = i + 1;
	}

	if (i == (ffuint)-1 || i == npts - 1)
		return -1;

	FF_ASSERT(sample > pts[i].sample && sample < pts[i + 1].sample);
	return i;
}

/* Initialize seek table.
Example for 1 sec interval: [0 1* 2* 3* 3.1] */
static inline int flac_seektab_init(struct flac_seektab *sktab, ffuint64 total_samples, ffuint interval)
{
	ffuint npts = total_samples / interval - !(total_samples % interval);
	if ((int)npts <= 0)
		return 0;

	if (NULL == (sktab->ptr = (struct flac_seekpt*)ffmem_alloc(npts * sizeof(struct flac_seekpt))))
		return -1;
	sktab->len = npts;

	ffuint64 pos = interval;
	for (ffuint i = 0;  i != npts;  i++) {
		struct flac_seekpt *sp = &sktab->ptr[i];
		sp->sample = pos;
		pos += interval;
	}

	return npts;
}

/** Return size for the whole seektable */
static inline ffuint flac_seektab_size(ffsize npts)
{
	return sizeof(struct flac_hdr) + npts * sizeof(struct flac_seekpoint);
}

static inline ffuint flac_seektab_add(struct flac_seekpt *pts, ffsize npts, ffuint idx, ffuint64 nsamps, ffuint frlen, ffuint blksize)
{
	for (;  idx != npts;  idx++) {
		struct flac_seekpt *sp = &pts[idx];
		if (!(sp->sample >= nsamps && sp->sample < nsamps + blksize))
			break;
		sp->sample = nsamps;
		sp->off = frlen;
	}
	return idx;
}

/** Add seek table to the stream.
Move duplicate points to the right */
static inline ffuint flac_seektab_write(void *out, ffsize cap, const struct flac_seekpt *pts, ffsize npts, ffuint blksize)
{
	ffuint n = 0,  len = npts * sizeof(struct flac_seekpoint);

	FF_ASSERT(npts != 0);
	FF_ASSERT(cap >= sizeof(struct flac_hdr) + len);

	flac_hdr_write(out, FLAC_TSEEKTABLE, 1, len);

	struct flac_seekpoint *skpt = (struct flac_seekpoint*)((char*)out + sizeof(struct flac_hdr));
	ffuint64 last_sample = (ffuint64)-1;
	for (ffuint i = 0;  i != npts;  i++) {
		const struct flac_seekpt *sp = &pts[i];

		if (sp->sample == last_sample)
			continue;

		*(ffuint64*)skpt[n].sample_number = ffint_be_cpu64(sp->sample);
		*(ffuint64*)skpt[n].stream_offset = ffint_be_cpu64(sp->off);
		*(ffushort*)skpt[n++].frame_samples = ffint_be_cpu16(blksize);
		last_sample = sp->sample;
	}

	for (;  n != npts;  n++) {
		*(ffuint64*)skpt[n].sample_number = ffint_be_cpu64(FLAC_SEEKPOINT_PLACEHOLDER);
		ffmem_zero(skpt[n].stream_offset, sizeof(skpt->stream_offset) + sizeof(skpt->frame_samples));
	}

	return sizeof(struct flac_hdr) + len;
}


/* Picture block:
type[4] // 0:Other 3:FrontCover 4:BackCover
mime_len[4]
mime[]
desc_len[4]
desc[]
width[4]
height[4]
bpp[4]
ncolors[4]
data_len[4]
data[]
*/
/** Parse picture block and return picture data
Return 0 on success */
static inline int flac_meta_pic(ffstr data, ffstr *pic)
{
	ffuint i = 0;
	const char *d = data.ptr;
	if (8 > data.len)
		return -1;
	i += 4;

	ffuint mime_len = ffint_be_cpu32_ptr(&d[i]);
	i += 4 + mime_len;
	if (i > data.len)
		return -1;

	ffuint desc_len = ffint_be_cpu32_ptr(&d[i]);
	i += 4 + desc_len;
	i += 4*4;
	if (i > data.len)
		return -1;

	ffuint n = ffint_be_cpu32_ptr(&d[i]);
	i += 4;
	if (i + n > data.len)
		return -1;

	// log("mime:%u  desc:%u  data:%u"
		// , mime_len, desc_len, n);

	ffstr_set(pic, &d[i], n);
	return 0;
}

struct flac_picinfo {
	const char *mime;
	const char *desc;
	ffuint width, height;
	ffuint bpp;
};

/** Write picture block data.
data: if NULL: return the number of bytes needed.
Return -1 on error */
static inline int flac_pic_write(void *data, ffsize cap, const struct flac_picinfo *info, const ffstr *pic, ffuint islast)
{
	ffstr mime, desc;
	ffstr_setz(&mime, info->mime);
	ffstr_setz(&desc, info->desc);
	ffsize n = sizeof(struct flac_hdr) + 4 + 4+mime.len + 4+desc.len + 4*4 + 4+pic->len;

	if (data == NULL)
		return n;

	char *d = (char*)data;
	if (n > cap
		|| n < (mime.len | desc.len | pic->len))
		return -1;

	d += sizeof(struct flac_hdr);

	*(ffuint*)d = ffint_be_cpu32(3);  d += 4;

	*(ffuint*)d = ffint_be_cpu32(mime.len);  d += 4;
	d = (char*)ffmem_copy(d, mime.ptr, mime.len);

	*(ffuint*)d = ffint_be_cpu32(desc.len);  d += 4;
	d = (char*)ffmem_copy(d, desc.ptr, desc.len);

	*(ffuint*)d = ffint_be_cpu32(info->width);  d += 4;
	*(ffuint*)d = ffint_be_cpu32(info->height);  d += 4;
	*(ffuint*)d = ffint_be_cpu32(info->bpp);  d += 4;
	*(ffuint*)d = ffint_be_cpu32(0);  d += 4;

	*(ffuint*)d = ffint_be_cpu32(pic->len);  d += 4;
	d = (char*)ffmem_copy(d, pic->ptr, pic->len);

	n = d - (char*)data;
	flac_hdr_write(data, FLAC_TPIC, islast, n - sizeof(struct flac_hdr));
	return n;
}

/**
Return <0 on error */
static int _flac_frame_samples(ffuint *psamples, const char *d, ffsize len)
{
	int i = 0;
	ffuint samples = *psamples;
	switch (samples) {
	case 0:
		return -1; // reserved

	case 1:
		samples = 192;
		break;

	case 6:
		if (1 > len)
			return -1;
		samples = (ffbyte)d[0] + 1;
		i = 1;
		break;

	case 7:
		if (2 > len)
			return -1;
		samples = ffint_be_cpu16_ptr(d) + 1;
		i = 2;
		break;

	default:
		if (samples & 0x08)
			samples = 256 << (samples & ~0x08);
		else
			samples = 576 << (samples - 2);
	}

	*psamples = samples;
	return i;
}

/**
Return <0 on error */
static int _flac_frame_rate(ffuint *prate, const char *d, ffsize len)
{
	int i = 0;
	ffuint rate = *prate;
	switch (rate) {
	case 0:
		break;

	case 0x0c:
		if (1 > len)
			return -1;
		rate = (ffuint)(ffbyte)d[0] * 1000;
		i = 1;
		break;

	case 0x0d:
	case 0x0e:
		if (2 > len)
			return -1;
		if (rate == 0x0d)
			rate = ffint_be_cpu16_ptr(d);
		else
			rate = (ffuint)ffint_be_cpu16_ptr(d) * 10;
		i = 2;
		break;

	case 0x0f:
		return -1; // invalid

	default: {
		static const ffushort rates[] = {
			0, 88200/10, 176400/10, 192000/10, 8000/10, 16000/10, 22050/10, 24000/10, 32000/10, 44100/10, 48000/10, 96000/10
		};
		rate = rates[rate] * 10;
	}
	}

	*prate = rate;
	return i;
}

struct flac_frame {
	ffuint num; //-1 for variable-blocksize stream
	ffuint64 pos;
	ffuint samples;
	ffuint rate;
	ffuint channels;
	ffuint bps;
};

/** Decode a UTF-8 number (extended to hold 36-bit value):
...
11111110 10xxxxxx*6
*/
static int _flac_utf8_decode36(const char *utf8, ffsize len, ffuint64 *val)
{
	ffuint d = (ffbyte)utf8[0];
	if ((d & 0x80) == 0) {
		*val = d;
		return 1;
	}

	ffuint n = ffbit_find32(~(d << 24) & 0xff000000);
	if (n < 3)
		return 0; // invalid first ffbyte
	n--;
	static const ffbyte utf8_b1masks[] = { 0x1f, 0x0f, 0x07, 0x03, 0x01, 0 };
	ffuint64 r = d & utf8_b1masks[n - 2];

	if (len < n)
		return -(int)n; // need more data

	for (ffuint i = 1;  i != n;  i++) {
		d = (ffbyte)utf8[i];
		if ((d & 0xc0) != 0x80)
			return 0; // invalid
		r = (r << 6) | (d & ~0xc0);
	}

	*val = r;
	return n;
}

FF_EXTERN unsigned char flac_crc8(const char *data, unsigned int len);

/* FLAC frame header:
byte sync_res_bs[2] // sync[14] res[1] blksize_var[1]
byte samples_rate // samples[4] rate[4]
byte chan_bps_res // chan[4] bps[3] res2[1]
byte frame_number[1..6] or sample_number[1..7]
byte samples[0..2] //=samples-1
byte rate[0..2]
byte crc8
*/
/**
Return the position after the header
 0 on error */
static inline ffuint flac_frame_read(struct flac_frame *fr, const char *d, ffsize len)
{
	int r;
	if (4 > len)
		return 0;
	ffuint v = ffint_be_cpu32_ptr(d);
	if ((v & 0xfffe0001) != 0xfff80000)
		return 0;

	ffuint bsvar = !!(v & 0x00010000);
	if (!bsvar) {
		r = ffutf8_decode(&d[4], len - 4, &fr->num);
		fr->pos = 0;
	} else {
		fr->num = -1;
		r = _flac_utf8_decode36(&d[4], len - 4, &fr->pos);
	}
	if (r <= 0)
		return 0;
	ffuint i = 4 + r;

	fr->samples = (v & 0x0000f000) >> 12;
	if (0 > (r = _flac_frame_samples(&fr->samples, &d[i], len - i)))
		return 0;
	i += r;

	fr->rate = (v & 0x00000f00) >> 8;
	if (0 > (r = _flac_frame_rate(&fr->rate, &d[i], len - i)))
		return 0;
	i += r;

	fr->channels = (v & 0x000000f0) >> 4;
	if (fr->channels >= 0x0b)
		return 0; // reserved
	else if (fr->channels & 0x08)
		fr->channels = 2;
	else
		fr->channels++;

	ffuint bps = (v & 0x0000000e) >> 1;
	if ((bps & 3) == 3)
		return 0; // reserved
	static const ffbyte flac_bps[] = { 0, 8, 12, 0, 16, 20, 24 };
	fr->bps = flac_bps[bps];

#ifndef FLACREAD_NOCRC
	if ((ffbyte)d[i] != flac_crc8(d, i))
		return 0; // header CRC mismatch
#endif
	i++;

	return i;
}

/** Find a valid frame header
Return header position
 <0 if not found */
static inline ffssize flac_frame_find(const void *data, ffsize len, struct flac_frame *fr, ffbyte hdr[4])
{
	// .... ....  .... ..RV  SSSS RRRR  CCCC BBBR
	ffuint mask = ffint_be_cpu32(0xffff0f0f);
	ffuint h = (*(ffuint*)hdr) & mask;
	char *d = (char*)data;
	const ffuint MIN_FRAME_HDR = 4 + 1 + 1;

	for (ffsize i = 0;  i != len;  i++) {
		if ((ffbyte)d[i] != 0xff) {
			ffssize r = ffs_findchar(&d[i], len - i, 0xff);
			if (r < 0)
				break;
			i += r;
		}

		if (i + MIN_FRAME_HDR > len)
			break;

		if (h == 0 || ((*(ffuint*)&d[i]) & mask) == h) {

			ffuint r = flac_frame_read(fr, &d[i], len - i);
			if (r != 0)
				return i;
		}
	}

	return -1;
}
