/** avpack: .pls tester
2021, Simon Zolin
*/

#include <avpack/pls.h>
#include <test/test.h>

const char pls_sample[] = { "\
[playlist]\r\n\
File1=URL1\r\n\
Title1=TITLE1\r\n\
Length1=1\r\n\
File2=URL2\r\n\
Title2=TITLE2\r\n\
Length2=2\r\n\
" };

void pls_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	plsread p = {};
	plsread_open(&p);
	ffuint off = 0, trk_idx = 0, idx;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = plsread_process(&p, &in, &out, &idx);
		if (r != PLSREAD_MORE)
			xlog("r:%d out:'%S'", r, &out);

		if (r != PLSREAD_MORE && trk_idx+1 != idx)
			trk_idx++;

		switch (r) {
		case PLSREAD_URL: {
			static const char* vals[] = { "URL1", "URL2" };
			xseq(&out, vals[trk_idx]);
			break;
		}
		case PLSREAD_TITLE: {
			static const char* vals[] = { "TITLE1", "TITLE2" };
			xseq(&out, vals[trk_idx]);
			break;
		}

		case PLSREAD_DURATION: {
			static ffbyte vals[] = { 1,2 };
			xieq(vals[trk_idx], plsread_duration_sec(&p));
			break;
		}

		case PLSREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_set(&in, data.ptr + off, data.len - off);
			if (partial != 0)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("plsread_process: %s  line %u", plsread_error(&p), plsread_line(&p));
			x(0);
		}
	}

end:
	x(trk_idx == 1);
	plsread_close(&p);
}

void test_pls()
{
	ffstr data = {};
	ffstr_set(&data, pls_sample, sizeof(pls_sample)-1);
	pls_read(data, 0);
	pls_read(data, 3);
}
