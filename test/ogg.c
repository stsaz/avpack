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
		// printf("oggwrite_process: %d\n", r);
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

static void oggr_log(void *udata, ffstr msg)
{
	(void)udata;
	printf("%.*s\n", (int)msg.len, msg.ptr);
}

void test_ogg_read(ffstr data)
{
	int r;
	ffstr in, out;
	oggread o = {};
	o.log = oggr_log;
	oggread_open(&o, data.len);
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;
	const struct oggread_info *info;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = oggread_process(&o, &in, &out);
		// printf("oggread_process: %d\n", r);
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
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		case OGGREAD_DONE:
			goto end;

		default:
			printf("error: oggread_process: %s\n", oggread_error(&o));
			x(0);
		}
	}

end:
	oggread_close(&o);
}

void test_ogg_seek(ffstr data, ffuint delta)
{
	int r;
	ffstr in, out;
	oggread o = {};
	o.log = oggr_log;
	oggread_open(&o, data.len);
	ffuint seek = 0;
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;
	const struct oggread_info *info;
	ffuint reqs = 0, fileseek = 0;

	for (;;) {
		r = oggread_process(&o, &in, &out);
		// printf("oggread_process: %d\n", r);
		switch (r) {
		case OGGREAD_HEADER:
			info = oggread_info(&o);
			break;
		case OGGREAD_DATA:
			x(oggread_page_pos(&o) <= seek);
			x(o.page_endpos > seek);

			seek += delta;
			if (seek > info->total_samples)
				goto end;
			reqs++;
			oggread_seek(&o, seek);
			printf("\noggread: seeking to: %u\n", seek);
			break;
		case OGGREAD_SEEK:
			fileseek++;
			off = oggread_offset(&o);
			// fallthrough
		case OGGREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		case OGGREAD_DONE:
			goto end;

		default:
			printf("error: oggread_process: %s\n", oggread_error(&o));
			x(0);
		}
	}

end:
	printf("oggread: seek-reqs:%u  avg-file-seek-reqs:%u\n"
		, reqs, fileseek/reqs);
	oggread_close(&o);
}

void test_ogg()
{
	ffvec buf = {};
	buf = test_ogg_write();

	if (Verbose) {
		ffstr s = ffmem_print(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
		printf("%.*s\n", (int)s.len, s.ptr);
		ffstr_free(&s);
	}

	ffstr data = FFSTR_INITSTR(&buf);
	test_ogg_read(data);

#if 0
	data.ptr = NULL;
	file_readall("/tmp/1.ogg", &data);
	test_ogg_seek(data, 19*48000);
#endif

	ffvec_free(&buf);
}
