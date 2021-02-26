/** avpack: .avi tester
2021, Simon Zolin
*/

#include <avpack/avi-read.h>
#include <test/test.h>

static void avir_log(void *udata, ffstr msg)
{
	(void)udata;
	printf("%.*s\n", (int)msg.len, msg.ptr);
}

void test_avi_read(ffstr data)
{
	int r;
	ffstr in, out;
	aviread a = {};
	a.log = avir_log;
	aviread_open(&a);
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;

	for (;;) {
		r = aviread_process(&a, &in, &out);
		// printf("aviread_process: %d\n", r);
		switch (r) {
		case AVIREAD_HEADER: {
			const struct avi_audio_info *ti = aviread_track_info(&a, 1);
			x(NULL == aviread_track_info(&a, 2));
			xieq(1, ti->type);
			xieq(AVI_A_MP3, ti->codec);
			xieq(0, ti->bits);
			xieq(2, ti->channels);
			xieq(48000, ti->sample_rate);
			x(ti->codec_conf.len == 0);
			goto next;
		}

		case AVIREAD_TAG: {
			ffstr val;
			int t = aviread_tag(&a, &val);
			printf("aviread_tag: %d = %.*s\n"
				, t
				, (int)val.len, val.ptr);
			break;
		}

		case AVIREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			printf("aviread_process: %s\n", aviread_error(&a));
			x(0);
		}
	}

next:
	aviread_track_activate(&a, 1);
	for (;;) {
		r = aviread_process(&a, &in, &out);
		// printf("aviread_process: %d\n", r);
		switch (r) {
		case AVIREAD_DATA:
			// xseq(&out, "aacframe1");
			// printf("curpos=%u\n", (int)aviread_curpos(&a));
			break;
		case AVIREAD_DONE:
			goto end;

		case AVIREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			x(0);
		}
	}

	//aviread_seek

end:
	aviread_close(&a);
}

void test_avi()
{
	ffstr data;

#if 1
	x(0 == file_readall("/tmp/1.avi", &data));
#endif

	test_avi_read(data);
}
