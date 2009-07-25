/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Translate via the use of pseudo channels
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <sys/resource.h>
#include <math.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"

#define MAX_RECALC 1000 /* max sample recalc */

/*! \brief the list of translators */
static AST_RWLIST_HEAD_STATIC(translators, ast_translator);

struct translator_path {
	struct ast_translator *step;	/*!< Next step translator */
	unsigned int cost;		/*!< Complete cost to destination */
	unsigned int multistep;		/*!< Multiple conversions required for this translation */
};

/*! \brief a matrix that, for any pair of supported formats,
 * indicates the total cost of translation and the first step.
 * The full path can be reconstricted iterating on the matrix
 * until step->dstfmt == desired_format.
 *
 * Array indexes are 'src' and 'dest', in that order.
 *
 * Note: the lock in the 'translators' list is also used to protect
 * this structure.
 */
static struct translator_path tr_matrix[MAX_FORMAT][MAX_FORMAT];

/*! \todo
 * TODO: sample frames for each supported input format.
 * We build this on the fly, by taking an SLIN frame and using
 * the existing converter to play with it.
 */

/*! \brief returns the index of the lowest bit set */
static force_inline int powerof(unsigned int d)
{
	int x = ffs(d);

	if (x)
		return x - 1;

	ast_log(LOG_WARNING, "No bits set? %d\n", d);

	return -1;
}

/*
 * wrappers around the translator routines.
 */

/*!
 * \brief Allocate the descriptor, required outbuf space,
 * and possibly also plc and desc.
 */
static void *newpvt(struct ast_translator *t)
{
	struct ast_trans_pvt *pvt;
	int len;
	int useplc = t->plc_samples > 0 && t->useplc;	/* cache, because it can change on the fly */
	char *ofs;

	/*
	 * compute the required size adding private descriptor,
	 * plc, buffer, AST_FRIENDLY_OFFSET.
	 */
	len = sizeof(*pvt) + t->desc_size;
	if (useplc)
		len += sizeof(plc_state_t);
	if (t->buf_size)
		len += AST_FRIENDLY_OFFSET + t->buf_size;
	pvt = ast_calloc(1, len);
	if (!pvt)
		return NULL;
	pvt->t = t;
	ofs = (char *)(pvt + 1);	/* pointer to data space */
	if (t->desc_size) {		/* first comes the descriptor */
		pvt->pvt = ofs;
		ofs += t->desc_size;
	}
	if (useplc) {			/* then plc state */
		pvt->plc = (plc_state_t *)ofs;
		ofs += sizeof(plc_state_t);
	}
	if (t->buf_size)		/* finally buffer and header */
		pvt->outbuf.c = ofs + AST_FRIENDLY_OFFSET;
	/* call local init routine, if present */
	if (t->newpvt && t->newpvt(pvt)) {
		ast_free(pvt);
		return NULL;
	}
	ast_module_ref(t->module);
	return pvt;
}

static void destroy(struct ast_trans_pvt *pvt)
{
	struct ast_translator *t = pvt->t;

	if (ast_test_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR)) {
		/* If this flag is still set, that means that the translation path has
		 * been torn down, while we still have a frame out there being used.
		 * When ast_frfree() gets called on that frame, this ast_trans_pvt
		 * will get destroyed, too. */

		pvt->destroy = 1;

		return;
	}

	if (t->destroy)
		t->destroy(pvt);
	ast_free(pvt);
	ast_module_unref(t->module);
}

/*! \brief framein wrapper, deals with plc and bound checks.  */
static int framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int16_t *dst = pvt->outbuf.i16;
	int ret;
	int samples = pvt->samples;	/* initial value */
	
	/* Copy the last in jb timing info to the pvt */
	ast_copy_flags(&pvt->f, f, AST_FRFLAG_HAS_TIMING_INFO);
	pvt->f.ts = f->ts;
	pvt->f.len = f->len;
	pvt->f.seqno = f->seqno;

	if (f->samples == 0) {
		ast_log(LOG_WARNING, "no samples for %s\n", pvt->t->name);
	}
	if (pvt->t->buffer_samples) {	/* do not pass empty frames to callback */
		if (f->datalen == 0) { /* perform PLC with nominal framesize of 20ms/160 samples */
			if (pvt->plc) {
				int l = pvt->t->plc_samples;
				if (pvt->samples + l > pvt->t->buffer_samples) {
					ast_log(LOG_WARNING, "Out of buffer space\n");
					return -1;
				}
				l = plc_fillin(pvt->plc, dst + pvt->samples, l);
				pvt->samples += l;
				pvt->datalen = pvt->samples * 2;	/* SLIN has 2bytes for 1sample */
			}
			/* We don't want generic PLC. If the codec has native PLC, then do that */
			if (!pvt->t->native_plc)
				return 0;
		}
		if (pvt->samples + f->samples > pvt->t->buffer_samples) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
	}
	/* we require a framein routine, wouldn't know how to do
	 * it otherwise.
	 */
	ret = pvt->t->framein(pvt, f);
	/* possibly store data for plc */
	if (!ret && pvt->plc) {
		int l = pvt->t->plc_samples;
		if (pvt->samples < l)
			l = pvt->samples;
		plc_rx(pvt->plc, dst + pvt->samples - l, l);
	}
	/* diagnostic ... */
	if (pvt->samples == samples)
		ast_log(LOG_WARNING, "%s did not update samples %d\n",
			pvt->t->name, pvt->samples);
	return ret;
}

/*! \brief generic frameout routine.
 * If samples and datalen are 0, take whatever is in pvt
 * and reset them, otherwise take the values in the caller and
 * leave alone the pvt values.
 */
struct ast_frame *ast_trans_frameout(struct ast_trans_pvt *pvt,
	int datalen, int samples)
{
	struct ast_frame *f = &pvt->f;

	if (samples)
		f->samples = samples;
	else {
		if (pvt->samples == 0)
			return NULL;
		f->samples = pvt->samples;
		pvt->samples = 0;
	}
	if (datalen)
		f->datalen = datalen;
	else {
		f->datalen = pvt->datalen;
		pvt->datalen = 0;
	}

	f->frametype = AST_FRAME_VOICE;
	f->subclass = 1 << (pvt->t->dstfmt);
	f->mallocd = 0;
	f->offset = AST_FRIENDLY_OFFSET;
	f->src = pvt->t->name;
	f->data.ptr = pvt->outbuf.c;

	ast_set_flag(f, AST_FRFLAG_FROM_TRANSLATOR);

	return f;
}

static struct ast_frame *default_frameout(struct ast_trans_pvt *pvt)
{
	return ast_trans_frameout(pvt, 0, 0);
}

/* end of callback wrappers and helpers */

void ast_translator_free_path(struct ast_trans_pvt *p)
{
	struct ast_trans_pvt *pn = p;
	while ( (p = pn) ) {
		pn = p->next;
		destroy(p);
	}
}

/*! \brief Build a chain of translators based upon the given source and dest formats */
struct ast_trans_pvt *ast_translator_build_path(int dest, int source)
{
	struct ast_trans_pvt *head = NULL, *tail = NULL;
	
	source = powerof(source);
	dest = powerof(dest);

	if (source == -1 || dest == -1) {
		ast_log(LOG_WARNING, "No translator path: (%s codec is not valid)\n", source == -1 ? "starting" : "ending");
		return NULL;
	}

	AST_RWLIST_RDLOCK(&translators);

	while (source != dest) {
		struct ast_trans_pvt *cur;
		struct ast_translator *t = tr_matrix[source][dest].step;
		if (!t) {
			ast_log(LOG_WARNING, "No translator path from %s to %s\n", 
				ast_getformatname(source), ast_getformatname(dest));
			AST_RWLIST_UNLOCK(&translators);
			return NULL;
		}
		if (!(cur = newpvt(t))) {
			ast_log(LOG_WARNING, "Failed to build translator step from %d to %d\n", source, dest);
			if (head)
				ast_translator_free_path(head);	
			AST_RWLIST_UNLOCK(&translators);
			return NULL;
		}
		if (!head)
			head = cur;
		else
			tail->next = cur;
		tail = cur;
		cur->nextin = cur->nextout = ast_tv(0, 0);
		/* Keep going if this isn't the final destination */
		source = cur->t->dstfmt;
	}

	AST_RWLIST_UNLOCK(&translators);
	return head;
}

/*! \brief do the actual translation */
struct ast_frame *ast_translate(struct ast_trans_pvt *path, struct ast_frame *f, int consume)
{
	struct ast_trans_pvt *p = path;
	struct ast_frame *out = f;
	struct timeval delivery;
	int has_timing_info;
	long ts;
	long len;
	int seqno;

	has_timing_info = ast_test_flag(f, AST_FRFLAG_HAS_TIMING_INFO);
	ts = f->ts;
	len = f->len;
	seqno = f->seqno;

	/* XXX hmmm... check this below */
	if (!ast_tvzero(f->delivery)) {
		if (!ast_tvzero(path->nextin)) {
			/* Make sure this is in line with what we were expecting */
			if (!ast_tveq(path->nextin, f->delivery)) {
				/* The time has changed between what we expected and this
				   most recent time on the new packet.  If we have a
				   valid prediction adjust our output time appropriately */
				if (!ast_tvzero(path->nextout)) {
					path->nextout = ast_tvadd(path->nextout,
								  ast_tvsub(f->delivery, path->nextin));
				}
				path->nextin = f->delivery;
			}
		} else {
			/* This is our first pass.  Make sure the timing looks good */
			path->nextin = f->delivery;
			path->nextout = f->delivery;
		}
		/* Predict next incoming sample */
		path->nextin = ast_tvadd(path->nextin, ast_samp2tv(f->samples, ast_format_rate(f->subclass)));
	}
	delivery = f->delivery;
	for ( ; out && p ; p = p->next) {
		framein(p, out);
		if (out != f)
			ast_frfree(out);
		out = p->t->frameout(p);
	}
	if (consume)
		ast_frfree(f);
	if (out == NULL)
		return NULL;
	/* we have a frame, play with times */
	if (!ast_tvzero(delivery)) {
		/* Regenerate prediction after a discontinuity */
		if (ast_tvzero(path->nextout))
			path->nextout = ast_tvnow();

		/* Use next predicted outgoing timestamp */
		out->delivery = path->nextout;
		
		/* Predict next outgoing timestamp from samples in this
		   frame. */
		path->nextout = ast_tvadd(path->nextout, ast_samp2tv(out->samples, ast_format_rate(out->subclass)));
	} else {
		out->delivery = ast_tv(0, 0);
		ast_set2_flag(out, has_timing_info, AST_FRFLAG_HAS_TIMING_INFO);
		if (has_timing_info) {
			out->ts = ts;
			out->len = len;
			out->seqno = seqno;
		}
	}
	/* Invalidate prediction if we're entering a silence period */
	if (out->frametype == AST_FRAME_CNG)
		path->nextout = ast_tv(0, 0);
	return out;
}

/*! \brief compute the cost of a single translation step */
static void calc_cost(struct ast_translator *t, int seconds)
{
	int num_samples = 0;
	struct ast_trans_pvt *pvt;
	struct rusage start;
	struct rusage end;
	int cost;
	int out_rate = ast_format_rate(t->dstfmt);

	if (!seconds)
		seconds = 1;
	
	/* If they don't make samples, give them a terrible score */
	if (!t->sample) {
		ast_log(LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
		t->cost = 999999;
		return;
	}

	pvt = newpvt(t);
	if (!pvt) {
		ast_log(LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		t->cost = 999999;
		return;
	}

	getrusage(RUSAGE_SELF, &start);

	/* Call the encoder until we've processed the required number of samples */
	while (num_samples < seconds * out_rate) {
		struct ast_frame *f = t->sample();
		if (!f) {
			ast_log(LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
			destroy(pvt);
			t->cost = 999999;
			return;
		}
		framein(pvt, f);
		ast_frfree(f);
		while ((f = t->frameout(pvt))) {
			num_samples += f->samples;
			ast_frfree(f);
		}
	}

	getrusage(RUSAGE_SELF, &end);

	cost = ((end.ru_utime.tv_sec - start.ru_utime.tv_sec) * 1000000) + end.ru_utime.tv_usec - start.ru_utime.tv_usec;
	cost += ((end.ru_stime.tv_sec - start.ru_stime.tv_sec) * 1000000) + end.ru_stime.tv_usec - start.ru_stime.tv_usec;

	destroy(pvt);

	t->cost = cost / seconds;

	if (!t->cost)
		t->cost = 1;
}

/*!
 * \brief rebuild a translation matrix.
 * \note This function expects the list of translators to be locked
*/
static void rebuild_matrix(int samples)
{
	struct ast_translator *t;
	int x;      /* source format index */
	int y;      /* intermediate format index */
	int z;      /* destination format index */

	ast_debug(1, "Resetting translation matrix\n");

	memset(tr_matrix, '\0', sizeof(tr_matrix));

	/* first, compute all direct costs */
	AST_RWLIST_TRAVERSE(&translators, t, list) {
		if (!t->active)
			continue;

		x = t->srcfmt;
		z = t->dstfmt;

		if (samples)
			calc_cost(t, samples);
	  
		if (!tr_matrix[x][z].step || t->cost < tr_matrix[x][z].cost) {
			tr_matrix[x][z].step = t;
			tr_matrix[x][z].cost = t->cost;
		}
	}

	/*
	 * For each triple x, y, z of distinct formats, check if there is
	 * a path from x to z through y which is cheaper than what is
	 * currently known, and in case, update the matrix.
	 * Repeat until the matrix is stable.
	 */
	for (;;) {
		int changed = 0;
		for (x = 0; x < MAX_FORMAT; x++) {      /* source format */
			for (y = 0; y < MAX_FORMAT; y++) {    /* intermediate format */
				if (x == y)                     /* skip ourselves */
					continue;

				for (z = 0; z<MAX_FORMAT; z++) {  /* dst format */
					int newcost;

					if (z == x || z == y)       /* skip null conversions */
						continue;
					if (!tr_matrix[x][y].step)  /* no path from x to y */
						continue;
					if (!tr_matrix[y][z].step)  /* no path from y to z */
						continue;
					newcost = tr_matrix[x][y].cost + tr_matrix[y][z].cost;
					if (tr_matrix[x][z].step && newcost >= tr_matrix[x][z].cost)
						continue;               /* x->y->z is more expensive than
						                         * the existing path */
					/* ok, we can get from x to z via y with a cost that
					   is the sum of the transition from x to y and
					   from y to z */
						 
					tr_matrix[x][z].step = tr_matrix[x][y].step;
					tr_matrix[x][z].cost = newcost;
					tr_matrix[x][z].multistep = 1;
					ast_debug(3, "Discovered %d cost path from %s to %s, via %s\n", tr_matrix[x][z].cost,
						  ast_getformatname(1 << x), ast_getformatname(1 << z), ast_getformatname(1 << y));
					changed++;
				}
			}
		}
		if (!changed)
			break;
	}
}

static char *handle_cli_core_show_translation(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define SHOW_TRANS 16
	int x, y, z;
	int curlen = 0, longest = 0, magnitude[SHOW_TRANS] = { 0, };

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show translation [recalc]";
		e->usage =
			"Usage: core show translation [recalc [<recalc seconds>]]\n"
			"       Displays known codec translators and the cost associated\n"
			"       with each conversion.  If the argument 'recalc' is supplied along\n"
			"       with optional number of seconds to test a new test will be performed\n"
			"       as the chart is being displayed.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 5)
		return CLI_SHOWUSAGE;

	if (a->argv[3] && !strcasecmp(a->argv[3], "recalc")) {
		z = a->argv[4] ? atoi(a->argv[4]) : 1;

		if (z <= 0) {
			ast_cli(a->fd, "         Recalc must be greater than 0.  Defaulting to 1.\n");
			z = 1;
		}

		if (z > MAX_RECALC) {
			ast_cli(a->fd, "         Maximum limit of recalc exceeded by %d, truncating value to %d\n", z - MAX_RECALC, MAX_RECALC);
			z = MAX_RECALC;
		}
		ast_cli(a->fd, "         Recalculating Codec Translation (number of sample seconds: %d)\n\n", z);
		AST_RWLIST_WRLOCK(&translators);
		rebuild_matrix(z);
		AST_RWLIST_UNLOCK(&translators);
	} else if (a->argc > 3)
		return CLI_SHOWUSAGE;

	AST_RWLIST_RDLOCK(&translators);

	ast_cli(a->fd, "         Translation times between formats (in microseconds) for one second of data\n");
	ast_cli(a->fd, "          Source Format (Rows) Destination Format (Columns)\n\n");
	/* Get the length of the longest (usable?) codec name, so we know how wide the left side should be */
	for (x = 0; x < SHOW_TRANS; x++) {
		curlen = strlen(ast_getformatname(1 << (x)));
		if (curlen > longest)
			longest = curlen;
		for (y = 0; y < SHOW_TRANS; y++) {
			if (tr_matrix[x][y].cost > pow(10, magnitude[x])) {
				magnitude[y] = floor(log10(tr_matrix[x][y].cost));
			}
		}
	}
	for (x = -1; x < SHOW_TRANS; x++) {
		struct ast_str *out = ast_str_alloca(125);
		/*Go ahead and move to next iteration if dealing with an unknown codec*/
		if(x >= 0 && !strcmp(ast_getformatname(1 << (x)), "unknown"))
			continue;
		ast_str_set(&out, -1, " ");
		for (y = -1; y < SHOW_TRANS; y++) {
			/*Go ahead and move to next iteration if dealing with an unknown codec*/
			if (y >= 0 && !strcmp(ast_getformatname(1 << (y)), "unknown"))
				continue;
			if (y >= 0)
				curlen = strlen(ast_getformatname(1 << (y)));
			if (y >= 0 && magnitude[y] + 1 > curlen) {
				curlen = magnitude[y] + 1;
			}
			if (curlen < 5)
				curlen = 5;
			if (x >= 0 && y >= 0 && tr_matrix[x][y].step) {
				/* Actual codec output */
				ast_str_append(&out, -1, "%*d", curlen + 1, tr_matrix[x][y].cost);
			} else if (x == -1 && y >= 0) {
				/* Top row - use a dynamic size */
				ast_str_append(&out, -1, "%*s", curlen + 1, ast_getformatname(1 << (y)) );
			} else if (y == -1 && x >= 0) {
				/* Left column - use a static size. */
				ast_str_append(&out, -1, "%*s", longest, ast_getformatname(1 << (x)) );
			} else if (x >= 0 && y >= 0) {
				/* Codec not supported */
				ast_str_append(&out, -1, "%*s", curlen + 1, "-");
			} else {
				/* Upper left hand corner */
				ast_str_append(&out, -1, "%*s", longest, "");
			}
		}
		ast_str_append(&out, -1, "\n");
		ast_cli(a->fd, "%s", ast_str_buffer(out));
	}
	AST_RWLIST_UNLOCK(&translators);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_translate[] = {
	AST_CLI_DEFINE(handle_cli_core_show_translation, "Display translation matrix")
};

/*! \brief register codec translator */
int __ast_register_translator(struct ast_translator *t, struct ast_module *mod)
{
	static int added_cli = 0;
	struct ast_translator *u;
	char tmp[80];

	if (!mod) {
		ast_log(LOG_WARNING, "Missing module pointer, you need to supply one\n");
		return -1;
	}

	if (!t->buf_size) {
		ast_log(LOG_WARNING, "empty buf size, you need to supply one\n");
		return -1;
	}

	t->module = mod;

	t->srcfmt = powerof(t->srcfmt);
	t->dstfmt = powerof(t->dstfmt);
	t->active = 1;

	if (t->srcfmt == -1 || t->dstfmt == -1) {
		ast_log(LOG_WARNING, "Invalid translator path: (%s codec is not valid)\n", t->srcfmt == -1 ? "starting" : "ending");
		return -1;
	}
	if (t->plc_samples) {
		if (t->buffer_samples < t->plc_samples) {
			ast_log(LOG_WARNING, "plc_samples %d buffer_samples %d\n",
				t->plc_samples, t->buffer_samples);
			return -1;
		}
		if (t->dstfmt != powerof(AST_FORMAT_SLINEAR))
			ast_log(LOG_WARNING, "plc_samples %d format %x\n",
				t->plc_samples, t->dstfmt);
	}
	if (t->srcfmt >= MAX_FORMAT) {
		ast_log(LOG_WARNING, "Source format %s is larger than MAX_FORMAT\n", ast_getformatname(t->srcfmt));
		return -1;
	}

	if (t->dstfmt >= MAX_FORMAT) {
		ast_log(LOG_WARNING, "Destination format %s is larger than MAX_FORMAT\n", ast_getformatname(t->dstfmt));
		return -1;
	}

	if (t->buf_size) {
		/*
		 * Align buf_size properly, rounding up to the machine-specific
		 * alignment for pointers.
		 */
		struct _test_align { void *a, *b; } p;
		int align = (char *)&p.b - (char *)&p.a;

		t->buf_size = ((t->buf_size + align - 1) / align) * align;
	}

	if (t->frameout == NULL)
		t->frameout = default_frameout;
  
	calc_cost(t, 1);

	ast_verb(2, "Registered translator '%s' from format %s to %s, cost %d\n",
			    term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)),
			    ast_getformatname(1 << t->srcfmt), ast_getformatname(1 << t->dstfmt), t->cost);

	if (!added_cli) {
		ast_cli_register_multiple(cli_translate, ARRAY_LEN(cli_translate));
		added_cli++;
	}

	AST_RWLIST_WRLOCK(&translators);

	/* find any existing translators that provide this same srcfmt/dstfmt,
	   and put this one in order based on cost */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&translators, u, list) {
		if ((u->srcfmt == t->srcfmt) &&
		    (u->dstfmt == t->dstfmt) &&
		    (u->cost > t->cost)) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(t, list);
			t = NULL;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	/* if no existing translator was found for this format combination,
	   add it to the beginning of the list */
	if (t)
		AST_RWLIST_INSERT_HEAD(&translators, t, list);

	rebuild_matrix(0);

	AST_RWLIST_UNLOCK(&translators);

	return 0;
}

/*! \brief unregister codec translator */
int ast_unregister_translator(struct ast_translator *t)
{
	char tmp[80];
	struct ast_translator *u;
	int found = 0;

	AST_RWLIST_WRLOCK(&translators);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&translators, u, list) {
		if (u == t) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_verb(2, "Unregistered translator '%s' from format %s to %s\n", term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), ast_getformatname(1 << t->srcfmt), ast_getformatname(1 << t->dstfmt));
			found = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (found)
		rebuild_matrix(0);

	AST_RWLIST_UNLOCK(&translators);

	return (u ? 0 : -1);
}

void ast_translator_activate(struct ast_translator *t)
{
	AST_RWLIST_WRLOCK(&translators);
	t->active = 1;
	rebuild_matrix(0);
	AST_RWLIST_UNLOCK(&translators);
}

void ast_translator_deactivate(struct ast_translator *t)
{
	AST_RWLIST_WRLOCK(&translators);
	t->active = 0;
	rebuild_matrix(0);
	AST_RWLIST_UNLOCK(&translators);
}

/*! \brief Calculate our best translator source format, given costs, and a desired destination */
int ast_translator_best_choice(int *dst, int *srcs)
{
	int x,y;
	int best = -1;
	int bestdst = 0;
	int cur, cursrc;
	int besttime = INT_MAX;
	int beststeps = INT_MAX;
	int common = ((*dst) & (*srcs)) & AST_FORMAT_AUDIO_MASK;	/* are there common formats ? */

	if (common) { /* yes, pick one and return */
		for (cur = 1, y = 0; y <= MAX_AUDIO_FORMAT; cur <<= 1, y++) {
			if (cur & common)	/* guaranteed to find one */
				break;
		}
		/* We are done, this is a common format to both. */
		*srcs = *dst = cur;
		return 0;
	} else {	/* No, we will need to translate */
		AST_RWLIST_RDLOCK(&translators);
		for (cur = 1, y = 0; y <= MAX_AUDIO_FORMAT; cur <<= 1, y++) {
			if (! (cur & *dst))
				continue;
			for (cursrc = 1, x = 0; x <= MAX_AUDIO_FORMAT; cursrc <<= 1, x++) {
				if (!(*srcs & cursrc) || !tr_matrix[x][y].step ||
				    tr_matrix[x][y].cost >  besttime)
					continue;	/* not existing or no better */
				if (tr_matrix[x][y].cost < besttime ||
				    tr_matrix[x][y].multistep < beststeps) {
					/* better than what we have so far */
					best = cursrc;
					bestdst = cur;
					besttime = tr_matrix[x][y].cost;
					beststeps = tr_matrix[x][y].multistep;
				}
			}
		}
		AST_RWLIST_UNLOCK(&translators);
		if (best > -1) {
			*srcs = best;
			*dst = bestdst;
			best = 0;
		}
		return best;
	}
}

unsigned int ast_translate_path_steps(unsigned int dest, unsigned int src)
{
	unsigned int res = -1;

	/* convert bitwise format numbers into array indices */
	src = powerof(src);
	dest = powerof(dest);

	if (src == -1 || dest == -1) {
		ast_log(LOG_WARNING, "No translator path: (%s codec is not valid)\n", src == -1 ? "starting" : "ending");
		return -1;
	}
	AST_RWLIST_RDLOCK(&translators);

	if (tr_matrix[src][dest].step)
		res = tr_matrix[src][dest].multistep + 1;

	AST_RWLIST_UNLOCK(&translators);

	return res;
}

unsigned int ast_translate_available_formats(unsigned int dest, unsigned int src)
{
	unsigned int res = dest;
	unsigned int x;
	unsigned int src_audio = src & AST_FORMAT_AUDIO_MASK;
	unsigned int src_video = src & AST_FORMAT_VIDEO_MASK;

	/* if we don't have a source format, we just have to try all
	   possible destination formats */
	if (!src)
		return dest;

	/* If we have a source audio format, get its format index */
	if (src_audio)
		src_audio = powerof(src_audio);

	/* If we have a source video format, get its format index */
	if (src_video)
		src_video = powerof(src_video);

	AST_RWLIST_RDLOCK(&translators);

	/* For a given source audio format, traverse the list of
	   known audio formats to determine whether there exists
	   a translation path from the source format to the
	   destination format. */
	for (x = 1; src_audio && (x & AST_FORMAT_AUDIO_MASK); x <<= 1) {
		/* if this is not a desired format, nothing to do */
		if ((!dest) & x)
			continue;

		/* if the source is supplying this format, then
		   we can leave it in the result */
		if (src & x)
			continue;

		/* if we don't have a translation path from the src
		   to this format, remove it from the result */
		if (!tr_matrix[src_audio][powerof(x)].step) {
			res &= ~x;
			continue;
		}

		/* now check the opposite direction */
		if (!tr_matrix[powerof(x)][src_audio].step)
			res &= ~x;
	}

	/* For a given source video format, traverse the list of
	   known video formats to determine whether there exists
	   a translation path from the source format to the
	   destination format. */
	for (; src_video && (x & AST_FORMAT_VIDEO_MASK); x <<= 1) {
		/* if this is not a desired format, nothing to do */
		if ((!dest) & x)
			continue;

		/* if the source is supplying this format, then
		   we can leave it in the result */
		if (src & x)
			continue;

		/* if we don't have a translation path from the src
		   to this format, remove it from the result */
		if (!tr_matrix[src_video][powerof(x)].step) {
			res &= ~x;
			continue;
		}

		/* now check the opposite direction */
		if (!tr_matrix[powerof(x)][src_video].step)
			res &= ~x;
	}

	AST_RWLIST_UNLOCK(&translators);

	return res;
}

void ast_translate_frame_freed(struct ast_frame *fr)
{
	struct ast_trans_pvt *pvt;

	ast_clear_flag(fr, AST_FRFLAG_FROM_TRANSLATOR);

	pvt = (struct ast_trans_pvt *) (((char *) fr) - offsetof(struct ast_trans_pvt, f));

	if (!pvt->destroy)
		return;
	
	destroy(pvt);
}
