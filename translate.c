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

#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <asterisk/frame.h>
#include <asterisk/sched.h>
#include <asterisk/cli.h>
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
   
static char *type = "Trans";

static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
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

struct translator_pvt {
	/* Sockets for communication */
	int comm[2];
	struct ast_trans_pvt *system;
	struct ast_trans_pvt *rsystem;
	struct timeval lastpass;
#ifdef EXPERIMENTAL_TRANSLATION
	struct ast_frame_delivery *head;
	struct ast_frame_delivery *tail;
	struct sched_context *sched;
#endif
	pthread_t	threadid;
};


#ifdef EXPERIMENTAL_TRANSLATION
static int deliver(void *data)
{
	struct ast_frame_delivery *del = data;
	ast_log(LOG_DEBUG, "Delivering a packet\n");
	if (del->f) {
		if (del->chan)
			ast_write(del->chan, del->f);
		else
			ast_fr_fdwrite(del->fd, del->f);
		ast_frfree(del->f);
	}
	/* Take us out of the list */
	if (del->prev) 
		del->prev->next = del->next;
	else
		del->owner->head = del->next;
	if (del->next)
		del->next->prev = del->prev;
	else
		del->owner->tail = del->prev;
	/* Free used memory */
	free(del);
	/* Never run again */
	return 0;
}

/* Schedule the delivery of a packet in the near future, using the given context */
static int schedule_delivery(struct sched_context *sched, struct translator_pvt *p, struct ast_channel *c, 
							int fd, struct ast_frame *f, int ms)
{
	struct ast_frame_delivery *del;
	ast_log(LOG_DEBUG, "Scheduling a packet delivery\n");
	del = malloc(sizeof(struct ast_frame_delivery));
	if (del) {
		del->f = ast_frdup(f);
		del->chan = c;
		del->fd = fd;
		del->owner = p;
		if (p->tail) {
			del->prev = p->tail;
			p->tail = del;
			del->next = NULL;
		} else {
			p->head = p->tail = del;
			del->next = NULL;
			del->prev = NULL;
		}
		ast_sched_add(sched, ms, deliver, del);
		return 0;	
	} else
		return -1;
}
#endif
static int translator_hangup(struct ast_channel *chan)
{
	ast_log(LOG_WARNING, "Explicit hangup on '%s' not recommended!  Call translator_destroy() instead.\n", chan->name);
	chan->master->trans = NULL;
	ast_hangup(chan->master);
	chan->master = NULL;
	return 0;
}

static int translator_send_digit(struct ast_channel *chan, char digit)
{
	/* Pass digits right along */
	if (chan->master->pvt->send_digit)
		return chan->master->pvt->send_digit(chan->master, digit);
	return -1;
}

static int translator_call(struct ast_channel *chan, char *addr, int timeout)
{
	if (chan->master->pvt->call)
		return chan->master->pvt->call(chan->master, addr, timeout);
	return -1;
}

static int translator_answer(struct ast_channel *chan)
{
	if (chan->master->pvt->answer)
		return chan->master->pvt->answer(chan->master);
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

static void ast_translator_free(struct translator_pvt *pvt)
{
#ifdef EXPERIMENTAL_TRANSLATION
	struct ast_frame_delivery *d, *dl;
	if (pvt->sched)
		sched_context_destroy(pvt->sched);
	d = pvt->head;
	while(d) {
		dl = d;
		d = d->next;
		ast_frfree(dl->f);
		free(dl);
	}
#endif
	ast_translator_free_path(pvt->system);
	ast_translator_free_path(pvt->rsystem);
	if (pvt->comm[0] > -1)
		close(pvt->comm[0]);
	if (pvt->comm[1] > -1)
		close(pvt->comm[1]);
	free(pvt);
}

struct ast_trans_pvt *ast_translator_build_path(int source, int dest)
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
					free(tmp);
					tmp = NULL;
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

static struct ast_frame *translator_read(struct ast_channel *chan)
{
	return ast_fr_fdread(chan->fd);
}

static int translator_write(struct ast_channel *chan, struct ast_frame *frame)
{
	return ast_fr_fdwrite(chan->fd, frame);
}

struct ast_frame_chain *ast_translate(struct ast_trans_pvt *path, struct ast_frame *f)
{
	struct ast_trans_pvt *p;
	struct ast_frame *out;
	struct ast_frame_chain *outc = NULL, *prev = NULL, *cur;
	p = path;
	/* Feed the first frame into the first translator */
	p->step->framein(p->state, f);
	while(p) {
		/* Read all the frames from the current translator */
		while((out = p->step->frameout(p->state)))  {
			if (p->next) {
				/* Feed to next layer */
				p->next->step->framein(p->next->state, out);
			} else {
				/* Last layer -- actually do something */
				cur = malloc(sizeof(struct ast_frame_chain));
				if (!cur) {
					/* XXX Leak majorly on a problem XXX */
					ast_log(LOG_WARNING, "Out of memory\n");
					return NULL;
				}
				if (prev) 
					prev->next = cur;
				else
					outc = cur;
				cur->fr = ast_frisolate(out);
				cur->next = NULL;
				if (prev)
					prev = prev->next;
				else
					prev = outc;
			}
		}
		p = p->next;
	}
	return outc;
}

#define FUDGE 0

static void translator_apply(struct translator_pvt *pvt, struct ast_trans_pvt *path, struct ast_frame *f, int fd, struct ast_channel *c, struct timeval *last)
{
	struct ast_trans_pvt *p;
	struct ast_frame *out;
	struct timeval tv;
	int ms;
	p = path;
	/* Feed the first frame into the first translator */
	p->step->framein(p->state, f);
	while(p) {
		/* Read all the frames from the current translator */
		while((out = p->step->frameout(p->state)))  {
			if (p->next) {
				/* Feed to next layer */
				p->next->step->framein(p->next->state, out);
			} else {
				/* Delay if needed */
				if (last->tv_sec || last->tv_usec) {
					gettimeofday(&tv, NULL);
					ms = 1000 * (tv.tv_sec - last->tv_sec) +
						(tv.tv_usec - last->tv_usec) / 1000;
#ifdef EXPERIMENTAL_TRANSLATION
					if (ms + FUDGE < out->timelen) 
						schedule_delivery(pvt->sched, pvt, 
											c, fd, out, ms);
					else {
						if (c)
							ast_write(c, out);
						else
							ast_fr_fdwrite(fd, out);
					}
					last->tv_sec = tv.tv_sec;
					last->tv_usec = tv.tv_usec;
					/* Schedule this packet to be delivered at the
					   right time */
				} else
#else
					/* XXX Not correct in the full duplex case XXX */
					if (ms + FUDGE < out->timelen) 
						usleep((out->timelen - ms - FUDGE) * 1000);
					last->tv_sec = tv.tv_sec;
					last->tv_usec = tv.tv_usec;
				}
#endif
				if (c)
					ast_write(c, out);
				else
					ast_fr_fdwrite(fd, out);
			}
			ast_frfree(out);
		}
		p = p->next;
	}
}

static void *translator_thread(void *data)
{
	struct ast_channel *real = data;
	struct ast_frame *f;
	int ms = -1;
	struct translator_pvt *pvt = NULL;
	int fd = -1;
	int fds[2];
	int res;
	/* Read from the real, translate, write as necessary to the fake */
	for(;;) {
		/* Break here if need be */
		pthread_testcancel();
		if (!real->trans) {
			ast_log(LOG_WARNING, "No translator anymore\n");
			break;
		}
		pvt = real->trans->pvt->pvt;
		fd = pvt->comm[1];
		fds[0] = fd;
		fds[1] = real->fd;
		CHECK_BLOCKING(real);
		res = ast_waitfor_n_fd(fds, 2, &ms);
		real->blocking = 0;
		/* Or we can die here, that's fine too */
		pthread_testcancel();
		if (res >= 0) {
			if (res == real->fd) {
				f = ast_read(real);
				if (!f) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Empty frame\n");
					break;
				}
				if (f->frametype ==  AST_FRAME_VOICE) {
					if (pvt->system)
						translator_apply(pvt, pvt->system, f, fd, NULL, &pvt->lastpass);
				} else {
					/* If it's not voice, just pass it along */
					ast_fr_fdwrite(fd, f);
				}
				ast_frfree(f);
			} else {
				f = ast_fr_fdread(res);
				if (!f) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Empty (hangup) frame\n");
					break;
				}
				
				if (f->frametype == AST_FRAME_VOICE) {
					if (pvt->rsystem)
						translator_apply(pvt, pvt->rsystem, f, -1, real, &pvt->lastpass);
				} else {
					ast_write(real, f);
				}
				ast_frfree(f);
			}
		} else {
			ast_log(LOG_DEBUG, "Waitfor returned non-zero\n");
			break;
		}
	}
	if (pvt)
		pvt->comm[1] = -1;
	if (fd > -1) {
		/* Write a bogus frame */
		write(fd, data, 1);
		close(fd);
	}
	return NULL;
}

struct ast_channel *ast_translator_create(struct ast_channel *real, int format, int direction)
{
	struct ast_channel *tmp;
	struct translator_pvt *pvt;
	if (real->trans) {
		ast_log(LOG_WARNING, "Translator already exists on '%s'\n", real->name);
		return NULL;
	}
	if (!(pvt = malloc(sizeof(struct translator_pvt)))) {
		ast_log(LOG_WARNING, "Unable to allocate private translator on '%s'\n", real->name);
		return NULL;
	}
	pvt->comm[0] = -1;
	pvt->comm[1] = -1;
	pvt->lastpass.tv_usec = 0;
	pvt->lastpass.tv_sec = 0;

#ifdef EXPERIMENTAL_TRANSLATION	
	pvt->head = NULL;
	pvt->tail = NULL;
	pvt->sched = sched_context_create();
	if (!pvt->sched) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_translator_free(pvt);
		return NULL;
	}
#endif
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pvt->comm)) {
		ast_log(LOG_WARNING, "Unable to create UNIX domain socket on '%s'\n", real->name);
		ast_translator_free(pvt);
		return NULL;
	}
	/* In to the system */
	if (direction & AST_DIRECTION_IN)
		pvt->system = ast_translator_build_path(real->format, format);
	else
		pvt->system = NULL;
	/* Out from the system */
	if (direction & AST_DIRECTION_OUT)
		pvt->rsystem = ast_translator_build_path(format, real->format);
	else
		pvt->rsystem = NULL;
	if (!pvt->system && !pvt->rsystem) {
		ast_log(LOG_WARNING, "Unable to build a translation path for %s (%d to %d)\n", real->name, real->format, format);
		ast_translator_free(pvt);
		return NULL;
	}
	if (!pvt->system && (direction & AST_DIRECTION_IN)) {
		ast_log(LOG_WARNING, "Translation path for '%s' is one-way (reverse)\n", real->name);
		ast_translator_free(pvt);
		return NULL;
	}
	if (!pvt->rsystem && (direction & AST_DIRECTION_OUT)) {
		ast_log(LOG_WARNING, "Translation path for '%s' is one-way (forward)\n", real->name);
		ast_translator_free(pvt);
		return NULL;
	}
	if ((tmp = ast_channel_alloc())) {
		snprintf(tmp->name, sizeof(tmp->name), "%s/Translate:%d", real->name, format);
		tmp->type = type;
		tmp->fd = pvt->comm[0];
		tmp->format = format;
		tmp->state = real->state;
		tmp->rings = 0;
		tmp->pvt->pvt = pvt;
		tmp->master = real;
		tmp->pvt->send_digit = translator_send_digit;
		tmp->pvt->call = translator_call;
		tmp->pvt->hangup = translator_hangup;
		tmp->pvt->answer = translator_answer;
		tmp->pvt->read = translator_read;
		tmp->pvt->write = translator_write;
		tmp->pvt->pvt = pvt;
		real->trans = tmp;
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Created translator %s\n", tmp->name);
		if (pthread_create(&pvt->threadid, NULL, translator_thread, real) < 0) {
			ast_translator_destroy(tmp);
			tmp = NULL;
			ast_log(LOG_WARNING, "Failed to start thread\n");
		}
	} else {
		ast_translator_free(pvt);
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
	}
	return tmp;
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
	pthread_mutex_lock(&list_lock);
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
	pthread_mutex_unlock(&list_lock);
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
	t->srcfmt = powerof(t->srcfmt);
	t->dstfmt = powerof(t->dstfmt);
	if ((t->srcfmt >= MAX_FORMAT) || (t->dstfmt >= MAX_FORMAT)) {
		ast_log(LOG_WARNING, "Format %d is larger than MAX_FORMAT\n", t->srcfmt);
		return -1;
	}
	calc_cost(t);
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered translator '%s' from format %d to %d, cost %d\n", t->name, t->srcfmt, t->dstfmt, t->cost);
	pthread_mutex_lock(&list_lock);
	if (!added_cli) {
		ast_cli_register(&show_trans);
		added_cli++;
	}
	t->next = list;
	list = t;
	rebuild_matrix();
	pthread_mutex_unlock(&list_lock);
	return 0;
}

int ast_unregister_translator(struct ast_translator *t)
{
	struct ast_translator *u, *ul = NULL;
	pthread_mutex_lock(&list_lock);
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
	pthread_mutex_unlock(&list_lock);
	return (u ? 0 : -1);
}

void ast_translator_destroy(struct ast_channel *trans)
{
	struct translator_pvt *pvt;
	if (!trans->master) {
		ast_log(LOG_WARNING, "Translator is not part of a real channel?\n");
		return;
	}
	if (trans->master->trans != trans) {
		ast_log(LOG_WARNING, "Translator is not the right one!?!?\n");
		return;
	}
	trans->master->trans = NULL;
	pvt = trans->pvt->pvt;
	/* Cancel the running translator thread */
	pthread_cancel(pvt->threadid);
	pthread_join(pvt->threadid, NULL);
	ast_translator_free(pvt);
	trans->pvt->pvt = NULL;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Destroyed translator %s\n", trans->name);
	ast_channel_free(trans);
}

int ast_translator_best_choice(int dst, int srcs)
{
	/* Calculate our best source format, given costs, and a desired destination */
	int x,y;
	int best=-1;
	int cur = 1;
	int besttime=999999999;
	pthread_mutex_lock(&list_lock);
	for (y=0;y<MAX_FORMAT;y++) {
		if (cur & dst)
			for (x=0;x<MAX_FORMAT;x++) {
				if (tr_matrix[x][y].step &&	/* There's a step */
			   	 (tr_matrix[x][y].cost < besttime) && /* We're better than what exists now */
					(srcs & (1 << x)))			/* x is a valid source format */
					{
						best = 1 << x;
						besttime = tr_matrix[x][dst].cost;
					}
			}
		cur = cur << 1;
	}
	pthread_mutex_unlock(&list_lock);
	return best;
}
