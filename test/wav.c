/** avpack: .wav tester
2021, Simon Zolin
*/

#include <avpack/wav-read.h>
#include <avpack/wav-write.h>
#include <test/test.h>

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
		// printf("wavwrite_process: %d\n", r);
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
			printf("err: %s\n", wavwrite_error(&w));
			x(0);
		}
	}

end:
	wavwrite_close(&w);
	return v;
}

static void wavr_log(void *udata, ffstr msg)
{
	(void)udata;
	printf("%.*s\n", (int)msg.len, msg.ptr);
}

void test_wav_read(ffstr data)
{
	int r;
	ffstr in, out;
	wavread w = {};
	w.log = wavr_log;
	wavread_open(&w);
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;

	for (;;) {
		r = wavread_process(&w, &in, &out);
		// printf("wavread_process: %d\n", r);
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
			printf("wavread_tag: %d = %.*s\n"
				, t
				, (int)val.len, val.ptr);
			break;
		}

		case WAVREAD_SEEK:
			off = wavread_offset(&w);
			// fallthrough
		case WAVREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			printf("err: %s\n", wavread_error(&w));
			x(0);
		}
	}

next:
	for (;;) {
		r = wavread_process(&w, &in, &out);
		// printf("wavread_process: %d\n", r);
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
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			printf("err: %s\n", wavread_error(&w));
			x(0);
		}
	}

	//wavread_seek

end:
	wavread_close(&w);
}

#include <ffbase/mem-print.h>

void test_wav()
{
	ffvec buf = {};
	buf = test_wav_write();

#if 0
	ffstr s = ffmem_print(buf.ptr, buf.len, FFMEM_PRINT_ZEROSPACE);
	printf("%.*s\n", (int)s.len, s.ptr);
	ffstr_free(&s);
#endif

	ffstr data = FFSTR_INITSTR(&buf);
	test_wav_read(data);

	ffvec_free(&buf);
}
