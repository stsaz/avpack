/** avpack: test reader.h interface
2025, Simon Zolin */

#include <avpack/reader.h>
#include <test/test.h>
#include <avpack/aac-read.h>
#include <avpack/ape-read.h>
#include <avpack/avi-read.h>
#include <avpack/caf-read.h>
#include <avpack/flac-read.h>
#include <avpack/mkv-read.h>
#include <avpack/mp3-read.h>
#include <avpack/mp4-read.h>
#include <avpack/mpc-read.h>
#include <avpack/ogg-codec-read.h>
#include <avpack/ts-read.h>
#include <avpack/wav-read.h>
#include <avpack/wv-read.h>

static const char ret_str[][14] = {
	"AVPK_HEADER",
	"AVPK_META",
	"AVPK_DATA",
	"AVPK_SEEK",
	"AVPK_MORE",
	"AVPK_FIN",
	"AVPK_ERROR",
	"AVPK_WARNING",
};

static const struct avpkr_if *const avpk_formats[] = {
	&avpk_aac,
	&avpk_ape,
	&avpk_avi,
	&avpk_caf,
	&avpk_flac,
	&avpk_mkv,
	&avpk_mp3,
	&avpk_mp4,
	&avpk_mpc,
	&avpk_ogg,
	&avpk_ts,
	&avpk_wav,
	&avpk_wv,
};

int test_reader(ffstr data, const char *ext)
{
	xlog("TEST  data:%L  %s", data.len, ext);

	int ok = 0;
	struct avpk_reader_conf c = {
		.total_size = data.len,
	};
	avpk_reader ar = {};
	if (avpk_open(&ar, avpk_reader_find(ext, avpk_formats, FF_COUNT(avpk_formats)), &c)) {
		xlog("ERROR  avpk_open()");
		goto fin;
	}
	ffuint64 off = 0;
	ffstr in = {};
	int r, iframe = 0;
	for (;;) {
		union avpk_read_result res = {};
		r = avpk_read(&ar, &in, &res);
		xlog("%s", ret_str[r]);
		switch (r) {
		case AVPK_HEADER:
			xlog("codec:%u  %u/%u  %U"
				, res.hdr.codec, res.hdr.sample_rate, res.hdr.channels, res.hdr.duration);
			if (res.hdr.sample_bits
				&& ar.ifa.format != AVPKF_MKV)
				xieq(res.hdr.sample_bits, 16);
			xieq(res.hdr.sample_rate, 48000);
			xieq(res.hdr.channels, 2);
			break;

		case AVPK_META:
			xlog("tag  %u  %S = %S", res.tag.id, &res.tag.name, &res.tag.value);
			if (res.tag.id == MMTAG_ARTIST)
				xseq(&res.tag.value, "A");
			else if (res.tag.id == MMTAG_TITLE)
				xseq(&res.tag.value, "T");
			break;

		case AVPK_DATA:
			xlog("frame #%u  output:%L  %u @%U"
				, iframe++, res.frame.len, res.frame.duration, res.frame.pos);
			break;

		case AVPK_SEEK:
			off = res.seek_offset;
			x(off < data.len);
			// fallthrough
		case AVPK_MORE:
			if (off == data.len) {
				ok = 1;
				goto fin;
			}
			in.ptr = data.ptr + off;
			in.len = ffmin(data.len - off, 128); // force partial input
			off += in.len;
			break;

		case AVPK_FIN:
			ok = 1;
			goto fin;

		case AVPK_WARNING:
			xlog("WARNING  %s", res.error);
			break;
		case AVPK_ERROR:
			xlog("ERROR  %s", res.error);
			x(0);
			goto fin;
		}
	}

fin:
	avpk_close(&ar);
	return ok;
}
