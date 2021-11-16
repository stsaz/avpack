/** avpack: Vorbis tag tester
2021, Simon Zolin
*/

#include <avpack/vorbistag.h>
#include <test/test.h>

void test_vorbistag()
{
	struct tag {
		ffuint name;
		const char *val;
	};
	static const struct tag tags[] = {
		{ MMTAG_VENDOR, "vendor" },
		{ MMTAG_ALBUM, "album" },
		{ MMTAG_ARTIST, "artist" },
		{ MMTAG_DATE, "date" },
		{ MMTAG_TITLE, "title" },
		{ MMTAG_TRACKNO, "1" },
	};
	vorbistagwrite vw = {};
	const struct tag *t;
	FFARRAY_FOREACH(tags, t) {
		ffstr val = FFSTR_INITZ(t->val);
		vorbistagwrite_add(&vw, t->name, val);
	}
	ffstr d = vorbistagwrite_fin(&vw);

	vorbistagread vr = {};
	ffstr name, val;
	int ntags = 0;
	for (int i = 0;  i < 10;  i++) {
		int r = vorbistagread_process(&vr, &d, &name, &val);
		if (r == VORBISTAGREAD_DONE)
			break;
		x(r == (int)tags[ntags++].name);
	}
	xieq(ntags, FF_COUNT(tags));

	vorbistagwrite_destroy(&vw);
}
