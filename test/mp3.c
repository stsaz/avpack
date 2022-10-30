/** avpack: .mp3/MPEG tester
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
		// xlog("mp3write_process: %d", r);
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

// static void mpegr_log(void *udata, const char *fmt, va_list va)
// {
// 	(void)udata;
// 	xlogv(fmt, va);
// }

void test_mp3_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	mp3read m = {};
	// m.log = mpegr_log;
	mp3read_open(&m, data.len);
	ffuint off = 0;
	const struct mpeg1read_info *info;
	int frno = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = mp3read_process(&m, &in, &out);
		// xlog("mpegread_process: %d", r);
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
			xlog("mp3read_tag: (%u) %S = %S", (int)t, &name, &val);
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
			xlog("frame#%d:%u", frno++, (int)out.len);
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
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case MP3READ_DONE:
			goto end;

		default:
			xlog("error: mpegread_process: %s", mp3read_error(&m));
			x(0);
		}
	}

end:
	mp3read_close(&m);
}

void test_mpeg_seek(ffstr data, ffuint delta, int partial)
{
	int r;
	ffstr in = {}, out;
	mpeg1read m = {};
	// m.log = mpegr_log;
	mpeg1read_open(&m, data.len);
	ffuint seek = 0;
	ffuint off = 0;
	ffuint reqs = 0, fileseek = 0;
	const struct mpeg1read_info *info;

	for (;;) {
		r = mpeg1read_process(&m, &in, &out);
		// xlog("mpegread_process: %d", r);
		switch (r) {
		case MPEG1READ_HEADER:
			info = mpeg1read_info(&m);
			break;
		case MPEG1READ_DATA:
			x(mpeg1read_cursample(&m) <= seek);

			seek += delta;
			if (seek > info->total_samples)
				goto end;
			reqs++;
			mpeg1read_seek(&m, seek);
			xlog("mpegread: seeking to: %u", seek);
			break;
		case MPEG1READ_SEEK:
			fileseek++;
			off = mpeg1read_offset(&m);
			if (off > data.len)
				goto end;
			// fallthrough
		case MPEG1READ_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			if (in.len == 0)
				goto end;
			break;

		case MP3READ_DONE:
			goto end;

		default:
			xlog("error: mpegread_process: %s", mpeg1read_error(&m));
			x(0);
		}
	}

end:
	xlog("mpegread: seek-reqs:%u  avg-file-seek-reqs:%u"
		, reqs, fileseek/reqs);
	mpeg1read_close(&m);
}

void test_mp3()
{
	ffvec buf = {};
	buf = test_mp3_write();

	if (Verbose) {
		ffstr s = ffmem_alprint(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
		xlog("%S", &s);
		ffstr_free(&s);
	}

	ffstr data = FFSTR_INITSTR(&buf);
	test_mp3_read(data, 0);
	test_mp3_read(data, 3);

	ffstr_set(&data, mpeg_sample, sizeof(mpeg_sample)-1);
#if 0
	data.ptr = NULL;
	file_readall("/tmp/1.mpeg", &data);
#endif
	test_mp3_read(data, 0);

#if 0
	data.ptr = NULL;
	file_readall("/tmp/1.mpeg", &data);
	test_mpeg_seek(data, 19*48000);
#endif

	ffvec_free(&buf);
}
