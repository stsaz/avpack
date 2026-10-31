/** avpack: .ts substream reader
2026, Simon Zolin */

#pragma once
#include <avpack/aac-read.h>
#include <avpack/mp3-read.h>
#include <avpack/ts-read.h>

#define TSCR_SCAN_MAX  (1*1024*1024)

typedef struct tscr {
	tsread tsr;

	ffuint64 trk_pkt_last_off;
	ffstr data_pending;
	ffuint conf_flags;
	ffuint trk_id;
	const struct avpkr_if *substream_if;
	void *substream_ctx;
	ffuint hdr_done :1;
	ffuint substream_pending :1;
} tscr;

static inline void tscr_open(tscr *tc, struct avpk_reader_conf *conf)
{
	tsread_open(&tc->tsr, conf->total_size);
	tc->tsr.log = conf->log;
	tc->tsr.udata = conf->opaque;
	tc->conf_flags = conf->flags;
}

static inline void tscr_close(tscr *tc)
{
	tsread_close(&tc->tsr);
	if (tc->substream_ctx) {
		tc->substream_if->close(tc->substream_ctx);
		ffmem_free(tc->substream_ctx);
	}
}

static inline int tscr_process(tscr *tc, ffstr *input, union avpk_read_result *res)
{
	tsread *t = &tc->tsr;
	int r;
	for (;;) {
		if (tc->substream_pending) {
			r = tc->substream_if->process(tc->substream_ctx, &tc->data_pending, res);
			switch (r) {
			case AVPK_MORE:
				break;
			default:
				return r;
			}
			tc->substream_pending = 0;
		}

		r = tsread_process(t, input, &tc->data_pending);
		switch (r) {
		case AVPK_DATA:
			if (!tc->hdr_done) {
				// Note: treat the 1st track as main
				tc->hdr_done = 1;
				tc->trk_id = t->cur->pid;
				switch (t->cur->stream_type) {
				case TS_STREAM_AUDIO_MP3:
					tc->substream_if = &avpk_mp3;  break;
				case TS_STREAM_AUDIO_AAC:
					tc->substream_if = &avpk_aac;  break;
				default:
					res->error.message = "codec not supported";
					res->error.offset = t->off;
					return AVPK_ERROR;
				}

				if (!(tc->substream_ctx = ffmem_zalloc(tc->substream_if->context_size))) {
					res->error.message = "no memory";
					res->error.offset = t->off;
					return AVPK_ERROR;
				}

				struct avpk_reader_conf c = {
					.flags = tc->conf_flags,
				};
				tc->substream_if->open(tc->substream_ctx, &c);
			}

			if (tc->trk_id != t->cur->pid) {
				if (t->off >= tc->trk_pkt_last_off + TSCR_SCAN_MAX) {
					res->error.message = "packet scan limit reached";
					res->error.offset = t->off;
					return AVPK_ERROR;
				}
				continue; // The packet is for another track
			}

			tc->trk_pkt_last_off = t->off;
			tc->substream_pending = 1;
			continue;

		case AVPK_SEEK:
			res->seek_offset = t->pkt.off;
			break;

		case AVPK_ERROR:
		case AVPK_WARNING:
			res->error.message = t->error;
			res->error.offset = t->pkt.off;
			break;
		}
		return r;
	}
}

AVPKR_IF_INIT(avpk_ts, "ts", AVPKF_TS, tscr, tscr_open, tscr_process, NULL, tscr_close);
