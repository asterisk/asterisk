/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate via the use of pseudo channels
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <asterisk/frame.h>
#include <asterisk/sched.h>
#include <asterisk/cli.h>
#include <asterisk/term.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

/* Uncomment the EXPERIMENTAL_TRANSLATION to enable a more complicated, but probably more
   correct way of handling full duplex translation */

/*
#define EXPERIMENTAL_TRANSLATION
*/

/* This could all be done more efficiently *IF* we chained packets together
   by default, but it would also complicate virtually every application. */
   
static pthread_mutex_t list_lock = AST_MUTEX_INITIALIZER;
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
	struct ast_trans_pvt *pl;
	while(p) {
		pl = p;
		p = p->next;
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
				tmp->step = tr_matrix[source][dest].step;
				tmp->state = tmp->step->new();
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
			ast_log(LOG_WARNING, "No translator path from %d to %d\n", source, dest);
			return NULL;
		}
	}
	return tmpr;
}

struct ast_frame *ast_translate(struct ast_trans_pvt *path, struct ast_frame *f, int consume)
{
	struct ast_trans_pvt *p;
	struct ast_frame *out;
	p = path;
	/* Feed the first frame into the first translator */
	p->step->framein(p->state, f);
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
		else
			return out;
		p = p->next;
	}
	ast_log(LOG_WARNING, "I should never get here...\n");
	return NULL;
}

static void rebuild_matrix()
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
										ast_log(LOG_DEBUG, "Discovered %d cost path from %d to %d, via %d\n", tr_matrix[x][z].cost, x, z, y);
									changed++;
								 }
		
	} while (changed);
}

static void calc_cost(struct ast_translator *t)
{
	int sofar=0;
	struct ast_translator_pvt *pvt;
	struct ast_frame *f, *out;
	struct timeval start, finish;
	int cost;
	/* If they don't make samples, give them a terrible score */
	if (!t->sample) {
		ast_log(LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
		t->cost = 99999;
		return;
	}
	pvt = t->new();
	if (!pvt) {
		ast_log(LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		t->cost = 99999;
		return;
	}
	gettimeofday(&start, NULL);
	/* Call the encoder until we've processed one second of time */
	while(sofar < 1000) {
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
			sofar += out->timelen;
			ast_frfree(out);
		}
	}
	gettimeofday(&finish, NULL);
	t->destroy(pvt);
	cost = (finish.tv_sec - start.tv_sec) * 1000 + (finish.tv_usec - start.tv_usec) / 1000;
	t->cost = cost;
}

static int show_translation(int fd, int argc, char *argv[])
{
#define SHOW_TRANS 14
	int x,y;
	char line[80];
	if (argc != 2) 
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "                        Translation times between formats (in milliseconds)\n");
	ast_cli(fd, "                                 Destination Format\n");
	ast_pthread_mutex_lock(&list_lock);
	for (x=0;x<SHOW_TRANS; x++) {
		if (x == 1) 
			strcpy(line, "  Src  ");
		else if (x == 2)
			strcpy(line, "  Fmt  ");
		else
			strcpy(line, "       ");
		for (y=0;y<SHOW_TRANS;y++) {
			if (tr_matrix[x][y].step)
				snprintf(line + strlen(line), sizeof(line) - strlen(line), " %4d", tr_matrix[x][y].cost);
			else
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  n/a");
		}
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "\n");
		ast_cli(fd, line);			
	}
	ast_pthread_mutex_unlock(&list_lock);
	return RESULT_SUCCESS;
}

static int added_cli = 0;

static char show_trans_usage[] =
"Usage: show translation\n"
"       Displays known codec translators and the cost associated\n"
"with each conversion.\n";

static struct ast_cli_entry show_trans =
{ { "show", "translation", NULL }, show_translation, "Display translation matrix", show_trans_usage };

int ast_register_translator(struct ast_translator *t)
{
	char tmp[80];
	t->srcfmt = powerof(t->srcfmt);
	t->dstfmt = powerof(t->dstfmt);
	if ((t->srcfmt >= MAX_FORMAT) || (t->dstfmt >= MAX_FORMAT)) {
		ast_log(LOG_WARNING, "Format %d is larger than MAX_FORMAT\n", t->srcfmt);
		return -1;
	}
	calc_cost(t);
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered translator '%s' from format %d to %d, cost %d\n", term_color(tmp, t->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(tmp)), t->srcfmt, t->dstfmt, t->cost);
	ast_pthread_mutex_lock(&list_lock);
	if (!added_cli) {
		ast_cli_register(&show_trans);
		added_cli++;
	}
	t->next = list;
	list = t;
	rebuild_matrix();
	ast_pthread_mutex_unlock(&list_lock);
	return 0;
}

int ast_unregister_translator(struct ast_translator *t)
{
	struct ast_translator *u, *ul = NULL;
	ast_pthread_mutex_lock(&list_lock);
	u = list;
	while(u) {
		if (u == t) {
			if (ul)
				ul->next = u->next;
			else
				list = u->next;
			break;
		}
		ul = u;
		u = u->next;
	}
	rebuild_matrix();
	ast_pthread_mutex_unlock(&list_lock);
	return (u ? 0 : -1);
}

int ast_translator_best_choice(int *dst, int *srcs)
{
	/* Calculate our best source format, given costs, and a desired destination */
	int x,y;
	int best=-1;
	int bestdst=0;
	int cur = 1;
	int besttime=999999999;
	ast_pthread_mutex_lock(&list_lock);
	for (y=0;y<MAX_FORMAT;y++) {
		if ((cur & *dst) && (cur & *srcs)) {
			/* This is a common format to both.  Pick it if we don't have one already */
			besttime=0;
			bestdst = cur;
			best = cur;
			break;
		}
		if (cur & *dst)
			for (x=0;x<MAX_FORMAT;x++) {
				if (tr_matrix[x][y].step &&	/* There's a step */
			   	 (tr_matrix[x][y].cost < besttime) && /* We're better than what exists now */
					(*srcs & (1 << x)))			/* x is a valid source format */
					{
						best = 1 << x;
						bestdst = cur;
						besttime = tr_matrix[x][y].cost;
					}
			}
		cur = cur << 1;
	}
	if (best > -1) {
		*srcs = best;
		*dst = bestdst;
		best = 0;
	}
	ast_pthread_mutex_unlock(&list_lock);
	return best;
}
