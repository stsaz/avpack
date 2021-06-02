/** avpack: tester
2021, Simon Zolin */

#include <ffbase/stringz.h>
#include <test/test.h>
#include <stdio.h>

extern void test_avi();
extern void test_caf();
extern void test_mkv();
extern void test_mp3();
extern void test_mp4();
extern void test_ogg();
extern void test_wav();

struct test {
	const char *name;
	void (*func)();
};
#define T(nm) { #nm, &test_ ## nm }
static const struct test atests[] = {
	T(avi),
	T(caf),
	T(mkv),
	T(mp3),
	T(mp4),
	T(ogg),
	T(wav),
};
#undef T

int Verbose;

int main(int argc, const char **argv)
{
	const struct test *t;

	if (argc == 1) {
		printf("Usage: avpack-test [-v] TEST...\n");
		printf("Supported tests: all ");
		FF_FOREACH(atests, t) {
			printf("%s ", t->name);
		}
		printf("\n");
		printf("Options: -v  Verbose\n");
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
			printf("%s\n", t->name);
			t->func();
			printf("  OK\n");
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
			printf("unknown test: %s\n", argv[n]);
			return 1;
		}

call:
		printf("%s\n", sel->name);
		sel->func();
		printf("  OK\n");
	}

	return 0;
}
