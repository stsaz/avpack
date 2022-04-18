/** avpack: tester
2021, Simon Zolin */

#include <ffbase/stringz.h>
#include <ffbase/vector.h>
#include <test/test.h>
#include <avpack/shared.h>

extern void test_aac();
extern void test_ape();
extern void test_apetag();
extern void test_avi();
extern void test_bmp();
extern void test_caf();
extern void test_cue();
extern void test_flac();
extern void test_flacogg();
extern void test_icy();
extern void test_jpg();
extern void test_m3u();
extern void test_mkv();
extern void test_mp3();
extern void test_mp4();
extern void test_mpc();
extern void test_ogg();
extern void test_pls();
extern void test_png();
extern void test_vorbistag();
extern void test_wav();
extern void test_wv();
void test_gather();
void test_stream();

struct test {
	const char *name;
	void (*func)();
};
#define T(nm) { #nm, &test_ ## nm }
static const struct test atests[] = {
	T(aac),
	T(ape),
	T(apetag),
	T(avi),
	T(bmp),
	T(caf),
	T(cue),
	T(flac),
	T(flacogg),
	T(gather),
	T(icy),
	T(jpg),
	T(m3u),
	T(mkv),
	T(mp3),
	T(mp4),
	T(mpc),
	T(ogg),
	T(pls),
	T(png),
	T(stream),
	T(vorbistag),
	T(wav),
	T(wv),
};
#undef T

int Verbose;

void gather(ffstr d, ffuint partial)
{
	ffstr in = {}, out = {};
	int r, pos, n, first = 1;
	ffvec buf = {};
	for (;;) {
		n = ffmin(partial, d.len);
		if (first) {
			first = 0;
			n = 1;
		}
		x(n != 0);
		ffstr_set(&in, d.ptr, n);
		ffstr_shift(&d, n);

		for (;;) {
			r = _avpack_gather_header(&buf, in, 4, &out);
			ffstr_shift(&in, r);
			xlog("1 d='%S' buf='%S' out='%S'", &d, &buf, &out);
			if (out.len == 0)
				break;

			pos = ffstr_findz(&out, "1234");
			if (pos >= 0) {
				goto done;
			}
			r = _avpack_gather_trailer(&buf, in, 4, r);
			in.ptr += r,  in.len -= r;
			xlog("2 d='%S' buf='%S'", &d, &buf);
		}
	}

done:
	ffvec_free(&buf);
}

void test_gather()
{
	ffstr d = FFSTR_INITZ("abcdef1234wxyz");
	for (ffuint i = 1;  i <= d.len;  i++) {
		gather(d, i);
		xlog("");
	}
}

void test_stream()
{
	struct avp_stream s = {};
	_avp_stream_realloc(&s, 8);
	int r;
	ffstr in;
	ffstr v;

	// empty -> some
	ffstr_setz(&in, "12");
	r = _avp_stream_gather(&s, in, 4, &v);
	x(r == 2);
	xseq(&v, "12");

	// some -> middle
	ffstr_setz(&in, "345");
	r = _avp_stream_gather(&s, in, 4, &v);
	x(r == 3);
	xseq(&v, "12345");
	_avp_stream_consume(&s, 2); // "..345"

	// middle -> full
	ffstr_setz(&in, "678");
	r = _avp_stream_gather(&s, in, 6, &v);
	x(r == 3);
	xseq(&v, "345678");
	_avp_stream_consume(&s, 6); // "........"

	// full -> (move) middle
	ffstr_setz(&in, "1234");
	r = _avp_stream_gather(&s, in, 3, &v);
	x(r == 4);
	xseq(&v, "1234");
	x(v.ptr == s.ptr);
	_avp_stream_consume(&s, 2); // "..34"

	// middle -> (move) middle
	ffstr_setz(&in, "56789abc");
	r = _avp_stream_gather(&s, in, 7, &v);
	x(r == 6);
	xseq(&v, "3456789a");
	x(v.ptr == s.ptr);

	x(0 == _avp_stream_realloc(&s, 12));
	x(s.cap == 16);
	v = _avp_stream_view(&s);
	xseq(&v, "3456789a");

	_avp_stream_free(&s);
}

int main(int argc, const char **argv)
{
	const struct test *t;

	if (argc == 1) {
		ffvec v = {};
		ffvec_addsz(&v, "Usage: avpack-test [-v] TEST...\n");
		ffvec_addsz(&v, "Supported tests: all ");
		FF_FOREACH(atests, t) {
			ffvec_addfmt(&v, "%s ", t->name);
		}
		ffvec_addsz(&v, "\nOptions:\n-v  Verbose");
		xlog("%S", &v);
		ffvec_free(&v);
		return 0;
	}

	ffuint ia = 1;
	if (ffsz_eq(argv[ia], "-v")) {
		Verbose = 1;
		ia++;
	}

	if (ffsz_eq(argv[ia], "all")) {
		//run all tests
		FF_FOREACH(atests, t) {
			xlog("%s", t->name);
			t->func();
			xlog("  OK");
		}
		return 0;
	}

	//run the specified tests only

	for (ffuint n = ia;  n < (ffuint)argc;  n++) {
		const struct test *sel = NULL;

		FF_FOREACH(atests, t) {
			if (ffsz_eq(argv[n], t->name)) {
				sel = t;
				goto call;
			}
		}

		if (sel == NULL) {
			xlog("unknown test: %s", argv[n]);
			return 1;
		}

call:
		xlog("%s", sel->name);
		sel->func();
		xlog("  OK");
	}

	return 0;
}
