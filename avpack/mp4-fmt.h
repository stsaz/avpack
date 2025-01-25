/** avpack: .mp4 format
2016,2021, Simon Zolin
*/

/*
mp4_hdlr_write
mp4_box_find
mp4_box_write
mp4_fbox_write
mp4_tkhd_write
mp4_mvhd_write mp4_mdhd_write
mp4_stsd_write
CODEC:
	mp4_afmt_read mp4_afmt_write
	mp4_esds_read mp4_esds_write
	mp4_avc1_read
MAPS:
	mp4_seek
	mp4_stts_read mp4_stts_write
	mp4_stsc_read mp4_stsc_write
	mp4_stsz_size
	mp4_stsz_read mp4_stsz_add
	mp4_stco_size
	mp4_stco_read mp4_stco_add
META:
	mp4_ilst_find
	mp4_ilst_data_read mp4_ilst_data_write
	mp4_ilst_trkn_data_write
	mp4_itunes_smpb_read mp4_itunes_smpb_write
*/

/* .mp4 format:
box|box64(box|box64(...))...

ftyp()
moov(
 trak(mdia.minf.stbl(
  stsd(mp4a()|alac()|avc1())
  stts(AUDIO_POS => SAMPLE)
  stsc(SAMPLE => CHUNK)
  stco|co64(CHUNK => FILE_OFF)
  stsz(SAMPLE => SIZE))
 )...
 udta.meta.ilst(META_NAME(data(META_VAL)))...)
mdat(CHUNK(SAMPLE(DATA)...)...)
*/

#pragma once

#include <avpack/mmtag.h>
#include <avpack/id3v1.h>
#include <ffbase/string.h>

struct mp4box {
	ffbyte size[4];
	char type[4];
};

struct mp4box64 {
	ffbyte size[4]; //=1
	char type[4];
	ffbyte largesize[8];
};

struct mp4_fullbox {
	ffbyte version;
	ffbyte flags[3];
};

struct mp4_hdlr {
	ffbyte unused[4];
	ffbyte type[4]; //"soun"
	ffbyte unused2[12];
};

static inline int mp4_hdlr_write(void *dst)
{
	struct mp4_hdlr *hdlr = (struct mp4_hdlr*)dst;
	ffmem_zero_obj(hdlr);
	ffmem_copy(hdlr->type, "soun", 4);
	return sizeof(struct mp4_hdlr);
}

struct mp4_smhd {
	ffbyte unused[4];
};

struct mp4_dref {
	ffbyte cnt[4];
};


enum BOX {
	BOX_ANY,
	BOX_FTYP,
	BOX_MOOV,
	BOX_MVHD,
	BOX_TRAK,
	BOX_MDHD,
	BOX_HDLR,
	BOX_DREF,
	BOX_DREF_URL,
	BOX_TKHD,
	BOX_STSD,
	BOX_STSZ,
	BOX_STSC,
	BOX_STTS,
	BOX_STCO,
	BOX_CO64,
	BOX_STSD_ALAC,
	BOX_STSD_AVC1,
	BOX_STSD_MP4A,
	BOX_ALAC,
	BOX_ESDS,
	BOX_ILST_DATA,

	BOX_ITUNES,
	BOX_ITUNES_MEAN,
	BOX_ITUNES_NAME,
	BOX_ITUNES_DATA,

	BOX_MDAT,

	_BOX_TAG,
	//MMTAG_*
	BOX_TAG_GENRE_ID31 = _MMTAG_N,
};

enum MP4_F {
	MP4_F_WHOLE = 0x100, //wait until the whole box is in memory
	MP4_F_FULLBOX = 0x200, //box inherits "struct mp4_fullbox"
	MP4_F_REQ = 0x400, //mandatory box
	MP4_F_MULTI = 0x800, //allow multiple occurrences
	MP4_F_RO = 0x1000, // read-only
	MP4_F_LAST = 0x8000, //the last box in context
};

struct mp4_bbox {
	char type[4];
	ffuint flags; // "minsize" "priority" "enum MP4_F" "enum BOX"
	const struct mp4_bbox *ctx;
};

#define MP4_GET_TYPE(f)  ((f) & 0xff)
#define MP4_MINSIZE(n)  ((n) << 24)
#define MP4_GET_MINSIZE(f)  ((f & 0xff000000) >> 24)

/** Priority, strict order of boxes.
0: unspecified
1: highest priority
>1: require previous box with lower number */
#define MP4_PRIO(n)  ((n) << 16)
#define MP4_GET_PRIO(flags)  ((flags & 0x00ff0000) >> 16)

extern const struct mp4_bbox mp4_ctx_global[];
extern const struct mp4_bbox mp4_ctx_global_stream[];


/** Search box in the context.
Return -1 if not found. */
static inline int mp4_box_find(const struct mp4_bbox *ctx, const char type[4])
{
	for (ffuint i = 0;  ;  i++) {
		if (!ffmem_cmp(type, ctx[i].type, 4))
			return i;
		if (ctx[i].flags & MP4_F_LAST)
			break;
	}
	return -1;
}

/**
Return total box size. */
static inline int mp4_box_write(const char *type, char *dst, ffsize len)
{
	struct mp4box *b = (struct mp4box*)dst;
	*(ffuint*)b->size = ffint_be_cpu32(len + sizeof(struct mp4box));
	ffmem_copy(b->type, type, 4);
	return len + sizeof(struct mp4box);
}

/**
Return total box size. */
static inline int mp4_fbox_write(const char *type, char *dst, ffsize len)
{
	len += sizeof(struct mp4_fullbox);
	return mp4_box_write(type, dst, len);
}


struct mp4_ftyp {
	char major_brand[4];
	ffbyte minor_version[4];

	char compatible_brands[4];
	//...
};


struct mp4_tkhd0 {
	//flags = 0x07
	ffbyte creat_time[4];
	ffbyte mod_time[4];
	ffbyte id[4];
	ffbyte res[4];
	ffbyte duration[4];

	ffbyte unused[4 * 15];
};
struct mp4_tkhd1 {
	//flags = 0x07
	ffbyte creat_time[8];
	ffbyte mod_time[8];
	ffbyte id[4];
	ffbyte res[4];
	ffbyte duration[8];

	ffbyte unused[4 * 15];
};

static inline int mp4_tkhd_write(char *dst, ffuint id, ffuint64 total_samples)
{
	(void)id;
	struct mp4_fullbox *fbox = (struct mp4_fullbox*)dst;
	fbox->version = 1;
	fbox->flags[2] = 0x07;

	struct mp4_tkhd1 *tkhd = (struct mp4_tkhd1*)(fbox + 1);
	*(ffuint*)tkhd->id = ffint_be_cpu32(1);
	*(ffuint64*)tkhd->duration = ffint_be_cpu64(total_samples);
	return sizeof(struct mp4_tkhd1);
}


struct mp4_mvhd0 {
	ffbyte creat_time[4];
	ffbyte mod_time[4];
	ffbyte timescale[4];
	ffbyte duration[4];
	ffbyte unused[80];
};
struct mp4_mvhd1 {
	ffbyte creat_time[8];
	ffbyte mod_time[8];
	ffbyte timescale[4];
	ffbyte duration[8];
	ffbyte unused[80];
};

static inline int mp4_mvhd_write(char *dst, ffuint rate, ffuint64 total_samples)
{
	struct mp4_fullbox *fbox = (struct mp4_fullbox*)dst;
	fbox->version = 1;

	struct mp4_mvhd1 *mvhd = (struct mp4_mvhd1*)(fbox + 1);
	*(ffuint*)mvhd->timescale = ffint_be_cpu32(rate);
	*(ffuint64*)mvhd->duration = ffint_be_cpu64(total_samples);
	return sizeof(struct mp4_mvhd1);
}


struct mp4_mdhd0 {
	ffbyte creat_time[4];
	ffbyte mod_time[4];
	ffbyte timescale[4];
	ffbyte duration[4];
	ffbyte unused[4];
};

static inline int mp4_mdhd_write(char *dst, ffuint rate, ffuint64 total_samples)
{
	struct mp4_fullbox *fbox = (struct mp4_fullbox*)dst;
	fbox->version = 0;

	struct mp4_mdhd0 *mdhd = (struct mp4_mdhd0*)(fbox + 1);
	*(ffuint*)mdhd->timescale = ffint_be_cpu32(rate);
	*(ffuint*)mdhd->duration = ffint_be_cpu32(total_samples);
	return sizeof(struct mp4_mdhd0);
}


struct mp4_stsd {
	ffbyte cnt[4];
};

static inline int mp4_stsd_write(char *dst)
{
	struct mp4_stsd *stsd = (struct mp4_stsd*)dst;
	*(ffuint*)stsd->cnt = ffint_be_cpu32(1);
	return sizeof(struct mp4_stsd);
}


struct mp4_afmt {
	ffbyte res[6];
	ffbyte unused[2];

	ffbyte ver[2];
	ffbyte res2[6];
	ffbyte channels[2];
	ffbyte bits[2];
	ffbyte unused2[4];
	ffbyte rate[2];
	ffbyte rate_res[2];

	ffbyte ext[0];
};
/* QuickTime Sound Sample Description v1 */
struct mp4_afmt_exv1 {
	ffbyte unused[4*4];
};
/* QuickTime Sound Sample Description v2 */
struct mp4_afmt_exv2 {
	ffbyte unused[7*4+8];
};
struct mp4_aformat {
	ffuint bits;
	ffuint channels;
	ffuint rate;
};
struct mp4_alac {
	ffbyte conf[24];
	ffbyte chlayout[0]; //optional, 24 bytes
};

/** Read 'stsd.mp4a' or 'stsd.alac' data */
static inline int mp4_afmt_read(ffstr data, struct mp4_aformat *f)
{
	const struct mp4_afmt *afmt = (struct mp4_afmt*)data.ptr;
	f->bits = ffint_be_cpu16_ptr(afmt->bits);
	f->channels = ffint_be_cpu16_ptr(afmt->channels);

	if (f->rate == 0)
		f->rate = ffint_be_cpu16_ptr(afmt->rate);

	ffuint ver = ffint_be_cpu16_ptr(afmt->ver);
	if (ver == 1) {
		if (sizeof(struct mp4_afmt) + sizeof(struct mp4_afmt_exv1) <= data.len)
			return sizeof(struct mp4_afmt) + sizeof(struct mp4_afmt_exv1);
	} else if (ver == 2) {
		if (sizeof(struct mp4_afmt) + sizeof(struct mp4_afmt_exv2) <= data.len)
			return sizeof(struct mp4_afmt) + sizeof(struct mp4_afmt_exv2);
	}
	return sizeof(struct mp4_afmt);
}

/** Write 'stsd.mp4a' data */
static inline ffuint mp4_afmt_write(void *dst, const struct mp4_aformat *f)
{
	struct mp4_afmt *afmt = (struct mp4_afmt*)dst;
	*(ffushort*)afmt->bits = ffint_be_cpu16(f->bits);
	*(ffushort*)afmt->channels = ffint_be_cpu16(f->channels);
	if (f->rate <= 0xffff)
		*(ffushort*)afmt->rate = ffint_be_cpu16(f->rate);
	return sizeof(struct mp4_afmt);
}


enum MP4_ESDS_DEC_TYPE {
	MP4_ESDS_DEC_MPEG4_AUDIO = 0x40,
	MP4_ESDS_DEC_MPEG1_AUDIO = 0x6b,
};
struct mp4_acodec {
	ffuint type; //enum MP4_ESDS_DEC_TYPE
	ffuint stm_type;
	ffuint max_brate;
	ffuint avg_brate;
	const char *conf;
	ffuint conflen;
};

/* "esds" box:
(TAG SIZE ESDS) {
	(TAG SIZE DEC_CONF) {
		(TAG SIZE DEC_SPEC)
	}
	(TAG SIZE SL)
} */

enum MP4_ESDS_TAGS {
	MP4_ESDS_TAG = 3,
	MP4_ESDS_DEC_TAG = 4,
	MP4_ESDS_DECSPEC_TAG = 5,
	MP4_ESDS_SL_TAG = 6,
};
struct mp4_esds_tag {
	ffbyte tag; //enum MP4_ESDS_TAGS
	ffbyte size[4]; //"NN" | "80 80 80 NN"
};
struct mp4_esds {
	ffbyte unused[3];
};
struct mp4_esds_dec {
	ffbyte type; //enum MP4_ESDS_DEC_TYPE
	ffbyte stm_type;
	ffbyte unused[3];
	ffbyte max_brate[4];
	ffbyte avg_brate[4];
};
struct mp4_esds_decspec {
	ffbyte data[2]; //Audio Specific Config
};
struct mp4_esds_sl {
	ffbyte val;
};

/** Get next esds block.
@size: input: minimum block size;  output: actual block size
Return block tag;  0 on error. */
static int mp4_esds_block(const char **pd, const char *end, ffuint *size)
{
	const struct mp4_esds_tag *tag = (struct mp4_esds_tag*)*pd;
	ffuint sz;

	if (*pd + 2 > end)
		return 0;

	if (tag->size[0] != 0x80) {
		*pd += 2;
		sz = tag->size[0];

	} else {
		*pd += sizeof(struct mp4_esds_tag);
		if (*pd > end)
			return 0;
		sz = tag->size[3];
	}

	if (sz < *size)
		return 0;
	*size = sz;
	return tag->tag;
}

static ffuint mp4_esds_block_write(char *dst, ffuint tag, ffuint size)
{
	struct mp4_esds_tag *t = (struct mp4_esds_tag*)dst;
	t->tag = tag;
	*(ffuint*)t->size = ffint_be_cpu32(0x80808000 | size);
	return sizeof(struct mp4_esds_tag);
}

/**
Return 0 on success;
 <0 on error. */
static inline int mp4_esds_read(const char *data, ffuint len, struct mp4_acodec *ac)
{
	const char *d = data;
	const char *end = data + len;
	int r = -1;
	ffuint size;

	size = sizeof(struct mp4_esds);
	if (MP4_ESDS_TAG == mp4_esds_block(&d, end, &size)) {
		d += sizeof(struct mp4_esds);

		size = sizeof(struct mp4_esds_dec);
		if (MP4_ESDS_DEC_TAG == mp4_esds_block(&d, end, &size)) {
			const struct mp4_esds_dec *dec = (struct mp4_esds_dec*)d;
			d += sizeof(struct mp4_esds_dec);
			ac->type = dec->type;
			ac->stm_type = dec->stm_type;
			ac->max_brate = ffint_be_cpu32_ptr(dec->max_brate);
			ac->avg_brate = ffint_be_cpu32_ptr(dec->avg_brate);

			size = sizeof(struct mp4_esds_decspec);
			if (MP4_ESDS_DECSPEC_TAG == mp4_esds_block(&d, end, &size)) {
				const struct mp4_esds_decspec *spec = (struct mp4_esds_decspec*)d;
				d += size;
				ac->conf = (char*)spec->data,  ac->conflen = size;
				r = 0;
			}
		}
	}

	return r;
}

static inline int mp4_esds_write(char *dst, const struct mp4_acodec *ac)
{
	char *d = dst;
	ffuint total = sizeof(struct mp4_esds)
		+ sizeof(struct mp4_esds_tag) + sizeof(struct mp4_esds_dec)
		+ sizeof(struct mp4_esds_tag) + ac->conflen
		+ sizeof(struct mp4_esds_tag) + 1;

	d += mp4_esds_block_write(d, MP4_ESDS_TAG, total);
	ffmem_zero(d, sizeof(struct mp4_esds));
	d += sizeof(struct mp4_esds);

	d += mp4_esds_block_write(d, MP4_ESDS_DEC_TAG, sizeof(struct mp4_esds_dec) + sizeof(struct mp4_esds_tag) + ac->conflen);
	struct mp4_esds_dec *dec = (struct mp4_esds_dec*)d;
	dec->type = ac->type;
	dec->stm_type = ac->stm_type;
	ffmem_zero(dec->unused, sizeof(dec->unused));
	*(ffuint*)dec->max_brate = ffint_be_cpu32(ac->max_brate);
	*(ffuint*)dec->avg_brate = ffint_be_cpu32(ac->avg_brate);
	d += sizeof(struct mp4_esds_dec);

	d += mp4_esds_block_write(d, MP4_ESDS_DECSPEC_TAG, ac->conflen);
	struct mp4_esds_decspec *spec = (struct mp4_esds_decspec*)d;
	ffmem_copy(spec->data, ac->conf, ac->conflen);
	d += ac->conflen;

	d += mp4_esds_block_write(d, MP4_ESDS_SL_TAG, 1);
	struct mp4_esds_sl *sl = (struct mp4_esds_sl*)d;
	sl->val = 0x02;
	d += sizeof(struct mp4_esds_sl);

	return d - dst;
}


struct mp4_video {
	ffuint width, height;
};
struct mp4_avc1_read {
	ffbyte reserved1[6];
	ffbyte data_reference_index[2];
	ffbyte reserved2[16];
	ffbyte width[2];
	ffbyte height[2];
	ffbyte reserved3[14];
	ffbyte unused[4];
	ffbyte reserved4[4];
};

static inline int mp4_avc1_read(const char *data, ffuint len, struct mp4_video *vi)
{
	const struct mp4_avc1_read *a = (struct mp4_avc1_read*)data;
	FF_ASSERT(len >= sizeof(struct mp4_avc1_read));
	vi->width = ffint_be_cpu16_ptr(a->width);
	vi->height = ffint_be_cpu16_ptr(a->height);
	return 0;
}


struct mp4_seekpt {
	ffuint64 audio_pos;
	ffuint size;
	ffuint chunk_id; //index to ffmp4.chunktab
};

/**
Return the index of lower-bound seekpoint;
 -1 on error. */
static inline int mp4_seek(const struct mp4_seekpt *pts, ffsize npts, ffuint64 sample)
{
	ffsize n = npts;
	ffuint i = -1, start = 0;

	while (start != n) {
		i = start + (n - start) / 2;
		if (sample == pts[i].audio_pos)
			return i;
		else if (sample < pts[i].audio_pos)
			n = i--;
		else
			start = i + 1;
	}

	if (i == (ffuint)-1 || i == npts - 1)
		return -1;

	FF_ASSERT(sample > pts[i].audio_pos && sample < pts[i + 1].audio_pos);
	return i;
}


struct mp4_stts_ent {
	ffbyte sample_cnt[4];
	ffbyte sample_delta[4];
};
struct mp4_stts {
	ffbyte cnt[4];
	struct mp4_stts_ent ents[0];
};

/** Set audio position (in samples) for each seek point.
The last entry is always equal to the total samples.
Return total samples;
 <0 on error. */
static inline ffint64 mp4_stts_read(struct mp4_seekpt *sk, ffuint skcnt, const char *data, ffuint len)
{
	const struct mp4_stts *stts = (struct mp4_stts*)data;
	const struct mp4_stts_ent *ents = (struct mp4_stts_ent*)stts->ents;
	ffuint64 pos = 0;
	ffuint i, k, cnt, isk = 0;

	cnt = ffint_be_cpu32_ptr(stts->cnt);
	if (len < sizeof(struct mp4_stts) + cnt * sizeof(struct mp4_stts_ent))
		return -1;

	for (i = 0;  i != cnt;  i++) {
		ffuint nsamps = ffint_be_cpu32_ptr(ents[i].sample_cnt);
		ffuint delt = ffint_be_cpu32_ptr(ents[i].sample_delta);

		if (isk + nsamps >= skcnt)
			return -1;

		for (k = 0;  k != nsamps;  k++) {
			sk[isk++].audio_pos = pos + delt * k;
		}
		pos += nsamps * delt;
	}

	if (isk != skcnt - 1)
		return -1;

	sk[skcnt - 1].audio_pos = pos;
	return pos;
}

static inline int mp4_stts_write(char *dst, ffuint64 total_samples, ffuint framelen)
{
	struct mp4_stts *stts = (struct mp4_stts*)dst;
	struct mp4_stts_ent *ent = stts->ents;

	if ((total_samples / framelen) != 0) {
		*(ffuint*)ent->sample_cnt = ffint_be_cpu32(total_samples / framelen);
		*(ffuint*)ent->sample_delta = ffint_be_cpu32(framelen);
		ent++;
	}

	if ((total_samples % framelen) != 0) {
		*(ffuint*)ent->sample_cnt = ffint_be_cpu32(1);
		*(ffuint*)ent->sample_delta = ffint_be_cpu32(total_samples % framelen);
		ent++;
	}

	*(ffuint*)stts->cnt = ffint_be_cpu32(ent - stts->ents);
	return sizeof(struct mp4_stts) + (ent - stts->ents) * sizeof(struct mp4_stts_ent);
}


struct mp4_stsc_ent {
	ffbyte first_chunk[4];
	ffbyte chunk_samples[4];
	ffbyte sample_description_index[4];
};
struct mp4_stsc {
	ffbyte cnt[4];
	struct mp4_stsc_ent ents[0];
};

/** Set chunk index for each seek point. */
static inline int mp4_stsc_read(struct mp4_seekpt *sk, ffuint skcnt, const char *data, ffuint len)
{
	const struct mp4_stsc *stsc = (struct mp4_stsc*)data;
	const struct mp4_stsc_ent *e = &stsc->ents[0];
	ffuint i, cnt, isk = 0, ich, isamp;

	cnt = ffint_be_cpu32_ptr(stsc->cnt);
	if (cnt == 0 || len < sizeof(struct mp4_stsc) + cnt * sizeof(struct mp4_stsc_ent))
		return -1;

	ffuint nsamps = ffint_be_cpu32_ptr(e[0].chunk_samples);
	ffuint prev_first_chunk = ffint_be_cpu32_ptr(e[0].first_chunk);
	if (prev_first_chunk == 0)
		return -1;

	for (i = 1;  i != cnt;  i++) {
		ffuint first_chunk = ffint_be_cpu32_ptr(e[i].first_chunk);

		if (prev_first_chunk >= first_chunk
			|| isk + (first_chunk - prev_first_chunk) * nsamps >= skcnt)
			return -1;

		for (ich = prev_first_chunk;  ich != first_chunk;  ich++) {
			for (isamp = 0;  isamp != nsamps;  isamp++) {
				sk[isk++].chunk_id = ich - 1;
			}
		}

		nsamps = ffint_be_cpu32_ptr(e[i].chunk_samples);
		prev_first_chunk = ffint_be_cpu32_ptr(e[i].first_chunk);
	}

	for (ich = prev_first_chunk;  ;  ich++) {
		if (isk + nsamps >= skcnt)
			return -1;
		for (isamp = 0;  isamp != nsamps;  isamp++) {
			sk[isk++].chunk_id = ich - 1;
		}
		if (isk == skcnt - 1)
			break;
	}

	return 0;
}

static inline int mp4_stsc_write(char *dst, ffuint64 total_samples, ffuint frame_samples, ffuint chunk_samples)
{
	struct mp4_stsc *stsc = (struct mp4_stsc*)dst;
	struct mp4_stsc_ent *ent = stsc->ents;

	ffuint total_frames = total_samples / frame_samples + !!(total_samples % frame_samples);
	ffuint chunk_frames = chunk_samples / frame_samples;

	if (0 != total_frames / chunk_frames) {
		*(ffuint*)ent->first_chunk= ffint_be_cpu32(1);
		*(ffuint*)ent->chunk_samples= ffint_be_cpu32(chunk_frames);
		*(ffuint*)ent->sample_description_index= ffint_be_cpu32(1);
		ent++;
	}

	if (0 != (total_frames % chunk_frames)) {
		*(ffuint*)ent->first_chunk= ffint_be_cpu32(total_frames / chunk_frames + 1);
		*(ffuint*)ent->chunk_samples= ffint_be_cpu32(total_frames % chunk_frames);
		*(ffuint*)ent->sample_description_index= ffint_be_cpu32(1);
		ent++;
	}

	ffuint cnt = ent - stsc->ents;
	*(ffuint*)stsc->cnt= ffint_be_cpu32(cnt);
	return sizeof(struct mp4_stsc) + cnt * sizeof(struct mp4_stsc_ent);
}


struct mp4_stsz {
	ffbyte def_size[4];
	ffbyte cnt[4];
	ffbyte size[0][4]; // if def_size == 0
};

/** Initialize seek table.  Set size (in bytes) for each seek point.
Return the number of seek points;
 <0 on error. */
static inline int mp4_stsz_read(const char *data, ffuint len, struct mp4_seekpt *sk)
{
	const struct mp4_stsz *stsz = (struct mp4_stsz*)data;
	ffuint i, def_size, cnt = ffint_be_cpu32_ptr(stsz->cnt);

	if (sk == NULL)
		return cnt + 1;

	def_size = ffint_be_cpu32_ptr(stsz->def_size);
	if (def_size != 0) {
		for (i = 0;  i != cnt;  i++) {
			sk[i].size = def_size;
		}

	} else {
		if (sizeof(struct mp4_stsz) + cnt * sizeof(int) > len)
			return -1;

		const int *psize = (int*)stsz->size;
		for (i = 0;  i != cnt;  i++) {
			sk[i].size = ffint_be_cpu32_ptr(&psize[i]);
		}
	}

	return cnt + 1;
}

static inline int mp4_stsz_size(ffuint frames)
{
	return sizeof(struct mp4_stsz) + frames * sizeof(int);
}

static inline int mp4_stsz_add(char *dst, ffuint frsize)
{
	struct mp4_stsz *stsz = (struct mp4_stsz*)dst;
	// ffmem_zero(stsz->def_size, sizeof(stsz->def_size));
	ffuint n = ffint_be_cpu32_ptr(stsz->cnt);
	*(ffuint*)stsz->cnt = ffint_be_cpu32(n + 1);
	*(ffuint*)stsz->size[n] = ffint_be_cpu32(frsize);
	return sizeof(struct mp4_stsz) + (n + 1) * sizeof(int);
}


struct mp4_stco {
	ffbyte cnt[4];
	ffbyte chunkoff[0][4];
};
struct mp4_co64 {
	ffbyte cnt[4];
	ffbyte chunkoff[0][8];
};

/** Set absolute file offset for each chunk.
Return number of chunks;
 <0 on error. */
static inline int mp4_stco_read(const char *data, ffuint len, ffuint type, ffuint64 *chunktab)
{
	const struct mp4_stco *stco = (struct mp4_stco*)data;
	ffuint64 off, lastoff = 0;
	ffuint i, cnt, sz;
	const ffuint *chunkoff = (ffuint*)stco->chunkoff;
	const ffuint64 *chunkoff64 = (ffuint64*)stco->chunkoff;

	sz = (type == BOX_STCO) ? sizeof(int) : sizeof(ffint64);
	cnt = ffint_be_cpu32_ptr(stco->cnt);
	if (len < sizeof(struct mp4_stco) + cnt * sz)
		return -1;

	if (chunktab == NULL)
		return cnt;

	for (i = 0;  i != cnt;  i++) {

		if (type == BOX_STCO)
			off = ffint_be_cpu32_ptr(&chunkoff[i]);
		else
			off = ffint_be_cpu64_ptr(&chunkoff64[i]);

		if (off < lastoff)
			return -1; //offsets must grow

		chunktab[i] = lastoff = off;
	}

	return cnt;
}

static inline int mp4_stco_size(ffuint type, ffuint chunks)
{
	if (type == BOX_STCO)
		return sizeof(struct mp4_stco) + chunks * sizeof(int);
	return sizeof(struct mp4_co64) + chunks * sizeof(ffint64);
}

static inline int mp4_stco_add(void *data, ffuint type, ffuint64 offset)
{
	struct mp4_stco *stco = (struct mp4_stco*)data;
	ffuint n = ffint_be_cpu32_ptr(stco->cnt);
	*(ffuint*)stco->cnt = ffint_be_cpu32(n + 1);

	if (type == BOX_STCO) {
		*(ffuint*)stco->chunkoff[n] = ffint_be_cpu32(offset);
		return sizeof(struct mp4_stco) + (n + 1) * sizeof(int);
	}

	struct mp4_co64 *co64 = (struct mp4_co64*)data;
	*(ffuint64*)co64->chunkoff[n] = ffint_be_cpu64(offset);
	return sizeof(struct mp4_stco) + (n + 1) * sizeof(ffint64);
}


enum MP4_ILST_DATA_TYPE {
	MP4_ILST_IMPLICIT,
	MP4_ILST_UTF8,
	MP4_ILST_JPEG = 13,
	MP4_ILST_PNG,
	MP4_ILST_INT = 21,
};

struct mp4_ilst_data {
	ffbyte unused[3];
	ffbyte type; //enum MP4_ILST_DATA_TYPE
	ffbyte unused2[4];
};

struct mp4_trkn {
	ffbyte unused[2];
	ffbyte num[2];
	ffbyte total[2];
	ffbyte unused2[2];
};

struct mp4_disk {
	ffbyte unused[2];
	ffbyte num[2];
	ffbyte total[2];
};

/** Process "ilst.*.data" box.
Return enum MMTAG;
  0 on error. */
static inline int mp4_ilst_data_read(const char *data, ffuint len, ffuint parent_type, ffstr *tagval, char *tagbuf, ffsize tagbuf_cap)
{
	const struct mp4_ilst_data *d = (struct mp4_ilst_data*)data;
	data += sizeof(struct mp4_ilst_data);
	len -= sizeof(struct mp4_ilst_data);

	switch (parent_type) {

	case MMTAG_TRACKNO: {
		if (sizeof(struct mp4_trkn) > len || d->type != MP4_ILST_IMPLICIT)
			return 0;

		const struct mp4_trkn *trkn = (struct mp4_trkn*)data;
		tagval->ptr = tagbuf;
		tagval->len = ffs_format_r0(tagbuf, tagbuf_cap, "%u/%u"
			, ffint_be_cpu16_ptr(trkn->num), ffint_be_cpu16_ptr(trkn->total));
		return MMTAG_TRACKNO;
	}

	case MMTAG_DISCNUMBER: {
		if (sizeof(struct mp4_disk) > len || d->type != MP4_ILST_IMPLICIT)
			return 0;

		const struct mp4_disk *disk = (struct mp4_disk*)data;
		int num = ffint_be_cpu16_ptr(disk->num);
		int n = ffs_fromint(num, tagbuf, tagbuf_cap, 0);
		ffstr_set(tagval, tagbuf, n);
		return MMTAG_DISCNUMBER;
	}

	case BOX_TAG_GENRE_ID31: {
		if (sizeof(short) > len || !(d->type == MP4_ILST_IMPLICIT || d->type == MP4_ILST_INT))
			return 0;

		ffuint n = ffint_be_cpu16_ptr(data);
		const char *g = (n < FF_COUNT(id3v1_genres)) ? id3v1_genres[n] : "";
		ffstr_setz(tagval, g);
		return MMTAG_GENRE;
	}
	}

	if (d->type != MP4_ILST_UTF8)
		return 0;

	ffstr_set(tagval, data, len);
	return parent_type;
}

static const struct mp4_bbox mp4_ctx_ilst[];

/** Find box by tag ID. */
static inline const struct mp4_bbox* mp4_ilst_find(ffuint mmtag)
{
	const struct mp4_bbox *p = mp4_ctx_ilst;
	for (ffuint i = 0;  ;  i++) {
		if (_BOX_TAG + mmtag == MP4_GET_TYPE(p[i].flags))
			return &p[i];
		if (p[i].flags & MP4_F_LAST)
			break;
	}
	return NULL;
}

static inline int mp4_ilst_data_write(char *data, const ffstr *val)
{
	if (data == NULL)
		return sizeof(struct mp4_ilst_data) + val->len;

	struct mp4_ilst_data *d = (struct mp4_ilst_data*)data;
	ffmem_zero_obj(d);
	d->type = MP4_ILST_UTF8;
	ffmem_copy(d + 1, val->ptr, val->len);
	return sizeof(struct mp4_ilst_data) + val->len;
}

static inline int mp4_ilst_trkn_data_write(char *data, ffuint num, ffuint total)
{
	if (data == NULL)
		return sizeof(struct mp4_ilst_data) + sizeof(struct mp4_trkn);

	struct mp4_ilst_data *d = (struct mp4_ilst_data*)data;
	ffmem_zero_obj(d);
	d->type = MP4_ILST_IMPLICIT;

	struct mp4_trkn *t = (struct mp4_trkn*)(d + 1);
	ffmem_zero_obj(t);
	*(ffushort*)t->num = ffint_be_cpu16(num);
	*(ffushort*)t->total = ffint_be_cpu16(total);
	return sizeof(struct mp4_ilst_data) + sizeof(struct mp4_trkn);
}

static inline int mp4_itunes_smpb_read(const char *data, ffsize len, ffuint *_enc_delay, ffuint *_padding)
{
	ffstr s;
	if (0 == mp4_ilst_data_read(data, len, (ffuint)-1, &s, NULL, 0))
		return 0;

	ffuint tmp, enc_delay, padding;
	ffuint64 samples;
	int r = ffstr_matchfmt(&s, " %8xu %8xu %8xu %16xU"
		, &tmp, &enc_delay, &padding, &samples);
	if (r <= 0)
		return 0;

	*_enc_delay = enc_delay;
	*_padding = padding;
	return r;
}

static inline int mp4_itunes_smpb_write(char *data, ffuint64 total_samples, ffuint enc_delay, ffuint padding)
{
	ffstr s;

	if (data == NULL) {
		s.len = (1 + 8) * 11 + (1 + 16);
		return mp4_ilst_data_write(NULL, &s);
	}

	char buf[255];
	s.ptr = buf;
	s.len = ffs_format(buf, sizeof(buf), " 00000000 %08Xu %08Xu %016XU 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000"
		, enc_delay, padding, total_samples - enc_delay - padding);
	return mp4_ilst_data_write(data, &s);
}

/* Supported box hierarchy:

All boxes are used by ffmp4_write() automatically, unless explicitly marked as (R)ead-only.
'moov' is written after 'mdat' when total samples number isn't known in advance.

ftyp
moov
 mvhd
 trak
  tkhd
  mdia
   mdhd
   hdlr
   minf
    smhd
    dinf
     dref
      url
    stbl
     stsd
      mp4a
       esds
       wave(R)
        esds
      alac(R)
       alac
      avc1(R)
     stts
     stsc
     stsz
     stco | co64(R)
 udta
  meta
   ilst
    [tag_name]
     data
    ----
     mean
     name
     data
mdat
*/

static const struct mp4_bbox
	mp4_ctx_moov[],
	mp4_ctx_trak[],
	mp4_ctx_mdia[],
	mp4_ctx_minf[],
	mp4_ctx_dinf[],
	mp4_ctx_dref[],
	mp4_ctx_stbl[],
	mp4_ctx_stsd[],
	mp4_ctx_alac[],
	mp4_ctx_mp4a[],
	mp4_ctx_mp4a_wave[],
	mp4_ctx_udta[],
	mp4_ctx_meta[],
	mp4_ctx_data[],
	mp4_ctx_itunes[];

const struct mp4_bbox mp4_ctx_global[] = {
	{"ftyp", BOX_FTYP | MP4_PRIO(1) | MP4_MINSIZE(sizeof(struct mp4_ftyp)), NULL},
	{"moov", BOX_MOOV | MP4_PRIO(2), mp4_ctx_moov},
	{"mdat", BOX_MDAT | MP4_PRIO(2) | MP4_F_LAST, NULL},
};
const struct mp4_bbox mp4_ctx_global_stream[] = {
	{"ftyp", BOX_FTYP | MP4_MINSIZE(sizeof(struct mp4_ftyp)), NULL},
	{"mdat", BOX_MDAT, NULL},
	{"moov", BOX_MOOV | MP4_F_LAST, mp4_ctx_moov},
};
static const struct mp4_bbox mp4_ctx_moov[] = {
	{"mvhd", BOX_MVHD | MP4_F_FULLBOX | MP4_F_REQ | MP4_MINSIZE(sizeof(struct mp4_mvhd0)), NULL},
	{"trak", BOX_TRAK | MP4_F_MULTI, mp4_ctx_trak},
	{"udta", BOX_ANY | MP4_F_LAST, mp4_ctx_udta},
};

static const struct mp4_bbox mp4_ctx_trak[] = {
	{"tkhd", BOX_TKHD | MP4_F_FULLBOX | MP4_F_REQ | MP4_MINSIZE(sizeof(struct mp4_tkhd0)), NULL},
	{"mdia", BOX_ANY | MP4_F_LAST, mp4_ctx_mdia},
};
static const struct mp4_bbox mp4_ctx_mdia[] = {
	{"hdlr", BOX_HDLR | MP4_F_FULLBOX | MP4_F_REQ | MP4_MINSIZE(sizeof(struct mp4_hdlr)), NULL},
	{"mdhd", BOX_MDHD | MP4_F_FULLBOX | MP4_F_REQ | MP4_MINSIZE(sizeof(struct mp4_mdhd0)), NULL},
	{"minf", BOX_ANY | MP4_F_LAST, mp4_ctx_minf},
};
static const struct mp4_bbox mp4_ctx_minf[] = {
	{"smhd", BOX_ANY | MP4_F_FULLBOX | MP4_MINSIZE(sizeof(struct mp4_smhd)), NULL},
	{"dinf", BOX_ANY | MP4_F_REQ, mp4_ctx_dinf},
	{"stbl", BOX_ANY | MP4_F_LAST, mp4_ctx_stbl},
};
static const struct mp4_bbox mp4_ctx_dinf[] = {
	{"dref", BOX_DREF | MP4_F_FULLBOX | MP4_F_REQ | MP4_MINSIZE(sizeof(struct mp4_dref)) | MP4_F_LAST, mp4_ctx_dref},
};
static const struct mp4_bbox mp4_ctx_dref[] = {
	{"url ", BOX_DREF_URL | MP4_F_FULLBOX | MP4_F_LAST, NULL},
};
static const struct mp4_bbox mp4_ctx_stbl[] = {
	{"stsd", BOX_STSD | MP4_F_FULLBOX | MP4_F_REQ | MP4_MINSIZE(sizeof(struct mp4_stsd)), mp4_ctx_stsd},
	{"co64", BOX_CO64 | MP4_F_FULLBOX | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_co64)) | MP4_F_RO, NULL},
	{"stco", BOX_STCO | MP4_F_FULLBOX | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_stco)), NULL},
	{"stsc", BOX_STSC | MP4_F_FULLBOX | MP4_F_REQ | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_stsc)), NULL},
	{"stsz", BOX_STSZ | MP4_F_FULLBOX | MP4_F_REQ | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_stsz)), NULL},
	{"stts", BOX_STTS | MP4_F_FULLBOX | MP4_F_REQ | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_stts)) | MP4_F_LAST, NULL},
};
static const struct mp4_bbox mp4_ctx_stsd[] = {
	{"alac", BOX_STSD_ALAC | MP4_MINSIZE(sizeof(struct mp4_afmt)) | MP4_F_RO, mp4_ctx_alac},
	{"avc1", BOX_STSD_AVC1 | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_avc1_read)) | MP4_F_RO, NULL},
	{"mp4a", BOX_STSD_MP4A | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_afmt)) | MP4_F_LAST, mp4_ctx_mp4a},
};
static const struct mp4_bbox mp4_ctx_alac[] = {
	{"alac", BOX_ALAC | MP4_F_FULLBOX | MP4_F_REQ | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_alac)) | MP4_F_LAST, NULL},
};
static const struct mp4_bbox mp4_ctx_mp4a[] = {
	{"esds", BOX_ESDS | MP4_F_FULLBOX | MP4_F_WHOLE, NULL},
	{"wave", BOX_ANY | MP4_F_RO | MP4_F_LAST, mp4_ctx_mp4a_wave},
};
static const struct mp4_bbox mp4_ctx_mp4a_wave[] = {
	{"esds", BOX_ESDS | MP4_F_FULLBOX | MP4_F_WHOLE | MP4_F_LAST, NULL},
};

static const struct mp4_bbox mp4_ctx_udta[] = {
	{"meta", BOX_ANY | MP4_F_FULLBOX | MP4_F_LAST, mp4_ctx_meta},
};
static const struct mp4_bbox mp4_ctx_meta[] = {
	{"ilst", BOX_ANY | MP4_F_LAST, mp4_ctx_ilst},
};
static const struct mp4_bbox mp4_ctx_ilst[] = {
	{"aART",	_BOX_TAG + MMTAG_ALBUMARTIST,	mp4_ctx_data},
	{"covr",	_BOX_TAG,	mp4_ctx_data},
	{"cprt",	_BOX_TAG + MMTAG_COPYRIGHT,	mp4_ctx_data},
	{"desc",	_BOX_TAG,	mp4_ctx_data},
	{"disk",	_BOX_TAG + MMTAG_DISCNUMBER,	mp4_ctx_data},
	{"gnre",	_BOX_TAG + BOX_TAG_GENRE_ID31,	mp4_ctx_data},
	{"trkn",	_BOX_TAG + MMTAG_TRACKNO,	mp4_ctx_data},
	{"\251alb",	_BOX_TAG + MMTAG_ALBUM,	mp4_ctx_data},
	{"\251ART",	_BOX_TAG + MMTAG_ARTIST,	mp4_ctx_data},
	{"\251cmt",	(_BOX_TAG + MMTAG_COMMENT) | MP4_F_MULTI,	mp4_ctx_data},
	{"\251day",	_BOX_TAG + MMTAG_DATE,	mp4_ctx_data},
	{"\251enc",	_BOX_TAG,	mp4_ctx_data},
	{"\251gen",	_BOX_TAG + MMTAG_GENRE,	mp4_ctx_data},
	{"\251lyr",	_BOX_TAG + MMTAG_LYRICS,	mp4_ctx_data},
	{"\251nam",	_BOX_TAG + MMTAG_TITLE,	mp4_ctx_data},
	{"\251too",	_BOX_TAG + MMTAG_VENDOR,	mp4_ctx_data},
	{"\251wrt",	_BOX_TAG + MMTAG_COMPOSER,	mp4_ctx_data},
	{"----",	BOX_ITUNES | MP4_F_MULTI | MP4_F_LAST,	mp4_ctx_itunes},
};

static const struct mp4_bbox mp4_ctx_data[] = {
	{"data", BOX_ILST_DATA | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_ilst_data)) | MP4_F_LAST, NULL},
};

static const struct mp4_bbox mp4_ctx_itunes[] = {
	{"mean", BOX_ITUNES_MEAN | MP4_F_FULLBOX | MP4_F_WHOLE, NULL},
	{"name", BOX_ITUNES_NAME | MP4_F_FULLBOX | MP4_F_WHOLE, NULL},
	{"data", BOX_ITUNES_DATA | MP4_F_WHOLE | MP4_MINSIZE(sizeof(struct mp4_ilst_data)) | MP4_F_LAST, NULL},
};
