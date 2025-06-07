/** avpack: file format detector
2025, Simon Zolin */

#pragma once
#include <avpack/decl.h>
#include <avpack/base/adts.h>
#include <avpack/base/mpeg1.h>

/** Detect file format by first several bytes
Return enum AVPK_FORMAT */
static inline int avpk_format_detect(const void *data, ffsize len)
{
	const ffbyte *d = data;

	if (len >= 189) {
		// byte sync // 0x47
		if (d[0] == 0x47
			&& d[188] == 0x47)
			return AVPKF_TS;
	}

	if (len >= 12) {
		// byte id[4]; // "RIFF"
		// byte size[4];
		// byte wave[4]; // "WAVE"
		if (!memcmp(&d[0], "RIFF", 4)
			&& !memcmp(&d[8], "WAVE", 4))
			return AVPKF_WAV;
	}

	if (len >= 12) {
		// byte id[4]; // "RIFF"
		// byte size[4];
		// byte wave[4]; // "AVI "
		if (!memcmp(&d[0], "RIFF", 4)
			&& !memcmp(&d[8], "AVI ", 4))
			return AVPKF_AVI;
	}

	if (len >= 11) {
		if (!memcmp(d, "[playlist]", 10)
			&& (d[10] == '\r' || d[10] == '\n'))
			return AVPKF_PLS;
	}

	if (len >= 10) {
		// id[4] // "wvpk"
		// size[4]
		// ver[2] // "XX 04"
		if (!memcmp(&d[0], "wvpk", 4)
			&& d[9] == 0x04)
			return AVPKF_WV;
	}

	if (len >= 8) {
		// byte size[4];
		// byte type[4]; // "ftyp"
		if (!memcmp(&d[4], "ftyp", 4)
			&& ffint_be_cpu32_ptr(&d[0]) <= 255)
			return AVPKF_MP4;
	}

	if (len >= 8) {
		// char caff[4]; // "caff"
		// ffbyte ver[2]; // =1
		// ffbyte flags[2]; // =0
		if (!memcmp(d, "caff\x00\x01\x00\x00", 8))
			return AVPKF_CAF;
	}

	if (len >= 8) {
		if (!memcmp(d, "#EXTM3U", 7)
			&& (d[7] == '\r' || d[7] == '\n'))
			return AVPKF_M3U;
	}

	if (len >= 7) {
		struct adts_hdr h;
		if (adts_hdr_read(&h, d, len) > 0)
			return AVPKF_AAC;
	}

	if (len >= 5) {
		// byte sync[4]; // "OggS"
		// byte ver; // 0x0
		if (!memcmp(&d[0], "OggS", 4)
			&& d[4] == 0)
			return AVPKF_OGG;
	}

	if (len >= 5) {
		// byte sig[4]; // "fLaC"
		// byte last_type; // [1] [7]
		if (!memcmp(&d[0], "fLaC", 4)
			&& (d[4] & 0x7f) < 9)
			return AVPKF_FLAC;
	}

	if (len >= 5) {
		// ID3v2 (.mp3)
		// byte id3[3]; // "ID3"
		// byte ver[2]; // e.g. 0x3 0x0
		if (!memcmp(&d[0], "ID3", 3)
			&& d[3] <= 9
			&& d[4] <= 9)
			return AVPKF_ID3;
	}

	if (len >= 4) {
		// byte id[4] // 1a45dfa3
		if (!memcmp(d, "\x1a\x45\xdf\xa3", 4))
			return AVPKF_MKV;
	}

	if (len >= 4) {
		if (mpeg1_valid(d))
			return AVPKF_MP3;
	}

	return 0;
}
