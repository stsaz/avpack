/** avpack: .caf tester
2021, Simon Zolin
*/

#include <avpack/caf-read.h>
#include <test/test.h>

static void cafr_log(void *udata, ffstr msg)
{
	(void)udata;
	printf("%.*s\n", (int)msg.len, msg.ptr);
}

void test_caf_read(ffstr data)
{
	int r;
	ffstr in, out;
	cafread c = {};
	c.log = cafr_log;
	cafread_open(&c);
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;

	for (;;) {
		r = cafread_process(&c, &in, &out);
		// printf("cafread_process: %d\n", r);
		switch (r) {
		case CAFREAD_HEADER: {
			const struct caf_info *ai = cafread_info(&c);
			xieq(CAF_LPCM, ai->codec);
			xieq(16, ai->bits);
			xieq(2, ai->channels);
			xieq(44100, ai->sample_rate);
			goto next;
		}

		case CAFREAD_TAG: {
			ffstr val;
			ffstr name = cafread_tag(&c, &val);
			printf("cafread_tag: %.*s = %.*s\n"
				, (int)name.len, name.ptr
				, (int)val.len, val.ptr);
			break;
		}

		case CAFREAD_SEEK:
			off = cafread_offset(&c);
			// fallthrough
		case CAFREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			printf("cafread_process: %s\n", cafread_error(&c));
			x(0);
		}
	}

next:
	for (;;) {
		r = cafread_process(&c, &in, &out);
		// printf("cafread_process: %d\n", r);
		switch (r) {
		case CAFREAD_DATA:
			// xseq(&out, "aacframe1");
			// printf("curpos=%u\n", (int)cafread_curpos(&c));
			break;
		case CAFREAD_DONE:
			goto end;

		case CAFREAD_SEEK:
			off = cafread_offset(&c);
			// fallthrough
		case CAFREAD_MORE:
		case CAFREAD_MORE_OR_DONE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			x(0);
		}
	}

	//cafread_seek

end:
	cafread_close(&c);
}

void test_caf()
{
	ffstr data;

#if 1
	x(0 == file_readall("/tmp/1.caf", &data));
#endif

	test_caf_read(data);
}
