/** avpack: ID3v1 tag
2021, Simon Zolin
*/

/*
id3v1write_init
id3v1write_set
id3v1read_process
*/

#pragma once

#include <avpack/mmtag.h>
#include <ffbase/stringz.h>

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

static inline void id3v1write_init(struct id3v1 *t)
{
	ffmem_zero_obj(t);
	ffmem_copy(t->tag, "TAG", 3);
	t->genre = 0xff;
}

/** Set ID3v1 tag field
id: enum MMTAG
Return N of bytes copied */
static inline int id3v1write_set(struct id3v1 *t, ffuint id, ffstr data)
{
	char *p;
	ffsize n = data.len;

	switch (id) {
	case MMTAG_TITLE:
		p = t->title;
		n = ffmin(n, sizeof(t->title));
		break;

	case MMTAG_ARTIST:
		p = t->artist;
		n = ffmin(n, sizeof(t->artist));
		break;

	case MMTAG_ALBUM:
		p = t->album;
		n = ffmin(n, sizeof(t->album));
		break;

	case MMTAG_DATE:
		p = t->year;
		n = ffmin(n, sizeof(t->year));
		break;

	case MMTAG_COMMENT:
		p = t->comment;
		n = ffmin(n, sizeof(t->comment) - 1);
		break;

	case MMTAG_TRACKNO:
		if (!ffstr_toint(&data, &t->track_no, FFS_INT8))
			return 0;
		return data.len;

	case MMTAG_GENRE:
		for (ffsize i = 0;  i != FF_COUNT(id3v1_genres);  i++) {
			if (0 == ffstr_icmpz(&data, id3v1_genres[i])) {
				t->genre = i;
				return data.len;
			}
		}
		return 0;

	default:
		return 0;
	}

	ffmem_copy(p, data.ptr, n);
	return n;
}

typedef struct id3v1read {
	ffuint state;
	char data[30*4];
	ffuint codepage;
} id3v1read;

enum ID3V1READ_R {
	ID3V1READ_NO = 1,
	ID3V1READ_DONE,
};

/** Read next ID3v1 tag field
data: whole ID3v1 tag
Return >0: enum ID3V1READ_R
 <=0: enum MMTAG */
static inline int id3v1read_process(struct id3v1read *rd, ffstr data, ffstr *val)
{
	enum { I_TITLE, I_ARTIST, I_ALBUM, I_YEAR, I_COMMENT, I_TRKNO, I_GENRE, I_DONE };
	struct id3v1 *t = (struct id3v1*)data.ptr;
	ffstr s = {};
	int r = 0;

	switch (rd->state) {

	case I_TITLE:
		if (data.len != sizeof(struct id3v1)
			|| ffmem_cmp(t, "TAG", 3))
			return ID3V1READ_NO;

		if (!(t->title[0] == '\0' || t->title[0] == ' ')) {
			ffstr_set(&s, t->title, sizeof(t->title));
			rd->state = I_ARTIST;
			r = MMTAG_TITLE;
			break;
		}
		// fallthrough

	case I_ARTIST:
		if (!(t->artist[0] == '\0' || t->artist[0] == ' ')) {
			ffstr_set(&s, t->artist, sizeof(t->artist));
			rd->state = I_ALBUM;
			r = MMTAG_ARTIST;
			break;
		}
		// fallthrough

	case I_ALBUM:
		if (!(t->album[0] == '\0' || t->album[0] == ' ')) {
			ffstr_set(&s, t->album, sizeof(t->album));
			rd->state = I_YEAR;
			r = MMTAG_ALBUM;
			break;
		}
		// fallthrough

	case I_YEAR:
		if (!(t->year[0] == '\0' || t->year[0] == ' ')) {
			ffstr_set(&s, t->year, sizeof(t->year));
			rd->state = I_COMMENT;
			r = MMTAG_DATE;
			break;
		}
		// fallthrough

	case I_COMMENT:
		if (!(t->comment[0] == '\0' || t->comment[0] == ' ')) {

			ffstr_set(&s, t->comment, sizeof(t->comment));
			rd->state = I_TRKNO;
			if (t->comment[28] != '\0') {
				s.len++;
				rd->state = I_GENRE;
			}

			r = MMTAG_COMMENT;
			break;
		}
		// fallthrough

	case I_TRKNO:
		if (t->track_no != 0) {
			ffuint n = ffs_fromint(t->track_no, rd->data, sizeof(rd->data), FFS_INTZERO | FFS_INTWIDTH(2));
			ffstr_set(val, rd->data, n);
			rd->state = I_GENRE;
			return -MMTAG_TRACKNO;
		}
		// fallthrough

	case I_GENRE:
		if (t->genre < FF_COUNT(id3v1_genres)) {
			ffstr_setz(val, id3v1_genres[t->genre]);
			rd->state = I_DONE;
			return -MMTAG_GENRE;
		}
		// fallthrough

	case I_DONE:
		return ID3V1READ_DONE;
	}

	// trim \0 and ' '
	ffssize i;
	for (i = s.len - 1;  i >= 0;  i--) {
		if (!(s.ptr[i] == '\0' || s.ptr[i] == ' '))
			break;
	}
	s.len = i+1;

	i = ffutf8_from_utf8(rd->data, sizeof(rd->data), s.ptr, s.len, 0);
	if (i != (ffssize)s.len) {
		if (rd->codepage == 0)
			rd->codepage = FFUNICODE_WIN1252;
		i = ffutf8_from_cp(rd->data, sizeof(rd->data), s.ptr, s.len, rd->codepage);
	}
	if (i < 0)
		i = 0;
	ffstr_set(val, rd->data, i);

	return -r;
}
