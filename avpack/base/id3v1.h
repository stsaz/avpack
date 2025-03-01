/** avpack: ID3v1 tag
2021, Simon Zolin */

#pragma once
#include <ffbase/base.h>

struct id3v1 {
	char tag[3]; // "TAG"
	char title[30];
	char artist[30];
	char album[30];
	char year[4];
	char comment[29];
	ffbyte track_no; // valid if comment[28]==0;  undefined: 0
	ffbyte genre; // undefined: 0xff
};

static const char id3v1_genres[][18] = {
	"Blues", "Classic Rock", "Country", "Dance", "Disco",
	"Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
	"New Age", "Oldies", "Other", "Pop", "R&B",
	"Rap", "Reggae", "Rock", "Techno", "Industrial",
	"Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack",
	"Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk",
	"Fusion", "Trance", "Classical", "Instrumental", "Acid",
	"House", "Game", "Sound Clip", "Gospel", "Noise",
	"AlternRock", "Bass", "Soul", "Punk", "Space",
	"Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
	"Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance",
	"Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
	"Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American",
	"Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes",
	"Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz",
	"Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock", //75-79
};
