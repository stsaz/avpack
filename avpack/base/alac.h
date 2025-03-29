/** avpack: ALAC */

#pragma once
#include <ffbase/base.h>

struct alac_conf {
	ffbyte frame_length[4];
	ffbyte compatible_version;
	ffbyte bit_depth;
	ffbyte unused[3];
	ffbyte channels;
	ffbyte maxrun[2];
	ffbyte max_frame_bytes[4];
	ffbyte avg_bitrate[4];
	ffbyte sample_rate[4];
};
