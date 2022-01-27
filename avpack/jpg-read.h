/** avpack: .jpg reader
2018,2021, Simon Zolin
*/

/*
jpgread_open jpgread_close
jpgread_process
jpgread_error
jpgread_info
*/

/* .jpg format:
SOI SOF... SOS DATA... EOI
*/

#pragma once

#include <ffbase/vector.h>

struct jpg_info {
	ffuint width, height;
	ffuint bpp;
};

typedef struct jpgread {
	ffuint state, nextstate;
	ffuint gather_size;
	const char *error;
	ffvec buf;
	ffstr chunk;
	struct jpg_info info;
} jpgread;

static inline void jpgread_open(jpgread *j)
{
	(void)j;
}

static inline void jpgread_close(jpgread *j)
{
	ffvec_free(&j->buf);
}

#define JPEG_M_START 0xff

enum JPEG_T {
	JPEG_T_SOF0 = 0xc0,
	JPEG_T_SOF1,
	JPEG_T_SOF2,

	JPEG_T_SOI = 0xd8,
	JPEG_T_EOI,

	JPEG_T_SOS = 0xda,
};
struct jpeg_marker {
	ffbyte start; //=0xff
	ffbyte type; // enum JPEG_T
	ffbyte len[2]; //= 2 + datalen
	ffbyte data[0];
};

struct jpeg_sof {
	ffbyte unused;
	ffbyte height[2];
	ffbyte width[2];
	ffbyte unused2;
};

/** Parse SOF marker. */
static int jpeg_sof(struct jpg_info *info, const struct jpeg_marker *m)
{
	ffuint len = ffint_be_cpu16_ptr(m->len);
	if (len < sizeof(struct jpeg_sof))
		return 0xbad;

	const struct jpeg_sof *sof = (void*)m->data;
	info->width = ffint_be_cpu16_ptr(sof->width);
	info->height = ffint_be_cpu16_ptr(sof->height);
	info->bpp = 0;
	return 0;
}

#define _JPGR_ERR(j, e) \
	(j)->error = (e),  JPGREAD_ERROR

static inline const char* jpgread_error(jpgread *j)
{
	return j->error;
}

#define _JPGR_GATHER(j, _nextstate, _len) \
do { \
	(j)->state = R_GATHER,  (j)->nextstate = _nextstate,  (j)->gather_size = _len; \
	(j)->buf.len = 0; \
} while (0)

#define _JPGR_GATHER_MORE(j, _nextstate, len) \
	(j)->state = R_GATHER_MORE,  (j)->nextstate = _nextstate,  (j)->gather_size = len

enum JPGREAD_R {
	JPGREAD_MORE,
	JPGREAD_HEADER,
	JPGREAD_DONE,
	JPGREAD_ERROR,
};

/**
Return enum JPGREAD_R. */
static inline int jpgread_process(jpgread *j, ffstr *input, ffstr *output)
{
	(void)output;
	enum JPEG_R {
		R_INIT, R_SOI, R_MARKER_NEXT, R_MARKER, R_MARKER_DATA, R_MARKER_SKIP,
		R_GATHER_MORE, R_GATHER,
		R_ERR,
	};
	int r;

	for (;;) {
		switch (j->state) {

		case R_INIT:
			_JPGR_GATHER(j, R_SOI, 2 + sizeof(struct jpeg_marker));
			continue;

		case R_MARKER_NEXT:
			_JPGR_GATHER(j, R_MARKER, sizeof(struct jpeg_marker));
			continue;

		case R_SOI: {
			const ffbyte *b = (ffbyte*)j->chunk.ptr;
			if (!(b[0] == JPEG_M_START && b[1] == JPEG_T_SOI))
				return _JPGR_ERR(j, "bad SOI");
			ffstr_shift(&j->chunk, 2);
		}
			// fallthrough

		case R_MARKER: {
			struct jpeg_marker *m = (struct jpeg_marker*)j->chunk.ptr;
			if (m->start != JPEG_M_START)
				return _JPGR_ERR(j, "bad marker start");
			ffuint len = ffint_be_cpu16_ptr(m->len);
			// log("marker type:%xu  len:%u", m->type, len);
			if (len < 2)
				return _JPGR_ERR(j, "bad marker length");
			len -= 2;

			switch (m->type) {

			case JPEG_T_SOF0:
			case JPEG_T_SOF1:
			case JPEG_T_SOF2:
				_JPGR_GATHER_MORE(j, R_MARKER_DATA, len + sizeof(struct jpeg_marker));
				continue;

			case JPEG_T_SOS:
				j->state = R_ERR;
				return JPGREAD_HEADER;

			case JPEG_T_EOI:
				return JPGREAD_DONE;
			}

			j->state = R_MARKER_SKIP;
			j->gather_size = len;
			continue;
		}

		case R_MARKER_DATA: {
			const struct jpeg_marker *m = (struct jpeg_marker*)j->chunk.ptr;
			switch (m->type) {
			case JPEG_T_SOF0:
			case JPEG_T_SOF1:
			case JPEG_T_SOF2:
				if (0 != jpeg_sof(&j->info, m))
					return _JPGR_ERR(j, "bad SOF");
				break;
			}
			j->state = R_MARKER_NEXT;
			continue;
		}

		case R_MARKER_SKIP:
			if (input->len == 0)
				return JPGREAD_MORE;
			r = ffmin(j->gather_size, input->len);
			ffstr_shift(input, r);
			j->gather_size -= r;
			if (j->gather_size != 0)
				return JPGREAD_MORE;
			j->state = R_MARKER_NEXT;
			continue;

		case R_ERR:
			return _JPGR_ERR(j, "data reading isn't implemented");

		case R_GATHER_MORE:
			if (j->buf.len == 0) {
				if (j->chunk.len != ffvec_add2(&j->buf, &j->chunk, 1))
					return _JPGR_ERR(j, "no memory");
			} else {}
			j->state = R_GATHER;
			// fallthrough

		case R_GATHER:
			r = ffstr_gather((ffstr*)&j->buf, &j->buf.cap, input->ptr, input->len, j->gather_size, &j->chunk);
			if (r < 0)
				return _JPGR_ERR(j, "no memory");
			ffstr_shift(input, r);
			if (j->chunk.len == 0)
				return JPGREAD_MORE;
			j->state = j->nextstate;
			continue;
		}
	}
}

#undef _JPGR_GATHER
#undef _JPGR_GATHER_MORE

static inline const struct jpg_info* jpgread_info(jpgread *j)
{
	return &j->info;
}
