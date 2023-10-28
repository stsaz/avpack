/** avpack: .mpc tester
2021, Simon Zolin
*/

#include <avpack/mpc-read.h>
#include <test/test.h>

static const ffbyte mpc_sample[] = {
"MPCK"
"SH" "\x0e\x73\x5b\xdd\x4e\x08\x82\xd8\x44\x00\x1b\x1b"
"AP" "\x09" "frame1"
"junk"
"AP" "\x09" "frame2"
"AP" "\x09" "frame3"
"ST" "\x0c" "seektable"
"SE" "\x03"
};

static const ffbyte apetag[] = {
"\x00\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
"\x41\x50\x45\x54\x41\x47\x45\x58\xd0\x07\x00\x00\x8e\x00\x00\x00"
"\x03\x00\x00\x00\x00\x00\x00\xa0\x00\x00\x00\x00\x00\x00\x00\x00"
"\x07\x00\x00\x00\x00\x00\x00\x00\x4d\x50\x33\x47\x41\x49\x4e\x5f"
"\x4d\x49\x4e\x4d\x41\x58\x00\x30\x38\x33\x2c\x32\x31\x30\x0c\x00"
"\x00\x00\x00\x00\x00\x00\x52\x45\x50\x4c\x41\x59\x47\x41\x49\x4e"
"\x5f\x54\x52\x41\x43\x4b\x5f\x47\x41\x49\x4e\x00\x2b\x30\x2e\x30"
"\x36\x30\x30\x30\x30\x20\x64\x42\x08\x00\x00\x00\x00\x00\x00\x00"
"\x52\x45\x50\x4c\x41\x59\x47\x41\x49\x4e\x5f\x54\x52\x41\x43\x4b"
"\x5f\x50\x45\x41\x4b\x00\x30\x2e\x39\x32\x33\x36\x39\x37\x41\x50"
"\x45\x54\x41\x47\x45\x58\xd0\x07\x00\x00\x8e\x00\x00\x00\x03\x00"
"\x00\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00\x00\x00"
};

static void mpcr_log(void *udata, const char *fmt, va_list va)
{
	(void)udata;
	xlogv(fmt, va);
}

void test_mpc_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	mpcread c = {};
	c.log = mpcr_log;
	mpcread_open(&c, data.len);
	ffuint off = 0;
	int frno = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = mpcread_process(&c, &in, &out);
		// xlog("mpcread_process: %d", r);

		switch (r) {
		case MPCREAD_HEADER: {
			const struct mpcread_info *ai = mpcread_info(&c);
			xieq(2, ai->channels);
			xieq(44100, ai->sample_rate);
			break;
		}

		case MPCREAD_TAG: {
			struct tag {
				const char *name, *val;
			};
			static const struct tag tags[] = {
				{"MP3GAIN_MINMAX", "083,210"},
				{"REPLAYGAIN_TRACK_GAIN", "+0.060000 dB"},
				{"REPLAYGAIN_TRACK_PEAK", "0.923697"},
			};
			ffstr name, val;
			/*int t =*/ mpcread_tag(&c, &name, &val);
			xlog("mpcread_tag: %S = %S", &name, &val);
			int k = 0;
			const struct tag *tag;
			FF_FOREACH(tags, tag) {
				if (ffstr_eqz(&name, tag->name)) {
					xseq(&val, tag->val);
					k = 1;
					break;
				}
			}
			x(k == 1);
			break;
		}

		case MPCREAD_SEEK:
			off = mpcread_offset(&c);
			// fallthrough
		case MPCREAD_MORE:
			x(off < data.len);
			ffstr_set(&in, data.ptr + off, data.len - off);
			if (partial != 0)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case MPCREAD_DATA:
			xlog("frame#%u:%u", frno++, (int)out.len);
			break;

		case MPCREAD_DONE:
			goto end;

		default:
			xlog("mpcread_process: %s", mpcread_error(&c));
			x(0);
		}
	}

	//mpcread_seek

end:
	mpcread_close(&c);
}

void test_mpc()
{
	ffstr data = {};
	ffsize cap = data.len;
	ffstr_growadd(&data, &cap, mpc_sample, sizeof(mpc_sample)-1);

#if 0
	ffstr_free(&data);
	x(0 == file_readall("/tmp/1.mpc", &data));
#endif

	ffstr_growadd(&data, &cap, apetag, sizeof(apetag)-1);

	test_mpc_read(data, 0);
	test_mpc_read(data, 3);

	ffstr_free(&data);
}
