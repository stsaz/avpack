/** avpack: .mpeg tester
2021, Simon Zolin
*/

#include <avpack/mp3-read.h>
#include <avpack/mp3-write.h>
#include <test/test.h>
#include <ffbase/mem-print.h>
extern int Verbose;

const char mpeg_frame[422] = {"skip\xff\xfb\x92\x64" "mpegframe1"};
const char mpeg_sample[] = {
"junk"
"\xff\xfb\x92\x64" "mpegframe1    "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"\xff\x00" // bad header
"\xff\xfb\x92\x64" "mpegframe2    "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"\xff\xfb\x92\x64" "mpegframe3    "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
"                                                  "
};

struct tag {
	ffuint name;
	const char *val;
};
static const struct tag tags[] = {
	{ MMTAG_ALBUM, "album" },
	{ MMTAG_ARTIST, "artist" },
	{ MMTAG_DATE, "date" },
	{ MMTAG_TITLE, "title" },
	{ MMTAG_TRACKNO, "01" },
	{ MMTAG_TRACKTOTAL, "09" },
};

ffvec test_mp3_write()
{
	ffvec v = {};
	mp3write m = {};
	int r;
	ffuint off = 0;
	ffuint flags = 0;
	ffstr in, out;
	mp3write_create(&m);
	m.id3v2_min_size = 200;

	const struct tag *tag;
	FFARRAY_FOREACH(tags, tag) {
		ffstr val = FFSTR_INITZ(tag->val);
		x(0 == mp3write_addtag(&m, tag->name, val));
	}

	ffstr_set(&in, mpeg_frame, sizeof(mpeg_frame));

	for (;;) {
		r = mp3write_process(&m, &in, &out, flags);
		// printf("mp3write_process: %d\n", r);
		switch (r) {
		case MP3WRITE_DATA:
			ffvec_grow(&v, out.len, 1);
			ffmem_copy(v.ptr + off, out.ptr, out.len);
			off += out.len;
			v.len = ffmax(v.len, off);
			break;
		case MP3WRITE_SEEK:
			off = mp3write_offset(&m);
			break;
		case MP3WRITE_DONE:
			x(in.len == 0);
			goto end;
		case MP3WRITE_MORE:
			flags = MP3WRITE_FLAST;
			break;
		default:
			x(0);
		}
	}

end:
	mp3write_close(&m);
	return v;
}

// static void mpegr_log(void *udata, ffstr msg)
// {
// 	(void)udata;
// 	printf("%.*s\n", (int)msg.len, msg.ptr);
// }

void test_mp3_read(ffstr data)
{
	int r;
	ffstr in = {}, out;
	mp3read m = {};
	// m.log = mpegr_log;
	mp3read_open(&m, data.len);
	ffuint off = 1;
	const struct mpeg1read_info *info;
	int frno = 0;
	int partial = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = mp3read_process(&m, &in, &out);
		// printf("mpegread_process: %d\n", r);
		switch (r) {

		case MP3READ_APETAG:
		case MP3READ_ID31:
		case MP3READ_ID32: {
			ffstr name, val;
			ffuint t = mp3read_tag(&m, &name, &val);
			int k = 0;
			const struct tag *tag;
			FFARRAY_FOREACH(tags, tag) {
				if (tag->name == t) {
					xseq(&val, tag->val);
					k = 1;
					break;
				}
			}
			printf("mp3read_tag: %u = %.*s\n"
				, (int)t
				, (int)val.len, val.ptr);
			x(k == 1);
			break;
		}

		case MPEG1READ_HEADER:
			info = mp3read_info(&m);
			xieq(44100, info->sample_rate);
			xieq(2, info->channels);
			xieq(128000, info->bitrate);
			x(info->total_samples != 0);
			break;

		case MPEG1READ_DATA:
			printf("frame#%d:%u\n", frno++, (int)out.len);
			break;

		case MPEG1READ_SEEK:
			off = mp3read_offset(&m);
			// fallthrough
		case MPEG1READ_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, 1);
			off += in.len;
			break;

		case MP3READ_DONE:
			goto end;

		default:
			printf("error: mpegread_process: %s\n", mp3read_error(&m));
			x(0);
		}
	}

end:
	mp3read_close(&m);
}

void test_mpeg_seek(ffstr data, ffuint delta)
{
	int r;
	ffstr in, out;
	mpeg1read m = {};
	// m.log = mpegr_log;
	mpeg1read_open(&m, data.len);
	ffuint seek = 0;
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;
	const struct mpeg1read_info *info;

	for (;;) {
		r = mpeg1read_process(&m, &in, &out);
		// printf("mpegread_process: %d\n", r);
		switch (r) {
		case MPEG1READ_HEADER:
			info = mpeg1read_info(&m);
			break;
		case MPEG1READ_DATA:
			x(mpeg1read_cursample(&m) <= seek);

			seek += delta;
			if (seek > info->total_samples)
				goto end;
			mpeg1read_seek(&m, seek);
			printf("\nmpegread: seeking to: %u\n", seek);
			break;
		case MPEG1READ_SEEK:
			off = mpeg1read_offset(&m);
			// fallthrough
		case MPEG1READ_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		case MP3READ_DONE:
			goto end;

		default:
			printf("error: mpegread_process: %s\n", mpeg1read_error(&m));
			x(0);
		}
	}

end:
	mpeg1read_close(&m);
}

void test_apetag()
{
	static const char apetag[] = {
	"\x00\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x41\x50\x45\x54\x41\x47\x45\x58\xd0\x07\x00\x00\x8e\x00\x00\x00"
	"\x03\x00\x00\x00\x00\x00\x00\xa0\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x07\x00\x00\x00\x00\x00\x00\x00\x4d\x50\x33\x47\x41\x49\x4e\x5f"
	"\x4d\x49\x4e\x4d\x41\x58\x00\x30\x38\x33\x2c\x32\x31\x30\x0c\x00"
	"\x00\x00\x00\x00\x00\x00\x52\x45\x50\x4c\x41\x59\x47\x41\x49\x4e"
	"\x5f\x54\x52\x41\x43\x4b\x5f\x47\x41\x49\x4e\x00\x2b\x30\x2e\x30"
	"\x36\x30\x30\x30\x30\x20\x64\x42\x08\x00\x00\x00\x00\x00\x00\x00"
	"\x52\x45\x50\x4c\x41\x59\x47\x41\x49\x4e\x5f\x54\x52\x41\x43\x4b"
	"\x5f\x50\x45\x41\x4b\x00\x30\x2e\x39\x32\x33\x36\x39\x37\x41\x50"
	"\x45\x54\x41\x47\x45\x58\xd0\x07\x00\x00\x8e\x00\x00\x00\x03\x00"
	"\x00\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00\x00\x00"
	};

	ffstr in;
	ffstr_set(&in, apetag, sizeof(apetag)-1);
	int r;
	struct apetagread a = {};
	apetagread_open(&a);

	r = apetagread_footer(&a, in);
	xieq(r, APETAGREAD_SEEK);
	r = apetagread_offset(&a);
	xieq(r, -(sizeof(apetag)-1 - 16));
	in.ptr = (char*)apetag + sizeof(apetag)-1 - (-r);
	in.len = -r;

	struct tag {
		const char *name, *val;
	};
	static const struct tag tags[] = {
		{"MP3GAIN_MINMAX", "083,210"},
		{"REPLAYGAIN_TRACK_GAIN", "+0.060000 dB"},
		{"REPLAYGAIN_TRACK_PEAK", "0.923697"},
	};

	for (;;) {
		ffstr name, val;
		r = apetagread_process(&a, &in, &name, &val);
		switch (r) {
		case APETAGREAD_DONE:
			goto done;
		default: {
			x(r <= 0);
			const struct tag *tag;
			int k = 0;
			FFARRAY_FOREACH(tags, tag) {
				if (ffstr_eqz(&name, tag->name)) {
					xseq(&val, tag->val);
					k = 1;
					break;
				}
			}
			x(k == 1);
		}
		}
	}

done:
	apetagread_close(&a);
}

void test_mp3()
{
	test_apetag();

	ffvec buf = {};
	buf = test_mp3_write();

	if (Verbose) {
		ffstr s = ffmem_print(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
		printf("%.*s\n", (int)s.len, s.ptr);
		ffstr_free(&s);
	}

	ffstr data = FFSTR_INITSTR(&buf);
	test_mp3_read(data);

	ffstr_set(&data, mpeg_sample, sizeof(mpeg_sample)-1);
	test_mp3_read(data);

#if 0
	data.ptr = NULL;
	file_readall("/tmp/1.mpeg", &data);
	test_mpeg_seek(data, 19*48000);
#endif

	ffvec_free(&buf);
}
