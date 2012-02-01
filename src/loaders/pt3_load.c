/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "load.h"
#include "mod.h"
#include "iff.h"

#define MAGIC_FORM	MAGIC4('F','O','R','M')
#define MAGIC_MODL	MAGIC4('M','O','D','L')

static int pt3_test(FILE *, char *, const int);
static int pt3_load(struct xmp_context *, FILE *, const int);
static int ptdt_load(struct xmp_context *, FILE *, const int);

struct xmp_loader_info pt3_loader = {
	"PT3",
	"Protracker 3",
	pt3_test,
	pt3_load
};

static int pt3_test(FILE *f, char *t, const int start)
{
	uint32 form, id;

	form = read32b(f);
	read32b(f);
	id = read32b(f);

	if (form != MAGIC_FORM || id != MAGIC_MODL)
		return -1;

	read_title(f, t, 0);

	return 0;
}

#define PT3_FLAG_CIA	0x0001	/* VBlank if not set */
#define PT3_FLAG_FILTER	0x0002	/* Filter status */
#define PT3_FLAG_SONG	0x0004	/* Modules have this bit unset */
#define PT3_FLAG_IRQ	0x0008	/* Soft IRQ */
#define PT3_FLAG_VARPAT	0x0010	/* Variable pattern length */
#define PT3_FLAG_8VOICE	0x0020	/* 4 voices if not set */
#define PT3_FLAG_16BIT	0x0040	/* 8 bit samples if not set */
#define PT3_FLAG_RAWPAT	0x0080	/* Packed patterns if not set */


static void get_info(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &p->m;
	int flags;
	int day, month, year, hour, min, sec;
	int dhour, dmin, dsec;

	fread(m->name, 1, 32, f);
	m->xxh->ins = read16b(f);
	m->xxh->len = read16b(f);
	m->xxh->pat = read16b(f);
	m->xxh->gvl = read16b(f);
	m->xxh->bpm = read16b(f);
	flags = read16b(f);
	day   = read16b(f);
	month = read16b(f);
	year  = read16b(f);
	hour  = read16b(f);
	min   = read16b(f);
	sec   = read16b(f);
	dhour = read16b(f);
	dmin  = read16b(f);
	dsec  = read16b(f);

	MODULE_INFO();

	_D(_D_INFO "Creation date: %02d/%02d/%02d %02d:%02d:%02d",
		       day, month, year, hour, min, sec);
	_D(_D_INFO "Playing time: %02d:%02d:%02d", dhour, dmin, dsec);
}

static void get_cmnt(struct xmp_context *ctx, int size, FILE *f)
{
	_D(_D_INFO "Comment size: %d", size);
}

static void get_ptdt(struct xmp_context *ctx, int size, FILE *f)
{
	ptdt_load(ctx, f, 0);
}

static int pt3_load(struct xmp_context *ctx, FILE *f, const int start)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &p->m;
	char buf[20];

	LOAD_INIT();

	read32b(f);		/* FORM */
	read32b(f);		/* size */
	read32b(f);		/* MODL */

	read32b(f);		/* VERS */
	read32b(f);		/* VERS size */

	fread(buf, 1, 10, f);
	set_type(m, "%-6.6s (Protracker IFFMODL)", buf + 4);

	/* IFF chunk IDs */
	iff_register("INFO", get_info);
	iff_register("CMNT", get_cmnt);
	iff_register("PTDT", get_ptdt);

	iff_setflag(IFF_FULL_CHUNK_SIZE);

	/* Load IFF chunks */
	while (!feof(f))
		iff_chunk(ctx, f);

	iff_release();

	return 0;
}

static int ptdt_load(struct xmp_context *ctx, FILE *f, const int start)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &p->m;
	int i, j;
	struct xxm_event *event;
	struct mod_header mh;
	uint8 mod_event[4];

	fread(&mh.name, 20, 1, f);
	for (i = 0; i < 31; i++) {
		fread(&mh.ins[i].name, 22, 1, f);
		mh.ins[i].size = read16b(f);
		mh.ins[i].finetune = read8(f);
		mh.ins[i].volume = read8(f);
		mh.ins[i].loop_start = read16b(f);
		mh.ins[i].loop_size = read16b(f);
	}
	mh.len = read8(f);
	mh.restart = read8(f);
	fread(&mh.order, 128, 1, f);
	fread(&mh.magic, 4, 1, f);

	m->xxh->ins = 31;
	m->xxh->smp = m->xxh->ins;
	m->xxh->chn = 4;
	m->xxh->len = mh.len;
	m->xxh->rst = mh.restart;
	memcpy(m->xxo, mh.order, 128);

	for (i = 0; i < 128; i++) {
		if (m->xxo[i] > m->xxh->pat)
			m->xxh->pat = m->xxo[i];
	}

	m->xxh->pat++;
	m->xxh->trk = m->xxh->chn * m->xxh->pat;

	INSTRUMENT_INIT();

	for (i = 0; i < m->xxh->ins; i++) {
		m->xxih[i].sub = calloc(sizeof (struct xxm_subinstrument), 1);
		m->xxs[i].len = 2 * mh.ins[i].size;
		m->xxs[i].lps = 2 * mh.ins[i].loop_start;
		m->xxs[i].lpe = m->xxs[i].lps + 2 * mh.ins[i].loop_size;
		m->xxs[i].flg = mh.ins[i].loop_size > 1 ? XMP_SAMPLE_LOOP : 0;
		if (m->xxs[i].flg & XMP_SAMPLE_LOOP) {
			if (m->xxs[i].len == 0 && m->xxs[i].len > m->xxs[i].lpe)
				m->xxs[i].flg |= XMP_SAMPLE_LOOP_FULL;
		}
		m->xxih[i].sub[0].fin = (int8)(mh.ins[i].finetune << 4);
		m->xxih[i].sub[0].vol = mh.ins[i].volume;
		m->xxih[i].sub[0].pan = 0x80;
		m->xxih[i].sub[0].sid = i;
		m->xxih[i].nsm = !!(m->xxs[i].len);
		m->xxih[i].rls = 0xfff;

		copy_adjust(m->xxih[i].name, mh.ins[i].name, 22);

		_D(_D_INFO "[%2X] %-22.22s %04x %04x %04x %c V%02x %+d %c",
				i, m->xxih[i].name,
				m->xxs[i].len, m->xxs[i].lps,
				m->xxs[i].lpe,
				mh.ins[i].loop_size > 1 ? 'L' : ' ',
				m->xxih[i].sub[0].vol,
				m->xxih[i].sub[0].fin >> 4,
				m->xxs[i].flg & XMP_SAMPLE_LOOP_FULL ? '!' : ' ');
	}

	PATTERN_INIT();

	/* Load and convert patterns */
	_D(_D_INFO "Stored patterns: %d", m->xxh->pat);

	for (i = 0; i < m->xxh->pat; i++) {
		PATTERN_ALLOC(i);
		m->xxp[i]->rows = 64;
		TRACK_ALLOC(i);
		for (j = 0; j < (64 * 4); j++) {
			event = &EVENT(i, j % 4, j / 4);
			fread(mod_event, 1, 4, f);
			cvt_pt_event(event, mod_event);
		}
	}

	m->xxh->flg |= XXM_FLG_MODRNG;

	/* Load samples */
	_D(_D_INFO "Stored samples: %d", m->xxh->smp);

	for (i = 0; i < m->xxh->smp; i++) {
		if (!m->xxs[i].len)
			continue;
		xmp_drv_loadpatch(ctx, f, m->xxih[i].sub[0].sid, 0,
				  &m->xxs[m->xxih[i].sub[0].sid], NULL);
	}

	return 0;
}
