/** avpack: .cue reader
2021, Simon Zolin
*/

/*
cueread_open cueread_close
cueread_process
cueread_cdframes
cueread_tracknumber
cueread_line
cueread_error
*/

/* .cue format:
REM NAME "VAL"
PERFORMER|TITLE "VAL"
FILE "VAL" TYPE
  TRACK TRACKNO AUDIO
    PERFORMER|TITLE "VAL"
    REM NAME "VAL"
    INDEX 00|01..99 00:00:00
*/

#pragma once

#include <ffbase/vector.h>
#include <ffbase/stringz.h>

typedef struct cueread {
	ffuint state;
	const char *error;
	ffvec buf;
	ffstr line;
	ffuint line_num;
	ffuint track_num;
	ffuint cdframes;
} cueread;

#define _CUER_WARN(c, e) \
	(c)->error = (e),  CUEREAD_WARN

static inline void cueread_open(cueread *c)
{
	c->track_num = (ffuint)-1;
}

static inline void cueread_close(cueread *c)
{
	ffvec_free(&c->buf);
}

enum CUEREAD_R {
	CUEREAD_MORE, // need more input data
	CUEREAD_WARN,

	// for these values 'output' contains data:
	CUEREAD_UNKNOWN_KEY,
	CUEREAD_REM_NAME,
	CUEREAD_REM_VAL,
	CUEREAD_PERFORMER,
	CUEREAD_TITLE,
	CUEREAD_FILE,
	CUEREAD_FILE_TYPE,
	CUEREAD_TRK_NUM,
	CUEREAD_TRK_TITLE,
	CUEREAD_TRK_PERFORMER,

	// for these values call cueread_cdframes() to get the number of CD frames (1/75 sec):
	CUEREAD_TRK_INDEX00,
	CUEREAD_TRK_INDEX,
};

/**
Return enum CUEREAD_R */
static inline int cueread_process(cueread *c, ffstr *input, ffstr *output)
{
	enum {
		R_GATHER_LINE, R_KEY, R_FILE_TYPE, R_REM_VAL,
	};
	int r;

	for (;;) {
	switch (c->state) {

	case R_GATHER_LINE: {
		ffssize pos = ffstr_findchar(input, '\n');
		if (pos < 0) {
			ffvec_add2(&c->buf, input, 1);
			ffstr_shift(input, input->len);
			return CUEREAD_MORE;
		}
		ffstr_set(&c->line, input->ptr, pos);
		if (c->buf.len != 0) {
			ffvec_add(&c->buf, input->ptr, pos, 1);
			ffstr_setstr(&c->line, &c->buf);
			c->buf.len = 0;
		}
		ffstr_shift(input, pos+1);
		ffstr_trimwhite(&c->line);
		c->line_num++;
		if (c->line.len == 0)
			continue;
		c->state = R_KEY;
	}
		// fallthrough

	case R_KEY: {
		enum {
			K_FILE,
			K_INDEX,
			K_PERFORMER,
			K_REM,
			K_TITLE,
			K_TRACK,
		};
		static const char keys[][9] = {
			"FILE",
			"INDEX",
			"PERFORMER",
			"REM",
			"TITLE",
			"TRACK",
		};
		ffstr key, val;
		ffstr_splitby(&c->line, ' ', &key, &c->line);
		r = ffcharr_findsorted(keys, FF_COUNT(keys), sizeof(keys[0]), key.ptr, key.len);

		ffstr ws = FFSTR_INIT(" \t");
		ffstr_skipany(&c->line, &ws);
		if (r != K_INDEX) {
			int spl = ' ';
			if (c->line.len != 0 && c->line.ptr[0] == '"') {
				ffstr_shift(&c->line, 1);
				spl = '"';
			}
			ffstr_splitby(&c->line, spl, &val, &c->line);
			*output = val;
		}

		c->state = R_GATHER_LINE;

		switch (r) {
		case K_FILE:
			c->state = R_FILE_TYPE;
			return CUEREAD_FILE;

		case K_INDEX: {
			ffuint idx, min, sec, frames;
			if (0 != ffstr_matchfmt(&c->line, "%2u %u:%2u:%2u", &idx, &min, &sec, &frames))
				return _CUER_WARN(c, "bad index value");
			ffstr_set(output, &c->line.ptr[3], c->line.len - 3);
			c->cdframes = (ffuint64)(min*60 + sec) * 75 + frames;
			return (idx == 0) ? CUEREAD_TRK_INDEX00 : CUEREAD_TRK_INDEX;
		}

		case K_PERFORMER:
			return (c->track_num == (ffuint)-1) ? CUEREAD_PERFORMER : CUEREAD_TRK_PERFORMER;

		case K_REM:
			c->state = R_REM_VAL;
			return CUEREAD_REM_NAME;

		case K_TITLE:
			return (c->track_num == (ffuint)-1) ? CUEREAD_TITLE : CUEREAD_TRK_TITLE;

		case K_TRACK:
			if (!ffstr_to_uint32(&val, &c->track_num))
				return _CUER_WARN(c, "bad track number");
			return CUEREAD_TRK_NUM;
		}

		*output = key;
		return CUEREAD_UNKNOWN_KEY;
	}

	case R_FILE_TYPE:
	case R_REM_VAL: {
		ffstr ws = FFSTR_INIT(" \t");
		ffstr_skipany(&c->line, &ws);
		int spl = ' ';
		if (c->line.len != 0 && c->line.ptr[0] == '"') {
			ffstr_shift(&c->line, 1);
			spl = '"';
		}
		ffstr_splitby(&c->line, spl, output, NULL);
		r = (c->state == R_FILE_TYPE) ? CUEREAD_FILE_TYPE : CUEREAD_REM_VAL;
		c->state = R_GATHER_LINE;
		return r;
	}

	default:
		return -1;
	}
	}
}

#define cueread_cdframes(c)  (c)->cdframes

#define cueread_tracknumber(c)  (c)->track_num

#define cueread_line(c)  (c)->line_num

static inline const char* cueread_error(cueread *c)
{
	return c->error;
}

#undef _CUER_WARN
