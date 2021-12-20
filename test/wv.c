/** avpack: .wv tester
2021, Simon Zolin
*/

#include <avpack/wv-read.h>
#include <test/test.h>

// 44.1k samples, apetag
static const ffbyte wv_sample[] = {
	"\x77\x76\x70\x6b\x34\x00\x00\x00\x10\x04\x00\x00\x44\xac\x00\x00"
	"\x00\x00\x00\x00\x22\x56\x00\x00\x21\x18\x80\x04\xaf\x42\x77\xf1"
	"\x02\x00\x03\x00\x04\x00\x05\x06\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x8a\x02\x00\x00\xff\xff\x88\x58\x77\x76\x70\x6b"
	"\x34\x00\x00\x00\x10\x04\x00\x00\x00\x00\x00\x00\x22\x56\x00\x00"
	"\x22\x56\x00\x00\x21\x18\x80\x04\xaf\x42\x77\xf1\x02\x00\x03\x00"
	"\x04\x00\x05\x06\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x8a\x02\x00\x00\xff\xff\x88\x58\x41\x50\x45\x54\x41\x47\x45\x58"
	"\xd0\x07\x00\x00\x3d\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\xa0"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x0d\x00\x00\x00\x00\x00\x00\x00"
	"\x65\x6e\x63\x6f\x64\x65\x72\x00\x4c\x61\x76\x66\x35\x38\x2e\x37"
	"\x36\x2e\x31\x30\x30\x41\x50\x45\x54\x41\x47\x45\x58\xd0\x07\x00"
	"\x00\x3d\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x80\x00\x00\x00"
	"\x00\x00\x00\x00\x00"
};

static void wvr_log(void *udata, ffstr msg)
{
	(void)udata;
	xlog("%S", &msg);
}

void test_wv_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	wvread w = {};
	w.log = wvr_log;
	wvread_open(&w, data.len);
	ffuint off = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = wvread_process(&w, &in, &out);
		// xlog("wvread_process: %d", r);
		switch (r) {
		case WVREAD_DATA: {
			const struct wvread_info *wi = wvread_info(&w);
			x(wi->total_samples == 44100);
			xlog("frame: %L @%L", out.len, (ffsize)wvread_cursample(&w));
			break;
		}

		case WVREAD_ID31:
		case WVREAD_APETAG: {
			ffstr name, val;
			ffuint t = wvread_tag(&w, &name, &val);
			xlog("wvread_tag: (%u) %S = %S", (int)t, &name , &val);
			break;
		}

		case WVREAD_SEEK:
			off = wvread_offset(&w);
			// fallthrough
		case WVREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("err: %s", wvread_error(&w));
			x(0);
		}
	}

end:
	wvread_close(&w);
}

void test_wv_seek(ffstr data, ffuint delta_msec, int partial)
{
	int r;
	ffstr in = {}, out;
	wvread w = {};
	w.log = wvr_log;
	wvread_open(&w, data.len);
	ffuint seek = 0;
	ffuint off = 0;
	const struct wvread_info *info;
	ffuint reqs = 0, fileseek = 0;
	ffuint sample_rate = 44100;

	for (int i = data.len;;  i--) {
		x(i >= 0);

		r = wvread_process(&w, &in, &out);
		// xlog("wvread_process: %d", r);
		switch (r) {
		case WVREAD_DATA:
			info = wvread_info(&w);
			x(wvread_cursample(&w) <= seek);
			x(wvread_cursample(&w) + w.block_samples > seek);

			seek += delta_msec * sample_rate / 1000;
			if (seek > info->total_samples)
				goto end;
			reqs++;
			wvread_seek(&w, seek);
			xlog("wvread: seeking to: %u", seek);
			break;
		case WVREAD_SEEK:
			fileseek++;
			off = wvread_offset(&w);
			// fallthrough
		case WVREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case WVREAD_ID31:
		case WVREAD_APETAG:
			break;

		default:
			xlog("error: wvread_process: %s", wvread_error(&w));
			x(0);
		}
	}

end:
	xlog("wvread: seek-reqs:%u  avg-file-seek-reqs:%u"
		, reqs, fileseek/reqs);
	wvread_close(&w);
}

void test_wv()
{
	ffstr data = FFSTR_INITN(wv_sample, sizeof(wv_sample)-1);
	test_wv_read(data, 0);
	test_wv_read(data, 3);

#if 0
	ffstr_null(&data);
	file_readall("/tmp/1.wv", &data);
	test_wv_seek(data, 900, 64*1024);
	ffstr_free(&data);
#endif
}
