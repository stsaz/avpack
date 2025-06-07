/** avpack: OGG codec info reader
2025, Simon Zolin */

#include <avpack/flac-ogg-read.h>
#include <avpack/ogg-read.h>
#include <avpack/base/opus.h>
#include <avpack/base/vorbis.h>

struct oggcr {
	oggread o;

	flacoggread flac_ogg;
	ffstr tags;
	unsigned cur_serial, hdr_len, codec, info_pkt;
	char hdr[64];
};

static int oggcr_process(struct oggcr *o, ffstr *in, union avpk_read_result *res)
{
	for (;;) {

		if (o->info_pkt) {
			o->info_pkt = 0;
			ffstr_set(&res->frame, o->hdr, o->hdr_len);
			res->frame.pos = ~0ULL;
			res->frame.end_pos = ~0ULL;
			res->frame.duration = ~0U;
			return AVPK_DATA; // Opus/Vorbis info packet or FLAC stream info
		}

		if (o->tags.len) {
			*(ffstr*)&res->frame = o->tags;
			ffstr_null(&o->tags);
			return AVPK_DATA; // return whole Opus/Vorbis Tags packet
		}

		int r = oggread_process2(&o->o, in, res);
		if (r == OGGREAD_HEADER) {
			if (o->cur_serial != o->o.info.serial) {
				// The first page of a new logical stream
				o->cur_serial = o->o.info.serial;
				o->codec = 0;
				o->hdr_len = 0;
				ffmem_zero_obj(&o->flac_ogg);
			}
			ffstr s = *(ffstr*)&res->frame;

			if (o->codec) {
				switch (o->codec) {
				case AVPKC_FLAC:
					switch ((ffbyte)s.ptr[0]) {
					case 0x84:
						if (s.len < 4) {
							continue; // the block is too small
						}
						r = 4;
						break;

					default:
						continue;
					}
					break;

				case AVPKC_VORBIS:
					if (!(r = vorbis_tags_read(s.ptr, s.len)))
						return AVPK_DATA;
					o->tags = s;
					break;

				case AVPKC_OPUS:
					if (!(r = opus_tags_read(s.ptr, s.len)))
						continue;
					o->tags = s;
					break;
				}

				ffstr_shift((ffstr*)&res->frame, r);
				o->info_pkt = 1;
				return _AVPK_META_BLOCK;
			}

			ffmem_zero_obj(&res->frame);
			res->hdr.duration = o->o.info.total_samples;
			if (ffstr_matchz(&s, "\x01vorbis")) {
				res->hdr.codec = AVPKC_VORBIS;
				unsigned chan, tmp;
				if (!vorbis_info_read(s.ptr, s.len, &chan, &res->hdr.sample_rate, &tmp)) {
					o->o.err = "bad Vorbis header";
					return AVPK_ERROR;
				}
				res->hdr.channels = chan;

			} else if (ffstr_matchz(&s, "OpusHead")) {
				res->hdr.codec = AVPKC_OPUS;
				unsigned chan, tmp;
				if (!opus_hdr_read(s.ptr, s.len, &chan, &tmp)) {
					o->o.err = "bad Opus header";
					return AVPK_ERROR;
				}
				res->hdr.channels = chan;
				res->hdr.sample_rate = 48000;

			} else if (ffstr_matchz(&s, "\x7f""FLAC")) {
				res->hdr.codec = AVPKC_FLAC;
				ffstr s2 = s;
				if (FLACOGGREAD_HEADER != (r = flacoggread_process(&o->flac_ogg, &s2, NULL))) {
					o->o.err = flacoggread_error(&o->flac_ogg);
					return r;
				}
				ffstr_shift(&s, FF_OFF(struct flacogg_hdr, info));
				res->hdr.sample_bits = o->flac_ogg.info.bits;
				res->hdr.sample_rate = o->flac_ogg.info.sample_rate;
				res->hdr.channels = o->flac_ogg.info.channels;

			} else {
				res->error.message = "unrecognized OGG codec";
				res->error.offset = ~0ULL;
				return AVPK_ERROR;
			}

			o->codec = res->hdr.codec;
			if (s.len < sizeof(o->hdr)) {
				memcpy(o->hdr, s.ptr, s.len);
				o->hdr_len = s.len;
			}
		}
		return r;
	}
}

AVPKR_IF_INIT(avpk_ogg, "ogg", AVPKF_OGG, struct oggcr, oggread_open2, oggcr_process, oggread_seek, oggread_close);
