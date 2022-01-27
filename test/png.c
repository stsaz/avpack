/** avpack: .png tester
2021, Simon Zolin
*/

#include <avpack/png-read.h>
#include <test/test.h>

static const char png_sample[] =
	"\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52"
	"\x00\x00\x02\x58\x00\x00\x01\x2c\x08\x06\x00\x00\x00\x1f\x3d\xcd"
	"\x96\x00\x00\x00\x09\x70\x48\x59\x73\x00\x00\x16\x25\x00\x00\x16"
	"\x25\x01\x49\x52\x24\xf0";

void test_png_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	pngread p = {};
	pngread_open(&p);
	ffuint off = 0;
	int hdr = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = pngread_process(&p, &in, &out);
		// xlog("pngread_process: %d", r);
		switch (r) {
		case PNGREAD_HEADER: {
			hdr = 1;
			const struct png_info *info = pngread_info(&p);
			xieq(600, info->width);
			xieq(300, info->height);
			xieq(32, info->bpp);
			goto end;
		}

		case PNGREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_setstr(&in, &data);
			ffstr_shift(&in, off);
			if (partial)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("pngread_process: %s", pngread_error(&p));
			x(0);
		}
	}

end:
	x(hdr);
	pngread_close(&p);
}

void test_png()
{
	ffstr data = {};
	ffstr_set(&data, png_sample, sizeof(png_sample)-1);

	test_png_read(data, 0);
	test_png_read(data, 3);
}
