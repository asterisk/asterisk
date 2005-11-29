/*
 * Cheops Next Generation
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C)1999, Mark Spencer
 * 
 * Distributed under the terms of the GNU General Public License (GPL) Version 2
 *
 * Logging routines
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include "asterisk.h"

#define AST_EVENT_LOG AST_LOG_DIR "/" EVENTLOG

#define MAX_MSG_QUEUE 200

static pthread_mutex_t msglist_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;

static struct msglist {
	char *msg;
	struct msglist *next;
} *list = NULL, *last = NULL;

static int msgcnt = 0;

static FILE *eventlog = NULL;

static char *levels[] = {
	"DEBUG",
	"EVENT",
	"NOTICE",
	"WARNING",
	"ERROR"
};

static struct verb {
	void (*verboser)(char *string, int opos, int replacelast, int complete);
	struct verb *next;
} *verboser = NULL;

int init_logger(void)
{

	mkdir(AST_LOG_DIR, 0755);
	eventlog = fopen(AST_EVENT_LOG, "a");
	if (eventlog) {
		ast_log(LOG_EVENT, "Started Asterisk Event Logger\n");
		if (option_verbose)
			ast_verbose("Asterisk Event Logger Started\n");
		return 0;
	} else 
		ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	return -1;
}

extern void ast_log(int level, char *file, int line, char *function, char *fmt, ...)
{
	char date[256];
	time_t t;
	struct tm *tm;

	va_list ap;
	va_start(ap, fmt);
	pthread_mutex_lock(&loglock);
	if (level == 1 /* Event */) {
		time(&t);
		tm = localtime(&t);
		if (tm) {
			/* Log events into the event log file, with a different format */
			strftime(date, sizeof(date), "%b %e %T", tm);
			fprintf(eventlog, "%s asterisk[%d]: ", date, getpid());
			vfprintf(eventlog, fmt, ap);
			fflush(eventlog);
		} else
			ast_log(LOG_WARNING, "Unable to retrieve local time?\n");
	} else {
		fprintf(stdout, "%s: File %s, Line %d (%s): ", levels[level], file, line, function);
		vfprintf(stdout, fmt, ap);
		fflush(stdout);
	}
	pthread_mutex_unlock(&loglock);
	va_end(ap);
}

extern void ast_verbose(char *fmt, ...)
{
	static char stuff[4096];
	static int pos = 0, opos;
	static int replacelast = 0, complete;
	struct msglist *m;
	struct verb *v;
	va_list ap;
	va_start(ap, fmt);
	pthread_mutex_lock(&msglist_lock);
	vsnprintf(stuff + pos, sizeof(stuff) - pos, fmt, ap);
	opos = pos;
	pos = strlen(stuff);
	if (fmt[strlen(fmt)-1] == '\n') 
		complete = 1;
	else
		complete=0;
	if (complete) {
		if (msgcnt < MAX_MSG_QUEUE) {
			/* Allocate new structure */
			m = malloc(sizeof(struct msglist));
			msgcnt++;
		} else {
			/* Recycle the oldest entry */
			m = list;
			list = list->next;
			free(m->msg);
		}
		if (m) {
			m->msg = strdup(stuff);
			if (m->msg) {
				if (last)
					last->next = m;
				else
					list = m;
				m->next = NULL;
				last = m;
			} else {
				msgcnt--;
				ast_log(LOG_DEBUG, "Out of memory\n");
				free(m);
			}
		}
	}
	pthread_mutex_lock(&loglock);
	if (verboser) {
		v = verboser;
		while(v) {
			v->verboser(stuff, opos, replacelast, complete);
			v = v->next;
		}
	} else
		fprintf(stdout, stuff + opos);

	if (fmt[strlen(fmt)-1] != '\n') 
		replacelast = 1;
	else 
		replacelast = pos = 0;
	pthread_mutex_unlock(&loglock);
	va_end(ap);
	pthread_mutex_unlock(&msglist_lock);
}


int ast_register_verbose(void (*v)(char *string, int opos, int replacelast, int complete)) 
{
	struct msglist *m;
	struct verb *tmp;
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	if ((tmp = malloc(sizeof (struct verb)))) {
		tmp->verboser = v;
		pthread_mutex_lock(&msglist_lock);
		tmp->next = verboser;
		verboser = tmp;
		m = list;
		while(m) {
			/* Send all the existing entries that we have queued (i.e. they're likely to have missed) */
			v(m->msg, 0, 0, 1);
			m = m->next;
		}
		pthread_mutex_unlock(&msglist_lock);
		return 0;
	}
	return -1;
}

int ast_unregister_verbose(void (*v)(char *string, int opos, int replacelast, int complete))
{
	int res = -1;
	struct verb *tmp, *tmpl=NULL;
	pthread_mutex_lock(&msglist_lock);
	tmp = verboser;
	while(tmp) {
		if (tmp->verboser == v)	{
			if (tmpl)
				tmpl->next = tmp->next;
			else
				verboser = tmp->next;
			break;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	if (tmp)
		res = 0;
	pthread_mutex_unlock(&msglist_lock);
	return res;
}
