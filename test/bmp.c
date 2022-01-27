/** avpack: .bmp tester
2021, Simon Zolin
*/

#include <avpack/bmp-read.h>
#include <test/test.h>

static const char bmp_sample[] =
	"\x42\x4d\xfa\xfc\x0a\x00\x00\x00\x00\x00\x7a\x00\x00\x00\x6c\x00"
	"\x00\x00\x58\x02\x00\x00\x2c\x01\x00\x00\x01\x00\x20\x00\x03\x00"
	"\x00\x00\x80\xfc\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\x00\x00\xff\x00\x00\xff"
	"\x00\x00\xff\x00\x00\x00\x42\x47\x52\x73\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff";

void test_bmp_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	bmpread p = {};
	bmpread_open(&p);
	ffuint off = 0;
	int hdr = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = bmpread_process(&p, &in, &out);
		// xlog("bmpread_process: %d", r);
		switch (r) {
		case BMPREAD_HEADER: {
			hdr = 1;
			const struct bmp_info *info = bmpread_info(&p);
			xieq(600, info->width);
			xieq(300, info->height);
			xieq(32, info->bpp);
			goto end;
		}

		case BMPREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("bmpread_process: %s", bmpread_error(&p));
			x(0);
		}
	}

end:
	x(hdr);
	bmpread_close(&p);
}

void test_bmp()
{
	ffstr data = {};
	ffstr_set(&data, bmp_sample, sizeof(bmp_sample)-1);

	test_bmp_read(data, 0);
	test_bmp_read(data, 3);
}
