/** avpack: .cue tester
2021, Simon Zolin
*/

#include <avpack/cue.h>
#include <test/test.h>

const char cue_sample[] = { "\
REM NAME \"VAL\"\r\n\
PERFORMER \"VAL\"\r\n\
TITLE \"VAL\"\r\n\
\r\n\
FILE \"VAL\" WAVE\r\n\
  TRACK 01 AUDIO\r\n\
    PERFORMER \"VAL\"\r\n\
    TITLE \"VAL\"\r\n\
    REM NAME \"VAL\"\r\n\
    INDEX 00 00:00:00\r\n\
    INDEX 01 00:00:01\r\n\
  TRACK 02 AUDIO\r\n\
    INDEX 00 00:00:02\r\n\
    INDEX 01 00:00:03\r\n\
  TRACK 03 AUDIO\r\n\
    INDEX 01 00:00:04\r\n\
" };

void cue_read(ffstr data, int partial)
{
	int r;
	ffstr in = {}, out;
	cueread c = {};
	cueread_open(&c);
	ffuint off = 0;
	int trk_ival = 0, trk_idx_ival = 0;

	for (int i = data.len*2;;  i--) {
		x(i >= 0);

		r = cueread_process(&c, &in, &out);
		if (r != CUEREAD_MORE)
			xlog("r:%d out:'%S'", r, &out);

		switch (r) {
		case CUEREAD_REM_NAME:
			xseq(&out, "NAME");
			break;
		case CUEREAD_REM_VAL:
		case CUEREAD_PERFORMER:
		case CUEREAD_TITLE:
		case CUEREAD_FILE:
		case CUEREAD_TRK_TITLE:
		case CUEREAD_TRK_PERFORMER:
			xseq(&out, "VAL");
			break;

		case CUEREAD_FILE_TYPE:
			xseq(&out, "WAVE");
			break;

		case CUEREAD_TRK_NUM: {
			static ffbyte vals[] = { 1,2,3 };
			xieq(vals[trk_ival++], cueread_tracknumber(&c));
			break;
		}

		case CUEREAD_TRK_INDEX00:
		case CUEREAD_TRK_INDEX: {
			static ffbyte vals[] = { 0,1,2,3,4 };
			xieq(vals[trk_idx_ival++], cueread_cdframes(&c));
			break;
		}

		case CUEREAD_MORE:
			if (off == data.len)
				goto end;
			ffstr_set(&in, data.ptr + off, data.len - off);
			if (partial != 0)
				ffstr_set(&in, data.ptr + off, ffmin(partial, data.len - off));
			off += in.len;
			break;

		case CUEREAD_UNKNOWN_KEY:
			x(0);
			break;

		default:
			xlog("cueread_process: %s  line %u", cueread_error(&c), cueread_line(&c));
			x(0);
		}
	}

end:
	cueread_close(&c);
}

void test_cue()
{
	ffstr data = {};
	ffstr_set(&data, cue_sample, sizeof(cue_sample)-1);
	cue_read(data, 0);
	cue_read(data, 3);
}
