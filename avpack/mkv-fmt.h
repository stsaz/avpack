/** avpack: .mkv format
2016,2021, Simon Zolin
*/

/*
mkv_codec
mkv_varint mkv_varint_shift mkv_int_ntoh mkv_flt_ntoh
mkv_read_id_size
mkv_lacing
mkv_vorbis_hdr
mkv_el_find
*/

/* .mkv format:
(ELEMENT SIZE DATA)...
*/

#pragma once

#include <ffbase/string.h>
#include <ffbase/stringz.h>
#include <ffbase/vector.h>

enum MKV_E {
	MKV_EINTVAL = 1,
	MKV_EFLTVAL,
	MKV_ELACING,
	MKV_EVORBISHDR,
	MKV_EMEM,
};

/** Codec ID */
enum MKV_CODEC {
	MKV_A_AAC = 1,
	MKV_A_AC3,
	MKV_A_ALAC,
	MKV_A_FLAC,
	MKV_A_MPEGL3,
	MKV_A_OPUS,
	MKV_A_PCM,
	MKV_A_VORBIS,

	MKV_S_ASS,
	MKV_S_UTF8,

	MKV_V_AVC,
	MKV_V_HEVC,
};

// MKV_CODEC_ID:
static const char* const mkv_codecstr[] = {
	"A_AAC",
	"A_AC3",
	"A_ALAC",
	"A_FLAC",
	"A_MPEG/L3",
	"A_OPUS",
	"A_PCM/INT/LIT",
	"A_VORBIS",

	"S_TEXT/ASS",
	"S_TEXT/UTF8",

	"V_MPEG4/ISO/AVC",
	"V_MPEGH/ISO/HEVC",
};

/** Translate codec name to ID */
static int mkv_codec(ffstr name)
{
	int r = ffszarr_findsorted(mkv_codecstr, FF_COUNT(mkv_codecstr), name.ptr, name.len);
	if (r < 0)
		return -1;
	return r + 1;
}

/** Parse variable width integer.
1xxxxxxx
01xxxxxx xxxxxxxx
001xxxxx xxxxxxxx*2
...
Return N of bytes read
 <0: need at least -N bytes
 0: error */
static int mkv_varint(const void *data, ffsize len, ffuint64 *dst)
{
	if (len == 0)
		return -1;

	ffuint size;
	const ffbyte *d = (ffbyte*)data;
	if (0 == (size = ffbit_find32((d[0] << 24) & 0xff000000)))
		return 0;

	if (dst == NULL || size > len)
		return -(int)size;

	ffuint64 n = (ffuint)d[0];
	if (len != (ffsize)-1)
		n = n & ~(0x80 >> (size - 1));

	for (ffuint i = 1;  i != size;  i++) {
		n = (n << 8) | d[i];
	}
	*dst = n;
	return size;
}

/** Parse variable width integer and shift input data */
static int mkv_varint_shift(ffstr *data, ffuint64 *dst)
{
	int r;
	if (0 >= (r = mkv_varint(data->ptr, data->len, dst)))
		return -1;
	ffstr_shift(data, r);
	return r;
}

/** Get 1-ffbyte integer and shift input data */
static int mkv_byte_shift(ffstr *data, ffuint *dst)
{
	if (data->len == 0)
		return -1;
	*dst = data->ptr[0];
	ffstr_shift(data, 1);
	return 1;
}

static int mkv_int_ntoh(ffuint64 *dst, const char *d, ffsize len)
{
	switch (len) {
	case 1:
		*dst = *(ffbyte*)d;  break;
	case 2:
		*dst = ffint_be_cpu16_ptr(d);  break;
	case 3:
		*dst = ffint_be_cpu24_ptr(d);  break;
	case 4:
		*dst = ffint_be_cpu32_ptr(d);  break;
	case 8:
		*dst = ffint_be_cpu64_ptr(d);  break;
	default:
		return -1;
	}
	return 0;
}

static int mkv_flt_ntoh(double *dst, const char *d, ffsize len)
{
	union {
		ffuint u;
		ffuint64 u8;
		float f;
		double d;
	} u;

	switch (len) {
	case 4:
		u.u = ffint_be_cpu32_ptr(d);
		*dst = u.f;
		break;
	case 8:
		u.u8 = ffint_be_cpu64_ptr(d);
		*dst = u.d;
		break;
	default:
		return -1;
	}
	return 0;
}

/** Read element ID and size
Return N of bytes read
 <0: need at least -N bytes
  0: error */
static inline int mkv_read_id_size(ffstr d, ffuint *id, ffuint64 *size)
{
	int r, r2;
	r = mkv_varint(d.ptr, d.len, NULL);
	if (r == 0)
		return 0;
	r = -r;
	if ((ffuint)r > d.len)
		return -r;

	r2 = mkv_varint(d.ptr + r, d.len - r, size);
	if (r2 == 0)
		return 0;
	else if (r2 < 0)
		return -((int)d.len + -r2);

	ffuint64 id64 = 0;
	mkv_int_ntoh(&id64, d.ptr, r);
	*id = id64;
	return r + r2;
}

/** Read EBML lacing.
varint size_first
varint size_delta[]
*/
static int mkv_lacing_ebml(ffstr *data, ffuint *lace, ffuint n)
{
	int r;
	ffint64 val, prev = 0;

	if (0 > (r = mkv_varint_shift(data, (ffuint64*)&prev))
		|| prev > (ffuint)-1)
		return MKV_ELACING;
	*lace++ = prev;

	// log(, "EBML lacing: [%u] 0x%xU %*xb"
	// 	, n, prev, (ffsize)n - 1, data->ptr);

	for (ffuint i = 1;  i != n;  i++) {
		if (0 > (r = mkv_varint_shift(data, (ffuint64*)&val)))
			return MKV_ELACING;

		switch (r) {
		case 1:
			//.sxx xxxx  0..0x3e: negative;  0x40..0x7f: positive
			val = val - 0x3f;
			break;
		case 2:
			//..sx xxxx xxxx xxxx
			val = val - 0x1fff;
			break;
		default:
			return MKV_ELACING;
		}

		if (prev + val < 0)
			return MKV_ELACING;
		*lace++ = prev + val;
		prev = prev + val;
	}

	return 0;
}

static int _mkv_ogg_pktlen(const char *data, ffsize len, ffuint *pkt_size);

/** Read Xiph lacing */
static int mkv_lacing_xiph(ffstr *data, ffuint *lace, ffuint n)
{
	for (ffuint i = 0;  i != n;  i++) {
		int r = _mkv_ogg_pktlen(data->ptr, data->len, lace);
		if (r == 0)
			return MKV_ELACING;
		lace++;
		ffstr_shift(data, r);
	}
	return 0;
}

/** Read fixed-size lacing */
static int mkv_lacing_fixed(ffstr *data, ffuint *lace, ffuint n)
{
	if (data->len % n)
		return MKV_ELACING;
	for (ffuint i = 0;  i != n;  i++) {
		*lace++ = data->len / n;
	}
	return 0;
}

/** Read lacing data
ffbyte num_frames
ffbyte Xiph[] | ffbyte EBML[]
*/
static int mkv_lacing(ffstr *data, ffvec *lacing, ffuint type)
{
	int r;
	ffuint nframes;
	if (0 > mkv_byte_shift(data, &nframes))
		return MKV_EINTVAL;

	lacing->len = 0;
	if (NULL == ffvec_reallocT(lacing, nframes, ffuint))
		return MKV_EMEM;

	switch (type) {
	case 0x02:
		r = mkv_lacing_xiph(data, (ffuint*)lacing->ptr, nframes);
		break;

	case 0x04:
		r = mkv_lacing_fixed(data, (ffuint*)lacing->ptr, nframes);
		break;

	case 0x06:
		r = mkv_lacing_ebml(data, (ffuint*)lacing->ptr, nframes);
		break;
	}

	if (r != 0)
		return r;

	lacing->len = nframes;
	return 0;
}

/** Get packet length.
Return bytes processed;  0 on error */
static int _mkv_ogg_pktlen(const char *data, ffsize len, ffuint *pkt_size)
{
	ffuint i = 0, seglen, pktlen = 0;

	for (;;) {
		if (i == len)
			return 0;
		seglen = (ffbyte)data[i++];
		pktlen += seglen;
		if (seglen != 255)
			break;
	}

	*pkt_size = pktlen;
	return i;
}

struct mkv_vorbis {
	ffuint state;
	ffuint pkt2_off;
	ffuint pkt3_off;
	ffstr data;
};

/** Parse Vorbis codec private data.
Return 0 if packet is ready;
 1 if done;
 <0 on error */
/* PKTS_NUM PKT1_LEN PKT2_LEN  PKT1 PKT2 PKT3 */
static inline int mkv_vorbis_hdr(struct mkv_vorbis *m, ffstr *input, ffstr *output)
{
	switch (m->state) {
	case 0: {
		ffuint n, dataoff, pkt1_len, pkt2_len;
		ffstr s = *input;

		if (input->len < 3)
			return -MKV_EVORBISHDR;
		n = (ffbyte)input->ptr[0];
		if (n != 2)
			return -MKV_EVORBISHDR;
		ffstr_shift(&s, 1);

		if (0 == (n = _mkv_ogg_pktlen(s.ptr, s.len, &pkt1_len)))
			return -MKV_EVORBISHDR;
		ffstr_shift(&s, n);

		if (0 == (n = _mkv_ogg_pktlen(s.ptr, s.len, &pkt2_len)))
			return -MKV_EVORBISHDR;
		ffstr_shift(&s, n);

		dataoff = s.ptr - input->ptr;
		m->pkt2_off = dataoff + pkt1_len;
		m->pkt3_off = dataoff + pkt1_len + pkt2_len;

		ffstr_set(output, &input->ptr[dataoff], pkt1_len);
		break;
	}

	case 1:
		ffstr_set(output, &input->ptr[m->pkt2_off], input->len - m->pkt2_off);
		break;

	case 2:
		ffstr_set(output, &input->ptr[m->pkt3_off], input->len - m->pkt3_off);
		break;

	case 3:
		return 1;
	}

	m->state++;
	return 0;
}

enum {
	MKV_MASK_ELID = 0x000000ff,
};

enum MKV_TRKTYPE {
	MKV_TRK_VIDEO = 1,
	MKV_TRK_AUDIO = 2,
	MKV_TRK_SUBS = 17,
};

enum MKV_ELID {
	MKV_T_UKN = -1,
	MKV_T_ANY,

	MKV_T_SEG,
	MKV_T_VER,
	MKV_T_DOCTYPE, // "matroska"
	MKV_T_TRACKS,
	MKV_T_SCALE,
	MKV_T_TITLE,
	MKV_T_DUR,

	MKV_T_SEEKID,
	MKV_T_SEEKPOS,

	MKV_T_TRKNAME,
	MKV_T_TRKNO,
	MKV_T_TRKENT,
	MKV_T_TRKTYPE, // enum MKV_TRKTYPE
	MKV_T_CODEC_ID, // MKV_CODEC_ID
	MKV_T_CODEC_PRIV,

	MKV_T_V_WIDTH,
	MKV_T_V_HEIGHT,

	MKV_T_A_RATE,
	MKV_T_A_OUTRATE,
	MKV_T_A_CHANNELS,
	MKV_T_A_BITS,

	MKV_T_TAG,
	MKV_T_TAG_NAME,
	MKV_T_TAG_VAL,
	MKV_T_TAG_BVAL,

	MKV_T_CLUST,
	MKV_T_TIME,
	MKV_T_BLOCK,
	MKV_T_SBLOCK,
};

enum MKV_FLAGS {
	MKV_F_LAST = 0x0100,
	MKV_F_INT = 0x0200,
	MKV_F_FLT = 0x0400,
	MKV_F_WHOLE = 0x0800,
	MKV_F_REQ = 0x1000,
	MKV_F_MULTI = 0x2000,
	MKV_F_INT8 = 0x4000,
};

struct mkv_binel {
	ffuint id;
	ffuint flags; // PRIO MKV_FLAGS ID
	const struct mkv_binel *children;
};

/** Search element in the context.
Return -1 if not found */
static int mkv_el_find(const struct mkv_binel *ctx, ffuint id)
{
	for (ffuint i = 0;  ;  i++) {

		if (id == ctx[i].id)
			return i;

		if (ctx[i].flags & MKV_F_LAST)
			return -1;
	}
}

#define MKV_DEF(n)  (0)

/** Priority, strict order of elements.
0: unspecified
1: highest priority
>1: require previous element with smaller number */
#define MKV_PRIO(n)  ((n) << 24)
#define MKV_GET_PRIO(flags)  ((flags & 0xff000000) >> 24)

/* Supported elements:

EBMLHead (0x1a45dfa3)
 EBMLVersion (0x4286)
 EBMLDoocType (0x4282)
Segment (0x18538067)
 SeekHead (0x114d9b74)
  Seek (0x4dbb)
   SeekID (0x53ab)
   SeekPosition (0x53ac)
 Info (0x1549a966)
  TimecodeScale (0x2ad7b1)
  Duration (0x4489)
  Title (0x7ba9)
 Tracks (0x1654ae6b)
  TrackEntry (0xae)
   Name (0x536e)
   //TrackUID (0x73c5) uint64
   //DefaultDuration (0x23e383) uint
   TrackNumber (0xd7)
   TrackType (0x83)
   CodecID (0x86)
   CodecPrivate (0x63a2)
   Video (0xe0)
    PixelWidth (0xb0)
    PixelHeight (0xba)
   Audio (0xe1)
    SamplingFrequency (0xb5)
    OutputSamplingFrequency (0x78b5)
    Channels (0x9f)
    BitDepth (0x6264)
 //Chapters (0x1043a770)
 Tags (0x1254c367)
  Tag (0x7373)
   SimpleTag (0x67c8)
    TagName (0x45a3)
    TagString (0x4487)
    TagBinary (0x4485)
 Cluster (0x1f43b675)
  Timecode (0xe7)
  BlockGroup (0xa0)
  Block (0xa1)
  SimpleBlock (0xa3)
*/

static const struct mkv_binel mkv_ctx_head[];
static const struct mkv_binel mkv_ctx_segment[];
static const struct mkv_binel mkv_ctx_info[];
static const struct mkv_binel mkv_ctx_seek[];
static const struct mkv_binel mkv_ctx_seekpt[];
static const struct mkv_binel mkv_ctx_tracks[];
static const struct mkv_binel mkv_ctx_trackentry[];
static const struct mkv_binel mkv_ctx_trackentry_video[];
static const struct mkv_binel mkv_ctx_trackentry_audio[];
static const struct mkv_binel mkv_ctx_tags[];
static const struct mkv_binel mkv_ctx_tag[];
static const struct mkv_binel mkv_ctx_tag_simple[];
static const struct mkv_binel mkv_ctx_cluster[];
static const struct mkv_binel mkv_ctx_cluster_blkgrp[];

static const struct mkv_binel mkv_ctx_global[] = {
	{ 0x1A45DFA3, MKV_F_REQ | MKV_PRIO(1), mkv_ctx_head },
	{ 0x18538067, MKV_T_SEG | MKV_F_REQ | MKV_PRIO(2) | MKV_F_LAST, mkv_ctx_segment },
};

static const struct mkv_binel mkv_ctx_head[] = {
	{ 0x4286, MKV_T_VER | MKV_F_INT | MKV_F_REQ, NULL },
	{ 0x4282, MKV_T_DOCTYPE | MKV_F_WHOLE | MKV_F_REQ | MKV_F_LAST, NULL },
};

static const struct mkv_binel mkv_ctx_segment[] = {
	{ 0x1549a966, 0 | MKV_PRIO(1), mkv_ctx_info },
	{ 0x1654ae6b, MKV_T_TRACKS | MKV_F_REQ | MKV_PRIO(2), mkv_ctx_tracks },
	{ 0x114d9b74, 0, mkv_ctx_seek },
	{ 0x1254c367, 0, mkv_ctx_tags },
	{ 0x1f43b675, MKV_T_CLUST | MKV_F_MULTI | MKV_PRIO(3) | MKV_F_LAST, mkv_ctx_cluster },
};

static const struct mkv_binel mkv_ctx_info[] = {
	{ 0x2ad7b1,	MKV_T_SCALE | MKV_F_INT | MKV_DEF(1000000), NULL },
	{ 0x7ba9,	MKV_T_TITLE | MKV_F_WHOLE, NULL },
	{ 0x4489,	MKV_T_DUR | MKV_F_FLT | MKV_F_LAST, NULL },
};

static const struct mkv_binel mkv_ctx_seek[] = {
	{ 0x4dbb, 0 | MKV_F_MULTI | MKV_F_LAST, mkv_ctx_seekpt },
};
static const struct mkv_binel mkv_ctx_seekpt[] = {
	{ 0x53ab, MKV_T_SEEKID | MKV_F_INT, NULL },
	{ 0x53ac, MKV_T_SEEKPOS | MKV_F_INT | MKV_F_LAST, NULL },
};

static const struct mkv_binel mkv_ctx_tracks[] = {
	{ 0xae, MKV_T_TRKENT | MKV_F_MULTI | MKV_F_LAST, mkv_ctx_trackentry },
};
static const struct mkv_binel mkv_ctx_trackentry[] = {
	{ 0x536e,	MKV_T_TRKNAME | MKV_F_WHOLE, NULL },
	{ 0xd7,	MKV_T_TRKNO | MKV_F_INT, NULL },
	{ 0x83,	MKV_T_TRKTYPE | MKV_F_INT, NULL },
	{ 0x86,	MKV_T_CODEC_ID | MKV_F_WHOLE, NULL },
	{ 0x63a2,	MKV_T_CODEC_PRIV | MKV_F_WHOLE, NULL },
	{ 0xe0,	0, mkv_ctx_trackentry_video },
	{ 0xe1,	MKV_F_LAST, mkv_ctx_trackentry_audio },
};
static const struct mkv_binel mkv_ctx_trackentry_video[] = {
	{ 0xb0, MKV_T_V_WIDTH | MKV_F_INT, NULL },
	{ 0xba, MKV_T_V_HEIGHT | MKV_F_INT | MKV_F_LAST, NULL },
};
static const struct mkv_binel mkv_ctx_trackentry_audio[] = {
	{ 0xb5,	MKV_T_A_RATE | MKV_F_FLT, NULL },
	{ 0x78b5,	MKV_T_A_OUTRATE | MKV_F_FLT, NULL },
	{ 0x9f,	MKV_T_A_CHANNELS | MKV_F_INT, NULL },
	{ 0x6264,	MKV_T_A_BITS | MKV_F_INT | MKV_F_LAST, NULL },
};

static const struct mkv_binel mkv_ctx_tags[] = {
	{ 0x7373, MKV_F_MULTI | MKV_F_LAST, mkv_ctx_tag },
};
static const struct mkv_binel mkv_ctx_tag[] = {
	{ 0x67c8, MKV_T_TAG | MKV_F_MULTI | MKV_F_LAST, mkv_ctx_tag_simple },
};
static const struct mkv_binel mkv_ctx_tag_simple[] = {
	{ 0x45a3, MKV_T_TAG_NAME | MKV_F_WHOLE, NULL },
	{ 0x4487, MKV_T_TAG_VAL | MKV_F_WHOLE, NULL },
	{ 0x4485, MKV_T_TAG_BVAL | MKV_F_WHOLE | MKV_F_LAST, NULL },
};

static const struct mkv_binel mkv_ctx_cluster[] = {
	{ 0xe7, MKV_T_TIME | MKV_F_INT | MKV_F_REQ | MKV_PRIO(1), NULL },
	{ 0xa0, 0 | MKV_F_MULTI | MKV_PRIO(2), mkv_ctx_cluster_blkgrp },
	{ 0xa1, MKV_T_BLOCK | MKV_F_MULTI | MKV_F_WHOLE | MKV_PRIO(2), NULL },
	{ 0xa3, MKV_T_SBLOCK | MKV_F_MULTI | MKV_F_WHOLE | MKV_PRIO(2) | MKV_F_LAST, NULL },
};

static const struct mkv_binel mkv_ctx_cluster_blkgrp[] = {
	{ 0xa1, MKV_T_BLOCK | MKV_F_MULTI | MKV_F_WHOLE, NULL },
	{ 0xa3, MKV_T_SBLOCK | MKV_F_MULTI | MKV_F_WHOLE | MKV_F_LAST, NULL },
};
