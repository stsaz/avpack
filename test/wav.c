/** avpack: .wav tester
2021, Simon Zolin
*/

#include <avpack/wav-read.h>
#include <avpack/wav-write.h>
#include <test/test.h>
#include <ffbase/mem-print.h>
extern int Verbose;

ffvec test_wav_write()
{
	ffvec v = {};
	wavwrite w = {};
	int r;
	ffuint off = 0;
	ffstr in, out;
	struct wav_info info = {};
	info.format = WAV_16;
	info.channels = 2;
	info.sample_rate = 48000;
	x(0 == wavwrite_create(&w, &info));

	ffstr_setz(&in, "1234");

	for (;;) {
		r = wavwrite_process(&w, &in, &out);
		// xlog("wavwrite_process: %d", r);
		switch (r) {
		case WAVWRITE_HEADER:
		case WAVWRITE_DATA:
			ffvec_grow(&v, out.len, 1);
			ffmem_copy(v.ptr + off, out.ptr, out.len);
			off += out.len;
			v.len = ffmax(v.len, off);
			break;
		case WAVWRITE_SEEK:
			off = wavwrite_offset(&w);
			break;
		case WAVWRITE_DONE:
			x(in.len == 0);
			goto end;
		case WAVWRITE_MORE:
			wavwrite_finish(&w);
			break;
		default:
			xlog("err: %s", wavwrite_error(&w));
			x(0);
		}
	}

end:
	wavwrite_close(&w);
	return v;
}

static void wavr_log(void *udata, const char *fmt, va_list va)
{
	(void)udata;
	xlogv(fmt, va);
}

void test_wav_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	wavread w = {};
	w.log = wavr_log;
	wavread_open(&w);
	ffuint off = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = wavread_process(&w, &in, &out);
		// xlog("wavread_process: %d", r);
		switch (r) {
		case WAVREAD_HEADER: {
			const struct wav_info *wi = wavread_info(&w);
			x(wi->format == WAV_16);
			x(wi->channels == 2);
			x(wi->sample_rate == 48000);
			goto next;
		}

		case WAVREAD_TAG: {
			ffstr val;
			ffuint t = wavread_tag(&w, &val);
			xlog("wavread_tag: %d = %S", t, &val);
			break;
		}

		case WAVREAD_SEEK:
			off = wavread_offset(&w);
			// fallthrough
		case WAVREAD_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("err: %s", wavread_error(&w));
			x(0);
		}
	}

next:
	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = wavread_process(&w, &in, &out);
		// xlog("wavread_process: %d", r);
		switch (r) {
		case WAVREAD_DATA:
			x(0 == wavread_cursample(&w));
			xseq(&out, "1234");
			break;

		case WAVREAD_DONE:
			goto end;

		case WAVREAD_SEEK:
			off = wavread_offset(&w);
			// fallthrough
		case WAVREAD_MORE:
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("err: %s", wavread_error(&w));
			x(0);
		}
	}

	//wavread_seek

end:
	wavread_close(&w);
}

void test_wav()
{
	ffvec buf = {};
	buf = test_wav_write();

	if (Verbose) {
		ffstr s = ffmem_print(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
		xlog("%S", &s);
		ffstr_free(&s);
	}

	ffstr data = FFSTR_INITSTR(&buf);
	test_wav_read(data, 0);
	test_wav_read(data, 3);

	ffvec_free(&buf);
}
