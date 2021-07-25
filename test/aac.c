/** avpack: .aac tester
2021, Simon Zolin
*/

#include <avpack/aac-read.h>
#include <test/test.h>

static const char aac_sample[] =
"junk"
"\xff\xf1\x50\x80\x03\xdf\xfc"
"frame1                 "
"\xff\x00" // bad frame header
"\xff\xf1\x50\x80\x03\xdf\xfc"
"frame2                 "
"\xff\xf1\x50\x80\x03\xdf\xfc"
"frame3                 ";

void test_aac_read(ffstr data)
{
	int r;
	ffstr in = {}, out;
	aacread a = {};
	aacread_open(&a);
	int partial = 4;
	ffuint off = 0;
	int frno = 0;

	for (;;) {
		r = aacread_process(&a, &in, &out);
		// printf("aacread_process: %d\n", r);
		switch (r) {
		case AACREAD_HEADER: {
			const struct aacread_info *info = aacread_info(&a);
			xieq(2, info->channels);
			xieq(44100, info->sample_rate);
			x(out.len != 0);
			break;
		}

		case AACREAD_FRAME:
		case AACREAD_DATA:
			// xseq(&out, "aacframe1");
			printf("frame#%d:%u\n", frno++, (int)out.len);
			break;

		case AACREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			printf("aacread_process: %s\n", aacread_error(&a));
			x(0);
		}
	}

end:
	aacread_close(&a);
}

void test_aac()
{
	ffstr data = {};
#if 1
	ffstr_set(&data, aac_sample, sizeof(aac_sample)-1);
#else
	x(0 == file_readall("/tmp/1.aac", &data));
#endif

	test_aac_read(data);
}
