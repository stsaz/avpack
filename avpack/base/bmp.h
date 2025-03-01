/** avpack: .bmp format
2016,2021, Simon Zolin
*/

/* .bmp format:
FILEHDR (HDRV3 | HDRV4) (ROW#HEIGHT..ROW#1(BGR#1..BGR#WIDTH [PADDING:1..3]))
*/

#pragma once

#include <ffbase/base.h>

enum BMP_COMP {
	BMP_COMP_NONE,
	BMP_COMP_BITFIELDS = 3,
};

struct bmp_hdr {
//file header:
	ffbyte bm[2]; //"BM"
	ffbyte filesize[4];
	ffbyte reserved[4];
	ffbyte headersize[4];

//bitmap header:
	ffbyte infosize[4];
	ffbyte width[4];
	ffbyte height[4];
	ffbyte planes[2];
	ffbyte bpp[2];

	ffbyte compression[4]; //enum BMP_COMP
	ffbyte sizeimage[4];
	ffbyte xscale[4];
	ffbyte yscale[4];
	ffbyte colors[4];
	ffbyte clrimportant[4];
};

struct bmp_hdr4 {
	ffbyte mask_rgba[4*4];
	ffbyte cstype[4];
	ffbyte red_xyz[4*3];
	ffbyte green_xyz[4*3];
	ffbyte blue_xyz[4*3];
	ffbyte gamma_rgb[4*3];
};
