/** avpack: .m3u reader
2021, Simon Zolin
*/

/*
m3uread_open m3uread_close
m3uread_process
m3uread_duration_sec
m3uread_line
m3uread_error
m3uwrite_create m3uwrite_close
m3uwrite_process
m3uwrite_fin
*/

/* .m3u format:
#EXTM3U
#EXTINF:DUR_SEC,ARTIST - TITLE
/path/filename
*/

#pragma once

#include <ffbase/vector.h>
#include <ffbase/stringz.h>

typedef struct m3uread {
	ffuint state;
	ffuint line_num;
	ffuint dur;
	ffstr line;
	ffvec buf;
	const char *error;
	int bom;
} m3uread;

static inline void m3uread_open(m3uread *m)
{
	(void)m;
}

static inline void m3uread_close(m3uread *m)
{
	ffvec_free(&m->buf);
}

#define _M3UR_WARN(m, e) \
	(m)->error = (e),  M3UREAD_WARN

static inline const char* m3uread_error(m3uread *m)
{
	return m->error;
}

enum M3UREAD_R {
	M3UREAD_MORE, // need more input data
	M3UREAD_WARN, // call m3uread_error()

	M3UREAD_URL,
	M3UREAD_ARTIST,
	M3UREAD_TITLE,
	M3UREAD_DURATION, // call m3uread_duration_sec()
	M3UREAD_EXT, // unrecognized line starting with #
};

/**
Return enum M3UREAD_R */
static inline int m3uread_process(m3uread *m, ffstr *input, ffstr *output)
{
	enum { R_GATHER_LINE, R_ARTIST, R_TITLE, };

	for (;;) {
	switch (m->state) {

	case R_GATHER_LINE: {
		ffssize pos = ffstr_findchar(input, '\n');
		if (pos < 0) {
			if (input->len != ffvec_add(&m->buf, input->ptr, input->len, 1))
				return _M3UR_WARN(m, "not enough memory");
			ffstr_shift(input, input->len);
			return M3UREAD_MORE;
		}

		ffstr line;
		ffstr_set(&line, input->ptr, pos);
		if (m->buf.len != 0) {
			if ((ffsize)pos != ffvec_add(&m->buf, input->ptr, pos, 1))
				return _M3UR_WARN(m, "not enough memory");
			ffstr_setstr(&line, &m->buf);
			m->buf.len = 0;
		}
		ffstr_shift(input, pos+1);
		ffstr_trimwhite(&line);
		m->line_num++;
		if (line.len == 0)
			continue;

		if (!m->bom) {
			m->bom = 1;
			if (ffstr_matchz(&line, "\xef\xbb\xbf")) // UTF-8 BOM
				ffstr_shift(&line, 3);
		}

		if (line.ptr[0] != '#') {
			*output = line;
			return M3UREAD_URL;
		}

		if (ffstr_imatchz(&line, "#EXTM3U")) {
			continue;

		} else if (ffstr_imatchz(&line, "#EXTINF:")) {
			ffstr_shift(&line, FFS_LEN("#EXTINF:"));
			int dur;
			ffuint n = ffs_toint(line.ptr, line.len, &dur, FFS_INT32 | FFS_INTSIGN);
			if (n == 0)
				continue;

			ffstr_set(output, line.ptr, n);
			m->dur = (dur > 0) ? dur : 0;
			ffstr_shift(&line, n);
			if (line.len >= 2 && line.ptr[0] == ',') {
				ffstr_shift(&line, 1);
				m->line = line;
				m->state = R_ARTIST;
			}
			return M3UREAD_DURATION;
		}

		*output = line;
		return M3UREAD_EXT;
	}

	case R_ARTIST: {
		int div = ffstr_find(&m->line, " - ", 3);
		if (div < 0) {
			m->state = R_TITLE;
			continue;
		}
		ffstr_set(output, m->line.ptr, div);
		ffstr_shift(&m->line, div+3);
		m->state = R_TITLE;
		return M3UREAD_ARTIST;
	}

	case R_TITLE:
		*output = m->line;
		m->state = R_GATHER_LINE;
		return M3UREAD_TITLE;
	}
	}
}

#define m3uread_duration_sec(m)  ((m)->dur)
#define m3uread_line(m)  ((m)->line_num)

#undef _M3UR_WARN


enum M3UWRITE_OPT {
	M3UWRITE_CRLF,
	M3UWRITE_LF = 1,
};

typedef struct m3uwrite {
	ffuint state;
	ffvec buf;
	ffstr crlf;
	ffuint options; // enum M3UWRITE_OPT
} m3uwrite;

/**
options: [Optional] enum M3UWRITE_OPT */
static inline void m3uwrite_create(m3uwrite *m, ffuint options)
{
	m->options = options;
}

static inline void m3uwrite_close(m3uwrite *m)
{
	ffvec_free(&m->buf);
}

typedef struct m3uwrite_entry {
	ffstr url, artist, title;
	int duration_sec;
} m3uwrite_entry;

/** Add entry to playlist
Return 0 on success */
static inline int m3uwrite_process(m3uwrite *m, const m3uwrite_entry *e)
{
	enum { W_EXTM3U, W_ENTRY };

	switch (m->state) {
	case W_EXTM3U:
		ffstr_set(&m->crlf, "\r\n", 2);
		if (m->options & M3UWRITE_LF)
			ffstr_set(&m->crlf, "\n", 1);

		if (0 == ffvec_addsz(&m->buf, "#EXTM3U")
			|| 0 == ffvec_add2(&m->buf, &m->crlf, 1))
			return -1;
		m->state = W_ENTRY;
		// fallthrough

	case W_ENTRY: {
		ffstr sep = FFSTR_INITZ(" - ");
		if (e->artist.len == 0 && e->title.len == 0)
			sep.len = 0; // don't write separator if both artist & title are empty
		if (0 == ffvec_addfmt(&m->buf, "#EXTINF:%d,%S%S%S%S" "%S%S"
			, e->duration_sec, &e->artist, &sep, &e->title, &m->crlf
			, &e->url, &m->crlf))
			return -1;
		break;
	}
	}

	return 0;
}

static inline ffstr m3uwrite_fin(m3uwrite *m)
{
	return *(ffstr*)&m->buf;
}
