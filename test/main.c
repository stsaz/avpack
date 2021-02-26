/** avpack: tester
2021, Simon Zolin */

#include <ffbase/stringz.h>
#include <test/test.h>
#include <stdio.h>

extern void test_avi();
extern void test_caf();
extern void test_mkv();
extern void test_mp4();
extern void test_ogg();

struct test {
	const char *name;
	void (*func)();
};
#define T(nm) { #nm, &test_ ## nm }
static const struct test atests[] = {
	T(avi),
	T(caf),
	T(mkv),
	T(mp4),
	T(ogg),
};
#undef T

int main(int argc, const char **argv)
{
	const struct test *t;

	if (argc == 1) {
		printf("Supported tests: all ");
		FF_FOREACH(atests, t) {
			printf("%s ", t->name);
		}
		printf("\n");
		return 0;
	}

	if (ffsz_eq(argv[1], "all")) {
		//run all tests
		FF_FOREACH(atests, t) {
			printf("%s\n", t->name);
			t->func();
			printf("  OK\n");
		}
		return 0;
	}

	//run the specified tests only

	for (ffuint n = 1;  n < (ffuint)argc;  n++) {
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
