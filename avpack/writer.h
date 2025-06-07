/** avpack: audio format writer
2025, Simon Zolin */

/*
avpk_create avpk_writer_close
avpk_tag
avpk_write
*/

#pragma once
#include <avpack/decl.h>
#include <ffbase/stringz.h>


/** Get writer interface by file extension */
static inline const struct avpkw_if* avpk_writer_find(const char *ext, const struct avpkw_if *const wifs[], unsigned n)
{
	for (unsigned i = 0;  i < n;  i++) {
		const char *ext2 = wifs[i]->ext + ffsz_len(wifs[i]->ext) + 1;
		if (ffsz_ieq(ext, wifs[i]->ext)
			|| (*ext2 && ffsz_ieq(ext, ext2)))
			return wifs[i];
	}
	return NULL;
}

typedef struct avpk_writer avpk_writer;
struct avpk_writer {
	void *ctx;
	struct avpkw_if ifa;
};

struct avpk_writer_conf {
	struct avpk_info info;
};

static inline int avpk_create(avpk_writer *w, const struct avpkw_if *wif, struct avpk_writer_conf *c)
{
	if (!wif)
		return 1;

	w->ifa = *wif;
	w->ctx = ffmem_zalloc(w->ifa.context_size);
	int r;
	if ((r = w->ifa.create(w->ctx, &c->info)))
		return r;
	return 0;
}

static inline void avpk_writer_close(avpk_writer *w)
{
	if (w->ifa.close)
		w->ifa.close(w->ctx);
	ffmem_free(w->ctx);
}

static inline int avpk_tag(avpk_writer *w, unsigned id, ffstr name, ffstr val)
{
	if (!w->ifa.tag_add)
		return 1;

	return w->ifa.tag_add(w->ctx, id, name, val);
}

static inline enum AVPK_R avpk_write(avpk_writer *w, struct avpk_frame *frame, unsigned flags, union avpk_write_result *res)
{
	return w->ifa.process(w->ctx, frame, flags, res);
}
