/** avpack: test writer.h interface
2025, Simon Zolin */

#include <avpack/writer.h>
#include <avpack/mmtag.h>
#include <avpack/flac-write.h>
#include <avpack/mp3-write.h>
#include <avpack/mp4-write.h>
#include <avpack/ogg-write.h>
#include <avpack/wav-write.h>
#include <test/test.h>


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

static const struct avpkw_if *const avpkw_formats[] = {
	&avpkw_flac,
	&avpkw_mp3,
	&avpkw_mp4,
	&avpkw_ogg,
	&avpkw_wav,
};

static void test_writer_ext(ffstr *buf, const char *ext)
{
	xlog("TEST %s", ext);

	unsigned off = 0;
	avpk_writer w = {};
	struct avpk_writer_conf ac = {
		.info = {
			.duration = 96000,
			.sample_rate = 48000,
			.sample_bits = 16,
			.channels = 2,
		},
	};
	x(!avpk_create(&w, avpk_writer_find(ext, avpkw_formats, FF_COUNT(avpkw_formats)), &ac));

	avpk_tag(&w, MMTAG_VENDOR, FFSTR_Z(""), FFSTR_Z("V"));
	avpk_tag(&w, MMTAG_ARTIST, FFSTR_Z(""), FFSTR_Z("A"));
	avpk_tag(&w, MMTAG_TITLE, FFSTR_Z(""), FFSTR_Z("T"));

	struct avpk_frame in = {
		.len = 7,
		.ptr = "packet1",
	};
	unsigned flags = AVPKW_F_LAST;
	for (;;) {
		union avpk_write_result res = {};
		int r = avpk_write(&w, &in, flags, &res);
		xlog("%s", ret_str[r]);
		switch (r) {
		case AVPK_DATA:
			memcpy(buf->ptr + off, res.packet.ptr, res.packet.len);
			off += res.packet.len;
			buf->len = ffmax(buf->len, off);
			break;
		case AVPK_SEEK:
			off = res.seek_offset;
			x(off < 64*1024);
			continue;
		case AVPK_MORE:
			break;
		case AVPK_FIN:
			goto fin;
		case AVPK_ERROR:
			xlog("ERROR  %s", res.error.message);
			x(0);
			goto fin;
		}
	}

fin:
	avpk_writer_close(&w);
}

void test_writer()
{
	char data[64*1024];
	ffstr buf = { 0, data };

	test_writer_ext(&buf, "flac");
	file_writeall("avpk-test.flac", buf.ptr, buf.len);

	test_writer_ext(&buf, "mp3");
	file_writeall("avpk-test.mp3", buf.ptr, buf.len);

	test_writer_ext(&buf, "mp4");
	file_writeall("avpk-test.mp4", buf.ptr, buf.len);

	test_writer_ext(&buf, "ogg");
	file_writeall("avpk-test.ogg", buf.ptr, buf.len);

	test_writer_ext(&buf, "wav");
	file_writeall("avpk-test.wav", buf.ptr, buf.len);
}
