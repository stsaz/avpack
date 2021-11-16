/** avpack: ICY tester
2021, Simon Zolin
*/

#include <avpack/icy.h>
#include <test/test.h>

static const char icy_sample[] = {
	"1234"
	"\0"
	"5678"
	"\x03StreamTitle='artist - title';StreamUrl='';\0\0\0\0\0\0"
	"9012"
};
static const char icymeta_sample[] = {
	"StreamTitle='artist - title';StreamUrl='';\0\0\0\0\0\0"
};
static const char icymeta_sample_complex[] = {
	"StreamTitle='artist - title with ' and ; title2';StreamUrl='';\0\0\0\0\0\0"
};
static const char icymeta_sample2[] = {
	"\x03StreamTitle='artist - title';StreamUrl='';\0\0\0\0\0\0"
};

void test_icy_read(ffstr data, int partial)
{
	ffvec v = {};
	icyread c = {};
	icyread_open(&c, 4);
	ffstr in = {}, out;
	ffuint off = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		int r = icyread_process(&c, &in, &out);
		switch (r) {
		case ICYREAD_META:
			ffstr_eq(&out, icymeta_sample2, sizeof(icymeta_sample2)-1);
			break;

		case ICYREAD_DATA:
			ffvec_add2(&v, &out, 1);
			break;

		case ICYREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case ICYREAD_ERROR:
		default:
			x(0);
		}
	}

end:
	xseq((ffstr*)&v, "123456789012");
	ffvec_free(&v);
	icyread_close(&c);
}

void test_icymeta_read()
{
	ffstr d, k, v;
	ffstr_set(&d, icymeta_sample, sizeof(icymeta_sample)-1);

	x(0 == icymeta_read(&d, &k, &v));
	xseq(&k, "StreamTitle");
	xseq(&v, "artist - title");

	x(0 == icymeta_read(&d, &k, &v));
	xseq(&k, "StreamUrl");
	xseq(&v, "");

	x(0 != icymeta_read(&d, &k, &v));

	ffstr_setz(&v, "artist - title");
	ffstr a, t;
	icymeta_artist_title(v, &a, &t);
	xseq(&a, "artist");
	xseq(&t, "title");

	ffstr_set(&d, icymeta_sample_complex, sizeof(icymeta_sample_complex)-1);
	x(0 == icymeta_read(&d, &k, &v));
	xseq(&k, "StreamTitle");
	xseq(&v, "artist - title with ' and ; title2");
}

void test_icymeta_write()
{
	ffvec d = {};
	ffstr k, v;
	ffstr_setz(&k, "StreamTitle");
	ffstr_setz(&v, "artist - title");
	icymeta_add(&d, k, v);

	ffstr_setz(&k, "StreamUrl");
	ffstr_setz(&v, "");
	icymeta_add(&d, k, v);

	icymeta_fin(&d);

	x(ffstr_eq(&d, icymeta_sample2, sizeof(icymeta_sample2)-1));
	ffvec_free(&d);
}

void test_icy()
{
	test_icymeta_read();
	test_icymeta_write();

	ffstr d = FFSTR_INITN(icy_sample, sizeof(icy_sample)-1);
	test_icy_read(d, 0);
	test_icy_read(d, 3);
}
