/** avpack: .ogg tester
2021, Simon Zolin
*/

#include <avpack/ogg-read.h>
#include <avpack/ogg-write.h>
#include <test/test.h>
#include <ffbase/mem-print.h>
extern int Verbose;

ffvec test_ogg_write()
{
	ffvec v = {};
	oggwrite o = {};
	int r;
	ffuint off = 0;
	ffuint flags = 0;
	ffstr in, out;
	ffuint64 endpos = (ffuint64)-1;
	x(0 == oggwrite_create(&o, 123, 0));

	endpos = 1024;
	ffstr_setz(&in, "oggframe1");

	for (;;) {
		r = oggwrite_process(&o, &in, &out, endpos, flags);
		// xlog("oggwrite_process: %d", r);
		switch (r) {
		case OGGWRITE_DATA:
			ffvec_grow(&v, out.len, 1);
			ffmem_copy(v.ptr + off, out.ptr, out.len);
			off += out.len;
			v.len = ffmax(v.len, off);
			break;
		case OGGWRITE_DONE:
			x(in.len == 0);
			goto end;
		case OGGWRITE_MORE:
			flags = OGGWRITE_FLAST;
			break;
		default:
			x(0);
		}
	}

end:
	oggwrite_close(&o);
	return v;
}

static void oggr_log(void *udata, const char *fmt, va_list va)
{
	(void)udata;
	xlogv(fmt, va);
}

void test_ogg_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	oggread o = {};
	o.log = oggr_log;
	oggread_open(&o, data.len);
	ffuint off = 0;
	const struct oggread_info *info;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = oggread_process(&o, &in, &out);
		// xlog("oggread_process: %d", r);
		switch (r) {

		case OGGREAD_HEADER:
			info = oggread_info(&o);
			x(info->total_samples != 0);
			break;
		case OGGREAD_DATA:
			xseq(&out, "oggframe1");
			break;

		case OGGREAD_SEEK:
			off = oggread_offset(&o);
			// fallthrough
		case OGGREAD_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case OGGREAD_DONE:
			goto end;

		default:
			xlog("error: oggread_process: %s", oggread_error(&o));
			x(0);
		}
	}

end:
	oggread_close(&o);
}

void test_ogg_seek(ffstr data, ffuint delta_msec, int partial)
{
	int r;
	ffstr in = {}, out;
	oggread o = {};
	o.log = oggr_log;
	oggread_open(&o, data.len);
	ffuint seek = 0;
	ffuint off = 0;
	const struct oggread_info *info;
	ffuint reqs = 0, fileseek = 0;
	ffuint sample_rate = 44100;

	for (int i = data.len;;  i--) {
		x(i >= 0);

		r = oggread_process(&o, &in, &out);
		// xlog("oggread_process: %d", r);
		switch (r) {
		case OGGREAD_HEADER:
			info = oggread_info(&o);
			break;
		case OGGREAD_DATA:
			x(oggread_page_pos(&o) <= seek);
			x(o.page_endpos > seek);

			seek += delta_msec * sample_rate / 1000;
			if (seek > info->total_samples)
				goto end;
			reqs++;
			oggread_seek(&o, seek);
			xlog("oggread: seeking to: %u", seek);
			break;
		case OGGREAD_SEEK:
			fileseek++;
			off = oggread_offset(&o);
			// fallthrough
		case OGGREAD_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case OGGREAD_DONE:
			goto end;

		default:
			xlog("error: oggread_process: %s", oggread_error(&o));
			x(0);
		}
	}

end:
	xlog("oggread: seek-reqs:%u  avg-file-seek-reqs:%u"
		, reqs, fileseek/reqs);
	oggread_close(&o);
}

void test_ogg()
{
	ffvec buf = {};
	buf = test_ogg_write();

	if (Verbose) {
		ffstr s = ffmem_print(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
		xlog("%S", &s);
		ffstr_free(&s);
	}

	ffstr data = FFSTR_INITSTR(&buf);
	test_ogg_read(data, 0);
	test_ogg_read(data, 3);

#if 0
	ffstr_null(&data);
	file_readall("/tmp/1.ogg", &data);
	test_ogg_seek(data, 900, 64*1024);
	ffstr_free(&data);
#endif

	ffvec_free(&buf);
}
