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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"

#define MAX_RECALC 200 /* max sample recalc */

/*! \brief the list of translators */
static AST_LIST_HEAD_STATIC(translators, ast_translator);

struct translator_path {
	struct ast_translator *step;	/*!< Next step translator */
	unsigned int cost;		/*!< Complete cost to destination */
	unsigned int multistep;		/*!< Multiple conversions required for this translation */
};

/*! \brief a matrix that, for any pair of supported formats,
 * indicates the total cost of translation and the first step.
 * The full path can be reconstricted iterating on the matrix
 * until step->dstfmt == desired_format.
 */
static struct translator_path tr_matrix[MAX_FORMAT][MAX_FORMAT];

/*
 * TODO: sample frames for each supported input format.
 * We build this on the fly, by taking an SLIN frame and using
 * the existing converter to play with it.
 */

/* returns the index of the lowest bit set */
static int powerof(int d)
{
	int x;
	for (x = 0; x < MAX_FORMAT; x++)
		if ((1 << x) & d)
			return x;
	ast_log(LOG_WARNING, "Powerof %d: No power??\n", d);
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
		pvt->outbuf = ofs + AST_FRIENDLY_OFFSET;
	/* call local init routine, if present */
	if (t->newpvt && t->newpvt(pvt) == NULL) {
		free(pvt);
		return NULL;
	}
	ast_mutex_lock(&t->lockp->lock);
	t->lockp->usecnt++;
	ast_mutex_unlock(&t->lockp->lock);
	ast_update_use_count();
	return pvt;
}

static void destroy(struct ast_trans_pvt *pvt)
{
	struct ast_translator *t = pvt->t;

	if (t->destroy)
		t->destroy(pvt);
	free(pvt);
	ast_mutex_lock(&t->lockp->lock);
	t->lockp->usecnt--;
	ast_mutex_unlock(&t->lockp->lock);
	ast_update_use_count();
}

/*
 * framein wrapper, deals with plc and bound checks.
 */
static int framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	int16_t *dst = (int16_t *)pvt->outbuf;
	int ret;
	int samples = pvt->samples;	/* initial value */

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
			}
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

/*
 * generic frameout routine.
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
	f->data = pvt->outbuf;
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

/*! Build a chain of translators based upon the given source and dest formats */
struct ast_trans_pvt *ast_translator_build_path(int dest, int source)
{
	struct ast_trans_pvt *head = NULL, *tail = NULL;
	
	source = powerof(source);
	dest = powerof(dest);
	
	while (source != dest) {
		struct ast_trans_pvt *cur;
		struct ast_translator *t = tr_matrix[source][dest].step;
		if (!t) {
			ast_log(LOG_WARNING, "No translator path from %s to %s\n", 
				ast_getformatname(source), ast_getformatname(dest));
			return NULL;
		}
		if (!(cur = newpvt(t))) {
			ast_log(LOG_WARNING, "Failed to build translator step from %d to %d\n", source, dest);
			if (head)
				ast_translator_free_path(head);	
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
	return head;
}

/*! \brief do the actual translation */
struct ast_frame *ast_translate(struct ast_trans_pvt *path, struct ast_frame *f, int consume)
{
	struct ast_trans_pvt *p = path;
	struct ast_frame *out = f;
	struct timeval delivery;

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
		path->nextin = ast_tvadd(path->nextin, ast_samp2tv(f->samples, 8000));
	}
	delivery = f->delivery;
	for ( ; out && p ; p = p->next) {
		framein(p, out);
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
		path->nextout = ast_tvadd(path->nextout, ast_samp2tv( out->samples, 8000));
	} else {
		out->delivery = ast_tv(0, 0);
	}
	/* Invalidate prediction if we're entering a silence period */
	if (out->frametype == AST_FRAME_CNG)
		path->nextout = ast_tv(0, 0);
	return out;
}

/*! \brief compute the cost of a single translation step */
static void calc_cost(struct ast_translator *t, int seconds)
{
	int sofar=0;
	struct ast_trans_pvt *pvt;
	struct timeval start;
	int cost;

	if (!seconds)
		seconds = 1;
	
	/* If they don't make samples, give them a terrible score */
	if (!t->sample) {
		ast_log(LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
		t->cost = 99999;
		return;
	}
	pvt = newpvt(t);
	if (!pvt) {
		ast_log(LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		t->cost = 99999;
		return;
	}
	start = ast_tvnow();
	/* Call the encoder until we've processed the required number of samples */
	while (sofar < seconds * 8000) {
		struct ast_frame *f = t->sample();
		if (!f) {
			ast_log(LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
			destroy(pvt);
			t->cost = 99999;
			return;
		}
		framein(pvt, f);
		ast_frfree(f);
		while( (f = t->frameout(pvt))) {
			sofar += f->samples;
			ast_frfree(f);
		}
	}
	cost = ast_tvdiff_ms(ast_tvnow(), start);
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
	int x;	/* source format index */
	int y;	/* intermediate format index */
	int z;	/* destination format index */

	if (option_debug)
		ast_log(LOG_DEBUG, "Resetting translation matrix\n");

	bzero(tr_matrix, sizeof(tr_matrix));
	/* first, compute all direct costs */
	AST_LIST_TRAVERSE(&translators, t, list) {
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
		for (x=0; x < MAX_FORMAT; x++) {			/* source format */
			for (y=0; y < MAX_FORMAT; y++) {	/* intermediate format */
				if (x == y) 			/* skip ourselves */
					continue;

				for (z=0; z<MAX_FORMAT; z++) {	/* dst format */
					int newcost;

					if (z == x || z == y)	/* skip null conversions */
						continue;
					if (!tr_matrix[x][y].step)	/* no path from x to y */
						continue;
					if (!tr_matrix[y][z].step)	/* no path from y to z */
						continue;
					newcost = tr_matrix[x][y].cost + tr_matrix[y][z].cost;
					if (tr_matrix[x][z].step && newcost >= tr_matrix[x][z].cost)
						continue;	/* x->y->z is more expensive than
								 * the existing path */
					/* ok, we can get from x to z via y with a cost that
					   is the sum of the transition from x to y and
					   from y to z */
						 
					tr_matrix[x][z].step = tr_matrix[x][y].step;
					tr_matrix[x][z].cost = newcost;
					tr_matrix[x][z].multistep = 1;
					if (option_debug)
						ast_log(LOG_DEBUG, "Discovered %d cost path from %s to %s, via %d\n", tr_matrix[x][z].cost, ast_getformatname(x), ast_getformatname(z), y);
					changed++;
				}
			}
		}
		if (!changed)
			break;
	}
}

/*! \brief CLI "show translation" command handler */
static int show_translation(int fd, int argc, char *argv[])
{
#define SHOW_TRANS 11
	int x, y, z;

	if (argc > 4) 
		return RESULT_SHOWUSAGE;

	AST_LIST_LOCK(&translators);	
	
	if (argv[2] && !strcasecmp(argv[2],"recalc")) {
		z = argv[3] ? atoi(argv[3]) : 1;

		if (z <= 0) {
			ast_cli(fd,"         C'mon let's be serious here... defaulting to 1.\n");
			z = 1;
		}

		if (z > MAX_RECALC) {
			ast_cli(fd,"         Maximum limit of recalc exceeded by %d, truncating value to %d\n",z-MAX_RECALC,MAX_RECALC);
			z = MAX_RECALC;
		}
		ast_cli(fd,"         Recalculating Codec Translation (number of sample seconds: %d)\n\n",z);
		rebuild_matrix(z);
	}

	ast_cli(fd, "         Translation times between formats (in milliseconds)\n");
	ast_cli(fd, "          Source Format (Rows) Destination Format(Columns)\n\n");
	for (x = -1; x < SHOW_TRANS; x++) {
		char line[80];
		char *buf = line;
		size_t left = sizeof(line) - 1;	/* one initial space */
		/* next 2 lines run faster than using ast_build_string() */
		*buf++ = ' ';
		*buf = '\0';
		for (y=-1;y<SHOW_TRANS;y++) {
			if (x >= 0 && y >= 0 && tr_matrix[x][y].step)	/* XXX what is 99999 ? */
				ast_build_string(&buf, &left, " %5d", tr_matrix[x][y].cost >= 99999 ? 0 : tr_matrix[x][y].cost);
			else if (((x == -1 && y >= 0) || (y == -1 && x >= 0))) {
				ast_build_string(&buf, &left, " %5s", ast_getformatname(1<<(x+y+1)) );
			} else if (x != -1 && y != -1) {
				ast_build_string(&buf, &left, "     -");
			} else {
				ast_build_string(&buf, &left, "      ");
			}
		}
		ast_build_string(&buf, &left, "\n");
		ast_cli(fd, line);			
	}
	AST_LIST_UNLOCK(&translators);
	return RESULT_SUCCESS;
}


static char show_trans_usage[] =
"Usage: show translation [recalc] [<recalc seconds>]\n"
"       Displays known codec translators and the cost associated\n"
"with each conversion.  If the argument 'recalc' is supplied along\n"
"with optional number of seconds to test a new test will be performed\n"
"as the chart is being displayed.\n";

static struct ast_cli_entry show_trans =
{ { "show", "translation", NULL }, show_translation, "Display translation matrix", show_trans_usage };

int ast_register_translator(struct ast_translator *t)
{
	static int added_cli = 0;

	if (t->lockp == NULL) {
		ast_log(LOG_WARNING, "Missing lock pointer, you need to supply one\n");
		return -1;
	}
	if (t->buf_size == 0) {
		ast_log(LOG_WARNING, "empty buf size, you need to supply one\n");
		return -1;
	}
	if (t->plc_samples) {
		if (t->buffer_samples < t->plc_samples) {
			ast_log(LOG_WARNING, "plc_samples %d buffer_samples %d\n",
				t->plc_samples, t->buffer_samples);
			return -1;
		}
		if (t->dstfmt != AST_FORMAT_SLINEAR)
			ast_log(LOG_WARNING, "plc_samples %d format %x\n",
				t->plc_samples, t->dstfmt);
	}
	t->srcfmt = powerof(t->srcfmt);
	t->dstfmt = powerof(t->dstfmt);
	/* XXX maybe check that it is not existing yet ? */
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
		t->buf_size = ((t->buf_size + align - 1)/align)*align;
	}
	if (t->lockp->usecnt < 0) {	/* XXX need to initialize the lock */
		ast_mutex_init(&t->lockp->lock);
		t->lockp->usecnt = 0;
	}
	if (t->frameout == NULL)
		t->frameout = default_frameout;
  
	calc_cost(t,1);
	if (option_verbose > 1) {
		char tmp[80];
		ast_verbose(VERBOSE_PREFIX_2 "Registered translator '%s' from format %s to %s, cost %d\n",
			term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)),
			ast_getformatname(1 << t->srcfmt), ast_getformatname(1 << t->dstfmt), t->cost);
	}
	AST_LIST_LOCK(&translators);
	if (!added_cli) {
		ast_cli_register(&show_trans);
		added_cli++;
	}
	AST_LIST_INSERT_HEAD(&translators, t, list);
	rebuild_matrix(0);
	AST_LIST_UNLOCK(&translators);
	return 0;
}

/*! \brief unregister codec translator */
int ast_unregister_translator(struct ast_translator *t)
{
	char tmp[80];
	struct ast_translator *u;
	AST_LIST_LOCK(&translators);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&translators, u, list) {
		if (u == t) {
			AST_LIST_REMOVE_CURRENT(&translators, list);
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Unregistered translator '%s' from format %s to %s\n", term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), ast_getformatname(1 << t->srcfmt), ast_getformatname(1 << t->dstfmt));
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	rebuild_matrix(0);
	AST_LIST_UNLOCK(&translators);
	return (u ? 0 : -1);
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
	int common = (*dst) & (*srcs);	/* are there common formats ? */

	if (common) { /* yes, pick one and return */
		for (cur = 1, y=0; y < MAX_FORMAT; cur <<=1, y++) {
			if (cur & common)	/* guaranteed to find one */
				break;
		}
		/* We are done, this is a common format to both. */
		*srcs = *dst  = cur;
		return 0;
	} else {	/* No, we will need to translate */
		AST_LIST_LOCK(&translators);
		for (cur = 1, y=0; y < MAX_FORMAT; cur <<=1, y++) {
			if (! (cur & *dst))
				continue;
			for (cursrc = 1, x=0; x < MAX_FORMAT; cursrc <<= 1, x++) {
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
		AST_LIST_UNLOCK(&translators);
		if (best > -1) {
			*srcs = best;
			*dst = bestdst;
			best = 0;
		}
		return best;
	}
}
