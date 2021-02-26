/** avpack: .mkv tester
2021, Simon Zolin
*/

#include <avpack/mkv-read.h>
#include <test/test.h>

static void mkvr_log(void *udata, ffstr msg)
{
	(void)udata;
	printf("%.*s\n", (int)msg.len, msg.ptr);
}

void test_mkv_read(ffstr data)
{
	int r;
	ffstr in, out;
	mkvread m = {};
	m.log = mkvr_log;
	mkvread_open(&m, 0);
	ffstr_set(&in, data.ptr, 1);
	ffuint off = 1;
	ffuint track_total = 2;
	ffuint track_audio = 1;

	for (;;) {
		r = mkvread_process(&m, &in, &out);
		// printf("mkvread_process: %d\n", r);
		switch (r) {
		case MKVREAD_HEADER: {
			const struct mkvread_audio_info *ti = mkvread_track_info(&m, track_audio);
			x(NULL == mkvread_track_info(&m, track_total));
			xieq(MKV_TRK_AUDIO, ti->type);
			xieq(MKV_A_AAC, ti->codec);
			xieq(32, ti->bits);
			xieq(2, ti->channels);
			xieq(48000, ti->sample_rate);
			x(ti->codec_conf.len != 0);
			goto next;
		}

		case MKVREAD_TAG: {
			ffstr val;
			ffstr name = mkvread_tag(&m, &val);
			printf("mkvread_tag: %.*s = %.*s\n"
				, (int)name.len, name.ptr
				, (int)val.len, val.ptr);
			break;
		}

		case MKVREAD_SEEK:
			off = mkvread_offset(&m);
			// fallthrough
		case MKVREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			printf("mkvread_process: %s\n", mkvread_error(&m));
			x(0);
		}
	}

next:
	for (;;) {
		r = mkvread_process(&m, &in, &out);
		// printf("mkvread_process: %d\n", r);
		switch (r) {
		case MKVREAD_DATA:
			if (mkvread_block_trackno(&m) != track_audio)
				break;
			// xseq(&out, "aacframe1");
			printf("curpos=%u\n", (int)mkvread_curpos(&m));
			break;
		case MKVREAD_DONE:
			goto end;

		case MKVREAD_SEEK:
			off = mkvread_offset(&m);
			// fallthrough
		case MKVREAD_MORE:
			ffstr_set(&in, data.ptr + off, 1);
			off++;
			break;

		default:
			x(0);
		}
	}

	//mkvread_seek

end:
	mkvread_close(&m);
}

void test_mkv()
{
	ffstr data;

#if 1
	x(0 == file_readall("/tmp/1.mkv", &data));
#endif

	test_mkv_read(data);
}
