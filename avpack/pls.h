/** avpack: .pls reader
2021, Simon Zolin
*/

/*
plsread_open plsread_close
plsread_process
plsread_duration_sec
plsread_line
plsread_error
*/

/* .pls format:
[playlist]
FileN=URL
TitleN=TITLE
LengthN=LEN
*/

#pragma once

#include <ffbase/vector.h>
#include <ffbase/stringz.h>

typedef struct plsread {
	ffuint line_num;
	ffuint dur;
	ffvec buf;
	const char *error;
	int hdr;
} plsread;

static inline void plsread_open(plsread *p)
{
	(void)p;
}

static inline void plsread_close(plsread *p)
{
	ffvec_free(&p->buf);
}

#define _PLSR_WARN(p, e) \
	(p)->error = (e),  PLSREAD_WARN

static inline const char* plsread_error(plsread *p)
{
	return p->error;
}

enum PLSREAD_R {
	PLSREAD_MORE, // need more input data
	PLSREAD_WARN, // call plsread_error()

	PLSREAD_URL,
	PLSREAD_TITLE,
	PLSREAD_DURATION, // call plsread_duration_sec()
};

/**
index: [Output] index of the current entry
Return enum PLSREAD_R */
static inline int plsread_process(plsread *p, ffstr *input, ffstr *output, ffuint *index)
{
	for (;;) {

		ffssize pos = ffstr_findchar(input, '\n');
		if (pos < 0) {
			if (input->len != ffvec_add(&p->buf, input->ptr, input->len, 1))
				return _PLSR_WARN(p, "not enough memory");
			ffstr_shift(input, input->len);
			return PLSREAD_MORE;
		}

		ffstr line;
		ffstr_set(&line, input->ptr, pos);
		if (p->buf.len != 0) {
			if ((ffsize)pos != ffvec_add(&p->buf, input->ptr, pos, 1))
				return _PLSR_WARN(p, "not enough memory");
			ffstr_setstr(&line, &p->buf);
			p->buf.len = 0;
		}
		ffstr_shift(input, pos+1);
		ffstr_trimwhite(&line);
		p->line_num++;
		if (line.len == 0)
			continue;

		if (!p->hdr) {
			p->hdr = 1;
			if (!ffstr_ieqcz(&line, "[playlist]"))
				return _PLSR_WARN(p, "no .pls header");
			continue;
		}

		ffstr name, val;
		if (-1 == ffstr_splitby(&line, '=', &name, &val))
			continue;

		int r, shift;
		if (ffstr_imatchz(&name, "file")) {
			r = PLSREAD_URL;
			shift = FFS_LEN("file");
		} else if (ffstr_imatchz(&name, "title")) {
			r = PLSREAD_TITLE;
			shift = FFS_LEN("title");
		} else if (ffstr_imatchz(&name, "length")) {
			r = PLSREAD_DURATION;
			shift = FFS_LEN("length");
			p->dur = 0;
			(void)ffstr_toint(&val, &p->dur, FFS_INT32);
		} else {
			continue; // unsupported key
		}

		ffstr_shift(&name, shift);
		ffuint num;
		if (!ffstr_toint(&name, &num, FFS_INT32))
			continue;

		*index = num;
		*output = val;
		return r;
	}
}

#define plsread_duration_sec(p)  ((p)->dur)
#define plsread_line(p)  ((p)->line_num)

#undef _PLSR_WARN
