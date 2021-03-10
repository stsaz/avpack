/** avpack: .caf tester
2021, Simon Zolin
*/

#include <avpack/caf-read.h>
#include <test/test.h>

// 1msec (44 frames), pcm_s16le, 44100 Hz, stereo, meta='artist=A;title=T'
const ffbyte caf_sample[] = {
"\x63\x61\x66\x66\x00\x01\x00\x00\x64\x65\x73\x63\x00\x00\x00\x00"
"\x00\x00\x00\x20\x40\xe5\x88\x80\x00\x00\x00\x00\x6c\x70\x63\x6d"
"\x00\x00\x00\x02\x00\x00\x00\x04\x00\x00\x00\x01\x00\x00\x00\x02"
"\x00\x00\x00\x10\x63\x68\x61\x6e\x00\x00\x00\x00\x00\x00\x00\x0c"
"\x00\x65\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x69\x6e\x66\x6f"
"\x00\x00\x00\x00\x00\x00\x00\x2b\x00\x00\x00\x03\x41\x52\x54\x49"
"\x53\x54\x00\x41\x00\x54\x49\x54\x4c\x45\x00\x54\x00\x65\x6e\x63"
"\x6f\x64\x65\x72\x00\x4c\x61\x76\x66\x35\x38\x2e\x34\x35\x2e\x31"
"\x30\x30\x00\x64\x61\x74\x61\x00\x00\x00\x00\x00\x00\x00\xb4\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00"
};

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
	ffuint itag = 0;

	for (;;) {
		r = cafread_process(&c, &in, &out);
		// printf("cafread_process: %d\n", r);
		switch (r) {
		case CAFREAD_HEADER: {
			const struct caf_info *ai = cafread_info(&c);
			xieq(CAF_LPCM, ai->codec);
			xieq(16, ai->format&0xff);
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
			switch (itag++) {
			case 0:
				xseq(&name, "ARTIST");
				xseq(&val, "A");
				break;
			case 1:
				xseq(&name, "TITLE");
				xseq(&val, "T");
				break;
			}
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
	ffstr data = {};
	ffstr_set(&data, caf_sample, sizeof(caf_sample)-1);

#if 0
	x(0 == file_readall("/tmp/1.caf", &data));
#endif

	test_caf_read(data);
}
