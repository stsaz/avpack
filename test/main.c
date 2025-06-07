/** avpack: tester
2021, Simon Zolin */

#include <ffbase/stringz.h>
#include <ffbase/vector.h>
#include <test/test.h>
#include <avpack/shared.h>

extern void test_apetag();
extern void test_bmp();
extern void test_cue();
extern void test_icy();
extern void test_jpg();
extern void test_m3u();
extern void test_pls();
extern void test_png();
extern void test_vorbistag();
extern int test_reader(ffstr data, const char *ext);
extern void test_writer();
void test_gather();

struct test {
	const char *name;
	void (*func)();
};
#define T(nm) { #nm, &test_ ## nm }
static const struct test atests[] = {
	T(apetag),
	T(bmp),
	T(cue),
	T(gather),
	T(icy),
	T(jpg),
	T(m3u),
	T(pls),
	T(png),
	T(vorbistag),
	T(writer),
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

	if (ffsz_eq(argv[1], "reader")) {
		for (int i = 2;  i < argc;  i++) {
			ffstr v = {};
			file_readall(argv[i], &v);
			ffstr s = FFSTR_INITZ(argv[i]);
			int r = ffstr_rfindchar(&s, '.');
			if (r >= 0) {
				ffstr_shift(&s, r + 1);
				ffstr_lower(&s);
				xlog("%s", argv[i]);
				x(test_reader(v, s.ptr));
			}
			ffstr_free(&v);
		}
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
