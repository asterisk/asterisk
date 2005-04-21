/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate via the use of pseudo channels
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_RECALC 200 /* max sample recalc */

/* This could all be done more efficiently *IF* we chained packets together
   by default, but it would also complicate virtually every application. */
   
AST_MUTEX_DEFINE_STATIC(list_lock);
static struct ast_translator *list = NULL;

struct ast_translator_dir {
	struct ast_translator *step;	/* Next step translator */
	int cost;						/* Complete cost to destination */
};

struct ast_frame_delivery {
	struct ast_frame *f;
	struct ast_channel *chan;
	int fd;
	struct translator_pvt *owner;
	struct ast_frame_delivery *prev;
	struct ast_frame_delivery *next;
};

static struct ast_translator_dir tr_matrix[MAX_FORMAT][MAX_FORMAT];

struct ast_trans_pvt {
	struct ast_translator *step;
	struct ast_translator_pvt *state;
	struct ast_trans_pvt *next;
	struct timeval nextin;
	struct timeval nextout;
};


static int powerof(int d)
{
	int x;
	for (x = 0; x < 32; x++)
		if ((1 << x) & d)
			return x;
	ast_log(LOG_WARNING, "Powerof %d: No power??\n", d);
	return -1;
}

void ast_translator_free_path(struct ast_trans_pvt *p)
{
	struct ast_trans_pvt *pl, *pn;
	pn = p;
	while(pn) {
		pl = pn;
		pn = pn->next;
		if (pl->state && pl->step->destroy)
			pl->step->destroy(pl->state);
		free(pl);
	}
}

struct ast_trans_pvt *ast_translator_build_path(int dest, int source)
{
	struct ast_trans_pvt *tmpr = NULL, *tmp = NULL;
	/* One of the hardest parts:  Build a set of translators based upon
	   the given source and destination formats */
	source = powerof(source);
	dest = powerof(dest);
	while(source != dest) {
		if (tr_matrix[source][dest].step) {
			if (tmp) {
				tmp->next = malloc(sizeof(struct ast_trans_pvt));
				tmp = tmp->next;
			} else
				tmp = malloc(sizeof(struct ast_trans_pvt));

				
			if (tmp) {
				tmp->next = NULL;
				tmp->nextin.tv_sec = 0;
				tmp->nextin.tv_usec = 0;
				tmp->nextout.tv_sec = 0;
				tmp->nextout.tv_usec = 0;
				tmp->step = tr_matrix[source][dest].step;
				tmp->state = tmp->step->newpvt();
				if (!tmp->state) {
					ast_log(LOG_WARNING, "Failed to build translator step from %d to %d\n", source, dest);
					free(tmp);
					tmp = NULL;
					return NULL;
				}
				/* Set the root, if it doesn't exist yet... */
				if (!tmpr)
					tmpr = tmp;
				/* Keep going if this isn't the final destination */
				source = tmp->step->dstfmt;
			} else {
				/* XXX This could leak XXX */
				ast_log(LOG_WARNING, "Out of memory\n");
				return NULL;
			}
		} else {
			/* We shouldn't have allocated any memory */
			ast_log(LOG_WARNING, "No translator path from %s to %s\n", 
				ast_getformatname(source), ast_getformatname(dest));
			return NULL;
		}
	}
	return tmpr;
}

struct ast_frame *ast_translate(struct ast_trans_pvt *path, struct ast_frame *f, int consume)
{
	struct ast_trans_pvt *p;
	struct ast_frame *out;
	struct timeval delivery;
	p = path;
	/* Feed the first frame into the first translator */
	p->step->framein(p->state, f);
	if (f->delivery.tv_sec || f->delivery.tv_usec) {
		if (path->nextin.tv_sec || path->nextin.tv_usec) {
			/* Make sure this is in line with what we were expecting */
			if ((path->nextin.tv_sec != f->delivery.tv_sec) ||
			    (path->nextin.tv_usec != f->delivery.tv_usec)) {
				/* The time has changed between what we expected and this
				   most recent time on the new packet.  Adjust our output
				   time appropriately */
				long sdiff;
				long udiff;
				sdiff = f->delivery.tv_sec - path->nextin.tv_sec;
				udiff = f->delivery.tv_usec - path->nextin.tv_usec;
				path->nextin.tv_sec = f->delivery.tv_sec;
				path->nextin.tv_usec = f->delivery.tv_usec;
				path->nextout.tv_sec += sdiff;
				path->nextout.tv_usec += udiff;
				if (path->nextout.tv_usec < 0) {
					path->nextout.tv_usec += 1000000;
					path->nextout.tv_sec--;
				} else if (path->nextout.tv_usec >= 1000000) {
					path->nextout.tv_usec -= 1000000;
					path->nextout.tv_sec++;
				}
			}
		} else {
			/* This is our first pass.  Make sure the timing looks good */
			path->nextin.tv_sec = f->delivery.tv_sec;
			path->nextin.tv_usec = f->delivery.tv_usec;
			path->nextout.tv_sec = f->delivery.tv_sec;
			path->nextout.tv_usec = f->delivery.tv_usec;
		}
		/* Predict next incoming sample */
		path->nextin.tv_sec += (f->samples / 8000);
		path->nextin.tv_usec += ((f->samples % 8000) * 125);
		if (path->nextin.tv_usec >= 1000000) {
			path->nextin.tv_usec -= 1000000;
			path->nextin.tv_sec++;
		}
	}
	delivery.tv_sec = f->delivery.tv_sec;
	delivery.tv_usec = f->delivery.tv_usec;
	if (consume)
		ast_frfree(f);
	while(p) {
		out = p->step->frameout(p->state);
		/* If we get nothing out, return NULL */
		if (!out)
			return NULL;
		/* If there is a next state, feed it in there.  If not,
		   return this frame  */
		if (p->next) 
			p->next->step->framein(p->next->state, out);
		else {
			if (delivery.tv_sec || delivery.tv_usec) {
				/* Use next predicted outgoing timestamp */
				out->delivery.tv_sec = path->nextout.tv_sec;
				out->delivery.tv_usec = path->nextout.tv_usec;
				
				/* Predict next outgoing timestamp from samples in this
				   frame. */
				path->nextout.tv_sec += (out->samples / 8000);
				path->nextout.tv_usec += ((out->samples % 8000) * 125);
				if (path->nextout.tv_usec >= 1000000) {
					path->nextout.tv_sec++;
					path->nextout.tv_usec -= 1000000;
				}
			} else {
				out->delivery.tv_sec = 0;
				out->delivery.tv_usec = 0;
			}
			return out;
		}
		p = p->next;
	}
	ast_log(LOG_WARNING, "I should never get here...\n");
	return NULL;
}


static void calc_cost(struct ast_translator *t,int samples)
{
	int sofar=0;
	struct ast_translator_pvt *pvt;
	struct ast_frame *f, *out;
	struct timeval start, finish;
	int cost;
	if(!samples)
	  samples = 1;
	
	/* If they don't make samples, give them a terrible score */
	if (!t->sample) {
		ast_log(LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
		t->cost = 99999;
		return;
	}
	pvt = t->newpvt();
	if (!pvt) {
		ast_log(LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		t->cost = 99999;
		return;
	}
	gettimeofday(&start, NULL);
	/* Call the encoder until we've processed one second of time */
	while(sofar < samples * 8000) {
		f = t->sample();
		if (!f) {
			ast_log(LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
			t->destroy(pvt);
			t->cost = 99999;
			return;
		}
		t->framein(pvt, f);
		ast_frfree(f);
		while((out = t->frameout(pvt))) {
			sofar += out->samples;
			ast_frfree(out);
		}
	}
	gettimeofday(&finish, NULL);
	t->destroy(pvt);
	cost = (finish.tv_sec - start.tv_sec) * 1000 + (finish.tv_usec - start.tv_usec) / 1000;
	t->cost = cost / samples;
	if (!t->cost)
		t->cost = 1;
}

static void rebuild_matrix(int samples)
{
	struct ast_translator *t;
	int changed;
	int x,y,z;
	if (option_debug)
		ast_log(LOG_DEBUG, "Reseting translation matrix\n");
	/* Use the list of translators to build a translation matrix */
	bzero(tr_matrix, sizeof(tr_matrix));
	t = list;
	while(t) {
	  if(samples)
	    calc_cost(t,samples);
	  
		if (!tr_matrix[t->srcfmt][t->dstfmt].step ||
		     tr_matrix[t->srcfmt][t->dstfmt].cost > t->cost) {
			tr_matrix[t->srcfmt][t->dstfmt].step = t;
			tr_matrix[t->srcfmt][t->dstfmt].cost = t->cost;
		}
		t = t->next;
	}
	do {
		changed = 0;
		/* Don't you just love O(N^3) operations? */
		for (x=0; x< MAX_FORMAT; x++)				/* For each source format */
			for (y=0; y < MAX_FORMAT; y++) 			/* And each destination format */
				if (x != y)							/* Except ourselves, of course */
					for (z=0; z < MAX_FORMAT; z++) 	/* And each format it might convert to */
						if ((x!=z) && (y!=z)) 		/* Don't ever convert back to us */
							if (tr_matrix[x][y].step && /* We can convert from x to y */
								tr_matrix[y][z].step && /* And from y to z and... */
								(!tr_matrix[x][z].step || 	/* Either there isn't an x->z conversion */
								(tr_matrix[x][y].cost + 
								 tr_matrix[y][z].cost <	/* Or we're cheaper than the existing */
								 tr_matrix[x][z].cost)  /* solution */
							     )) {
								 			/* We can get from x to z via y with a cost that
											   is the sum of the transition from x to y and
											   from y to z */
								 
								 	tr_matrix[x][z].step = tr_matrix[x][y].step;
									tr_matrix[x][z].cost = tr_matrix[x][y].cost + 
														   tr_matrix[y][z].cost;
									if (option_debug)
										ast_log(LOG_DEBUG, "Discovered %d cost path from %s to %s, via %d\n", tr_matrix[x][z].cost, ast_getformatname(x), ast_getformatname(z), y);
									changed++;
								 }
		
	} while (changed);
}





static int show_translation(int fd, int argc, char *argv[])
{
#define SHOW_TRANS 11
	int x, y, z;
	char line[80];
	if (argc > 4) 
		return RESULT_SHOWUSAGE;

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
	ast_mutex_lock(&list_lock);
	for (x=-1;x<SHOW_TRANS; x++) {
		/* next 2 lines run faster than using strcpy() */
		line[0] = ' ';
		line[1] = '\0';
		for (y=-1;y<SHOW_TRANS;y++) {
			if (x >= 0 && y >= 0 && tr_matrix[x][y].step)
				snprintf(line + strlen(line), sizeof(line) - strlen(line), " %5d", tr_matrix[x][y].cost >= 99999 ? tr_matrix[x][y].cost-99999 : tr_matrix[x][y].cost);
			else
				if (((x == -1 && y >= 0) || (y == -1 && x >= 0))) {
					snprintf(line + strlen(line), sizeof(line) - strlen(line), 
						" %5s", ast_getformatname(1<<(x+y+1)) );
				} else if (x != -1 && y != -1) {
					snprintf(line + strlen(line), sizeof(line) - strlen(line), "     -");
				} else {
					snprintf(line + strlen(line), sizeof(line) - strlen(line), "      ");
				}
		}
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "\n");
		ast_cli(fd, line);			
	}
	ast_mutex_unlock(&list_lock);
	return RESULT_SUCCESS;
}

static int added_cli = 0;

static char show_trans_usage[] =
"Usage: show translation [recalc] [<recalc seconds>]\n"
"       Displays known codec translators and the cost associated\n"
"with each conversion.  if the arguement 'recalc' is supplied along\n"
"with optional number of seconds to test a new test will be performed\n"
"as the chart is being displayed.\n";

static struct ast_cli_entry show_trans =
{ { "show", "translation", NULL }, show_translation, "Display translation matrix", show_trans_usage };

int ast_register_translator(struct ast_translator *t)
{
	char tmp[80];
	t->srcfmt = powerof(t->srcfmt);
	t->dstfmt = powerof(t->dstfmt);
	if ((t->srcfmt >= MAX_FORMAT) || (t->dstfmt >= MAX_FORMAT)) {
		ast_log(LOG_WARNING, "Format %s is larger than MAX_FORMAT\n", ast_getformatname(t->srcfmt));
		return -1;
	}
	calc_cost(t,1);
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered translator '%s' from format %s to %s, cost %d\n", term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), ast_getformatname(1 << t->srcfmt), ast_getformatname(1 << t->dstfmt), t->cost);
	ast_mutex_lock(&list_lock);
	if (!added_cli) {
		ast_cli_register(&show_trans);
		added_cli++;
	}
	t->next = list;
	list = t;
	rebuild_matrix(0);
	ast_mutex_unlock(&list_lock);
	return 0;
}

int ast_unregister_translator(struct ast_translator *t)
{
	char tmp[80];
	struct ast_translator *u, *ul = NULL;
	ast_mutex_lock(&list_lock);
	u = list;
	while(u) {
		if (u == t) {
			if (ul)
				ul->next = u->next;
			else
				list = u->next;
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Unregistered translator '%s' from format %s to %s\n", term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), ast_getformatname(1 << t->srcfmt), ast_getformatname(1 << t->dstfmt));
			break;
		}
		ul = u;
		u = u->next;
	}
	rebuild_matrix(0);
	ast_mutex_unlock(&list_lock);
	return (u ? 0 : -1);
}

int ast_translator_best_choice(int *dst, int *srcs)
{
	/* Calculate our best source format, given costs, and a desired destination */
	int x,y;
	int best = -1;
	int bestdst = 0;
	int cur = 1;
	int besttime = INT_MAX;
	int common;

	if ((common = (*dst) & (*srcs))) {
		/* We have a format in common */
		for (y=0; y < MAX_FORMAT; y++) {
			if (cur & common) {
				/* This is a common format to both.  Pick it if we don't have one already */
				besttime = 0;
				bestdst = cur;
				best = cur;
			}
			cur = cur << 1;
		}
	} else {
		/* We will need to translate */
		ast_mutex_lock(&list_lock);
		for (y=0; y < MAX_FORMAT; y++) {
			if (cur & *dst)
				for (x=0; x < MAX_FORMAT; x++) {
					if ((*srcs & (1 << x)) &&			/* x is a valid source format */
					    tr_matrix[x][y].step &&			/* There's a step */
					    (tr_matrix[x][y].cost < besttime)) {	/* It's better than what we have so far */
						best = 1 << x;
						bestdst = cur;
						besttime = tr_matrix[x][y].cost;
					}
				}
			cur = cur << 1;
		}
		ast_mutex_unlock(&list_lock);
	}
	if (best > -1) {
		*srcs = best;
		*dst = bestdst;
		best = 0;
	}
	return best;
}
