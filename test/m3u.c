/** avpack: .m3u tester
2021, Simon Zolin
*/

#include <avpack/m3u.h>
#include <test/test.h>

const char m3u_sample[] = { "\
#EXTM3U\r\n\
#EXTINF:1,ARTIST0 - TITLE0\r\n\
URL0\r\n\
#EXTINF:2,ARTIST1 - TITLE1\r\n\
#EXT-X something\r\n\
URL1\r\n\
URL2\r\n\
" };

const char m3u_sample2[] = { "\
#EXTM3U\r\n\
#EXTINF:1,ARTIST0 - TITLE0\r\n\
URL0\r\n\
#EXTINF:2,ARTIST1 - TITLE1\r\n\
URL1\r\n\
#EXTINF:3,\r\n\
URL2\r\n\
" };

void m3u_write()
{
	m3uwrite m = {};
	m3uwrite_create(&m, 0);
	{
		m3uwrite_entry e = {
			.url = FFSTR_INITZ("URL0"),
			.artist = FFSTR_INITZ("ARTIST0"),
			.title = FFSTR_INITZ("TITLE0"),
			.duration_sec = 1,
		};
		m3uwrite_process(&m, &e);
	}
	{
		m3uwrite_entry e = {
			.url = FFSTR_INITZ("URL1"),
			.artist = FFSTR_INITZ("ARTIST1"),
			.title = FFSTR_INITZ("TITLE1"),
			.duration_sec = 2,
		};
		m3uwrite_process(&m, &e);
	}
	{
		m3uwrite_entry e = {
			.url = FFSTR_INITZ("URL2"),
			.duration_sec = 3,
		};
		m3uwrite_process(&m, &e);
	}
	ffstr d = m3uwrite_fin(&m);
	x(ffstr_eq(&d, m3u_sample2, sizeof(m3u_sample2)-1));
	m3uwrite_close(&m);
}

void m3u_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	m3uread p = {};
	m3uread_open(&p);
	ffuint off = 0;
	int trk_idx = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = m3uread_process(&p, &in, &out);
		if (r != M3UREAD_MORE)
			xlog("r:%d out:'%S'", r, &out);

		switch (r) {
		case M3UREAD_URL: {
			static const char* vals[] = { "URL0", "URL1", "URL2" };
			xseq(&out, vals[trk_idx]);
			trk_idx++;
			break;
		}
		case M3UREAD_ARTIST: {
			static const char* vals[] = { "ARTIST0", "ARTIST1" };
			xseq(&out, vals[trk_idx]);
			break;
		}
		case M3UREAD_TITLE: {
			static const char* vals[] = { "TITLE0", "TITLE1" };
			xseq(&out, vals[trk_idx]);
			break;
		}

		case M3UREAD_DURATION: {
			static ffbyte vals[] = { 1, 2 };
			xieq(vals[trk_idx], m3uread_duration_sec(&p));
			break;
		}

		case M3UREAD_EXT:
			xseq(&out, "#EXT-X something");
			break;

		case M3UREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_set(&in, data.ptr + off, data.len - off);
			if (partial != 0)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		default:
			xlog("m3uread_process: %s  line %u", m3uread_error(&p), m3uread_line(&p));
			x(0);
		}
	}

end:
	x(trk_idx != 0);
	m3uread_close(&p);
}

void test_m3u()
{
	m3u_write();

	ffstr data = {};
	ffstr_set(&data, m3u_sample, sizeof(m3u_sample)-1);
	m3u_read(data, 0);
	m3u_read(data, 3);
}
