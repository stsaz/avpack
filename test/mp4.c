/** avpack: .mp4 tester
2021, Simon Zolin
*/

#include <avpack/mp4-read.h>
#include <avpack/mp4-write.h>
#include <test/test.h>

struct tag {
	ffuint name;
	const char *val;
};
static const struct tag tags[] = {
	{ MMTAG_ALBUM, "album" },
	{ MMTAG_ARTIST, "artist" },
	{ MMTAG_DATE, "date" },
	{ MMTAG_TITLE, "title" },
	{ MMTAG_TRACKNO, "1" },
};

ffvec test_mp4_write()
{
	ffvec v = {};
	mp4write m = {};
	int r;
	ffuint off = 0;
	ffstr in, out;
	struct mp4_info info = {};
	info.fmt.bits = 16;
	info.fmt.channels = 2;
	info.fmt.rate = 48000;
	info.frame_samples = 1024;
	ffstr_setz(&info.conf, "aacconf");
	x(0 == mp4write_create_aac(&m, &info));

	const struct tag *tag;
	FFARRAY_FOREACH(tags, tag) {
		ffstr val = FFSTR_INITZ(tag->val);
		x(0 == mp4write_addtag(&m, tag->name, val));
	}

	ffstr_setz(&in, "aacframe1");

	for (;;) {
		r = mp4write_process(&m, &in, &out);
		// xlog("mp4write_process: %d", r);
		switch (r) {
		case MP4WRITE_DATA:
			ffvec_grow(&v, out.len, 1);
			ffmem_copy(v.ptr + off, out.ptr, out.len);
			off += out.len;
			v.len = ffmax(v.len, off);
			break;
		case MP4WRITE_SEEK:
			off = mp4write_offset(&m);
			break;
		case MP4WRITE_DONE:
			x(in.len == 0);
			goto end;
		case MP4WRITE_MORE:
			mp4write_finish(&m);
			break;
		default:
			xlog("err: %s", mp4write_error(&m));
			x(0);
		}
	}

end:
	mp4write_close(&m);
	return v;
}

static void mp4r_log(void *udata, const char *fmt, va_list va)
{
	(void)udata;
	xlogv(fmt, va);
}

void test_mp4_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	mp4read m = {};
	m.log = mp4r_log;
	mp4read_open(&m);
	ffuint off = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = mp4read_process(&m, &in, &out);
		// xlog("mp4read_process: %d", r);
		switch (r) {
		case MP4READ_HEADER: {
			const struct mp4read_audio_info *ti = mp4read_track_info(&m, 0);
			x(NULL == mp4read_track_info(&m, 1));
			x(ti->type == 1);
			x(ti->codec == MP4_A_AAC);
			x(ti->format.bits == 16);
			x(ti->format.channels == 2);
			x(ti->format.rate == 48000);
			goto next;
		}

		case MP4READ_TAG: {
			ffstr val;
			ffuint t = mp4read_tag(&m, &val);
			int k = 0;
			const struct tag *tag;
			FFARRAY_FOREACH(tags, tag) {
				if (tag->name == t) {
					xseq(&val, tag->val);
					k = 1;
					break;
				}
			}
			x(k == 1);
			break;
		}

		case MP4READ_SEEK:
			off = mp4read_offset(&m);
			// fallthrough
		case MP4READ_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			x(0);
		}
	}

next:
	mp4read_track_activate(&m, 0);
	for (;;) {
		r = mp4read_process(&m, &in, &out);
		// xlog("mp4read_process: %d", r);
		switch (r) {
		case MP4READ_DATA:
			x(0 == mp4read_cursample(&m));
			xseq(&out, "aacframe1");
			break;

		case MP4READ_DONE:
			goto end;

		case MP4READ_SEEK:
			off = mp4read_offset(&m);
			// fallthrough
		case MP4READ_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			// mp4read_error
			x(0);
		}
	}

	//mp4read_seek

end:
	mp4read_close(&m);
}

void test_mp4()
{
	ffvec buf = {};
	buf = test_mp4_write();

	ffstr data = FFSTR_INITSTR(&buf);
	test_mp4_read(data, 0);
	test_mp4_read(data, 3);

	ffvec_free(&buf);
}
