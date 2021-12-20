/** avpack: .ape tester
2021, Simon Zolin
*/

#include <avpack/ape-read.h>
#include <test/test.h>

static const char ape_sample[] = {
	"\x4d\x41\x43\x20\x96\x0f\x00\x00\x34\x00\x00\x00\x18\x00\x00\x00"
	"\x74\x1c\x00\x00\x00\x00\x00\x00\xb8\xb2\xcb\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x0a\x5d\x61\xc4\x64\xdc\x94\x0c\x51\x29\xf6\xb0"
	"\x1e\xa0\x8a\x0d\xa0\x0f\x20\x00\x00\x80\x04\x00\x68\xa5\x01\x00"
	"\x1f\x00\x00\x00\x10\x00\x02\x00\x44\xac\x00\x00\xc0\x1c\x00\x00"
	"\x6b\x0a\x06\x00\x95\xb4\x0b\x00\x36\x3b\x11\x00\x40\x01\x18\x00"
	"\x52\xd0\x1e\x00\x55\x3d\x26\x00\xad\xee\x2c\x00\xb6\xde\x33\x00"
	"\xeb\xeb\x3a\x00\x5e\x9e\x41\x00\x4f\x54\x49\x00\xc7\x53\x50\x00"
	"\xb7\x33\x56\x00\x36\x00\x5d\x00\x5b\x88\x63\x00\xab\x51\x6b\x00"
	"\xf1\x14\x72\x00\xa2\xd3\x78\x00\x9a\x35\x80\x00\x8f\x61\x87\x00"
	"\x95\xb2\x8f\x00\xfd\x50\x96\x00\x82\x75\x9c\x00\x10\x2f\xa2\x00"
	"\x0d\x63\xa7\x00\xa2\x66\xae\x00\x44\x03\xb7\x00\x33\x9c\xbd\x00"
	"\xc2\xe4\xc3\x00\x1b\xe9\xc9\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

void test_ape_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	aperead a = {};
	aperead_open(&a, data.len);
	ffuint off = 0;
	ffuint hdr = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = aperead_process(&a, &in, &out);
		// xlog("aperead_process: %d", r);
		switch (r) {
		case APEREAD_HEADER:
			hdr = 1;
			break;
		case APEREAD_DATA:
			xlog("frame: %L @%L", out.len, (ffsize)aperead_cursample(&a));
			break;

		case APEREAD_ID31:
		case APEREAD_APETAG: {
			ffstr name, val;
			ffuint t = aperead_tag(&a, &name, &val);
			xlog("aperead_tag: (%u) %S = %S", (int)t, &name , &val);
			break;
		}

		case APEREAD_SEEK:
			off = aperead_offset(&a);
			// fallthrough
		case APEREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("err: %s", aperead_error(&a));
			x(0);
		}
	}

end:
	x(hdr);
	aperead_close(&a);
}

void test_ape_seek(ffstr data, ffuint delta_msec, int partial)
{
	int r;
	ffstr in = {}, out;
	aperead a = {};
	aperead_open(&a, data.len);
	ffuint seek = 0;
	ffuint off = 0;
	ffuint reqs = 0, fileseek = 0;
	ffuint sample_rate = 44100;

	for (int i = data.len;;  i--) {
		x(i >= 0);

		r = aperead_process(&a, &in, &out);
		// xlog("aperead_process: %d", r);
		switch (r) {
		case APEREAD_DATA:
			x(aperead_cursample(&a) <= seek);
			x(aperead_cursample(&a) + a.info.block_samples > seek);

			seek += delta_msec * sample_rate / 1000;
			reqs++;
			aperead_seek(&a, seek);
			xlog("aperead: seeking to: %u", seek);
			break;
		case APEREAD_SEEK:
			fileseek++;
			off = aperead_offset(&a);
			// fallthrough
		case APEREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case APEREAD_ID31:
		case APEREAD_APETAG:
			break;

		default:
			xlog("error: aperead_process: %s", aperead_error(&a));
			x(0);
		}
	}

end:
	xlog("aperead: seek-reqs:%u  avg-file-seek-reqs:%u"
		, reqs, fileseek/reqs);
	aperead_close(&a);
}

void test_ape()
{
	ffstr data = FFSTR_INITN(ape_sample, sizeof(ape_sample)-1);
	test_ape_read(data, 0);
	test_ape_read(data, 3);

#if 0
	ffstr_null(&data);
	x(0 == file_readall("/tmp/1.ape", &data));
	test_ape_seek(data, 900, 64*1024);
	ffstr_free(&data);
#endif
}
