/*
 * Asterisk Logger
 * 
 * Mark Spencer <markster@marko.net>
 *
 * Copyright(C)1999, Linux Support Services, Inc.
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
#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/term.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include "asterisk.h"
#include "astconf.h"

#define MAX_MSG_QUEUE 200

static pthread_mutex_t msglist_lock = AST_MUTEX_INITIALIZER;
static pthread_mutex_t loglock = AST_MUTEX_INITIALIZER;

static struct msglist {
	char *msg;
	struct msglist *next;
} *list = NULL, *last = NULL;

struct logfile {
	char fn[256];
	int logflags;
	FILE *f;
	struct logfile *next;
};

static struct logfile *logfiles = NULL;

static int msgcnt = 0;

static FILE *eventlog = NULL;

static char *levels[] = {
	"DEBUG",
	"EVENT",
	"NOTICE",
	"WARNING",
	"ERROR"
};

static int colors[] = {
	COLOR_BRGREEN,
	COLOR_BRBLUE,
	COLOR_YELLOW,
	COLOR_BRRED,
	COLOR_RED
};

static int make_components(char *s, int lineno)
{
	char *w;
	int res = 0;
	char *stringp=NULL;
	stringp=s;
	w = strsep(&stringp, ",");
	while(w) {
		while(*w && (*w < 33))
			w++;
		if (!strcasecmp(w, "debug"))
			res |= (1 << 0);
		else if (!strcasecmp(w, "notice"))
			res |= (1 << 2);
		else if (!strcasecmp(w, "warning"))
			res |= (1 << 3);
		else if (!strcasecmp(w, "error"))
			res |= (1 << 4);
		else {
			fprintf(stderr, "Logfile Warning: Unknown keyword '%s' at line %d of logger.conf\n", w, lineno);
		}
		w = strsep(&stringp, ",");
	}
	return res;
}

static struct logfile *make_logfile(char *fn, char *components, int lineno)
{
	struct logfile *f;
	char tmp[256];
	if (!strlen(fn))
		return NULL;
	f = malloc(sizeof(struct logfile));
	if (f) {
		memset(f, 0, sizeof(f));
		strncpy(f->fn, fn, sizeof(f->fn) - 1);
		if (!strcasecmp(fn, "ignore")) {
			f->f = NULL;
		} else if (!strcasecmp(fn, "console")) {
			f->f = stdout;
		} else {
			if (fn[0] == '/') 
				strncpy(tmp, fn, sizeof(tmp) - 1);
			else
				snprintf(tmp, sizeof(tmp), "%s/%s", (char *)ast_config_AST_LOG_DIR, fn);
			f->f = fopen(tmp, "a");
			if (!f->f) {
				/* Can't log here, since we're called with a lock */
				fprintf(stderr, "Logger Warning: Unable to open log file '%s': %s\n", tmp, strerror(errno));
			}
		}
		f->logflags = make_components(components, lineno);
		
	}
	return f;
}

static void init_logger_chain(void)
{
	struct logfile *f, *cur;
	struct ast_config *cfg;
	struct ast_variable *var;

	ast_pthread_mutex_lock(&loglock);

	/* Free anything that is here */
	f = logfiles;
	while(f) {
		cur = f->next;
		if (f->f && (f->f != stdout) && (f->f != stderr))
			fclose(f->f);
		free(f);
		f = cur;
	}

	logfiles = NULL;

	ast_pthread_mutex_unlock(&loglock);
	cfg = ast_load("logger.conf");
	ast_pthread_mutex_lock(&loglock);
	
	/* If no config file, we're fine */
	if (!cfg) {
		ast_pthread_mutex_unlock(&loglock);
		return;
	}
	var = ast_variable_browse(cfg, "logfiles");
	while(var) {
		f = make_logfile(var->name, var->value, var->lineno);
		if (f) {
			f->next = logfiles;
			logfiles = f;
		}
		var = var->next;
	}
	if (!logfiles) {
		/* Gotta have at least one.  We'll make a NULL one */
		logfiles = make_logfile("ignore", "", -1);
	}
	ast_destroy(cfg);
	ast_pthread_mutex_unlock(&loglock);
	

}

static struct verb {
	void (*verboser)(const char *string, int opos, int replacelast, int complete);
	struct verb *next;
} *verboser = NULL;

int init_logger(void)
{
	char tmp[AST_CONFIG_MAX_PATH];
	mkdir((char *)ast_config_AST_LOG_DIR, 0755);
	snprintf(tmp, sizeof(tmp), "%s/%s", (char *)ast_config_AST_LOG_DIR, EVENTLOG);
	eventlog = fopen((char *)tmp, "a");
	if (eventlog) {
		init_logger_chain();
		ast_log(LOG_EVENT, "Started Asterisk Event Logger\n");
		if (option_verbose)
			ast_verbose("Asterisk Event Logger Started %s\n",(char *)tmp);
		return 0;
	} else 
		ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	init_logger_chain();
	return -1;
}

int reload_logger(void)
{
	char tmp[AST_CONFIG_MAX_PATH];
	ast_pthread_mutex_lock(&loglock);
	if (eventlog)
		fclose(eventlog);
	mkdir((char *)ast_config_AST_LOG_DIR, 0755);
	snprintf(tmp, sizeof(tmp), "%s/%s", (char *)ast_config_AST_LOG_DIR, EVENTLOG);
	eventlog = fopen((char *)tmp, "a");
	ast_pthread_mutex_unlock(&loglock);

	if (eventlog) {
		init_logger_chain();
		ast_log(LOG_EVENT, "Restarted Asterisk Event Logger\n");
		if (option_verbose)
			ast_verbose("Asterisk Event Logger restarted\n");
		return 0;
	} else 
		ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	init_logger_chain();
	return -1;
}

extern void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	char date[256];
	char tmp[80];
	char tmp2[80];
	char tmp3[80];
	char tmp4[80];
	char linestr[80];
	time_t t;
	struct tm tm;
	struct logfile *f;

	va_list ap;
	if (!option_verbose && !option_debug && (!level)) {
		return;
	}
	ast_pthread_mutex_lock(&loglock);
	if (level == 1 /* Event */) {
		time(&t);
		localtime_r(&t,&tm);
		if (&tm) {
			/* Log events into the event log file, with a different format */
			strftime(date, sizeof(date), "%b %e %T", &tm);
			fprintf(eventlog, "%s asterisk[%d]: ", date, getpid());
			va_start(ap, fmt);
			vfprintf(eventlog, fmt, ap);
			va_end(ap);
			fflush(eventlog);
		} else
			/** Cannot use ast_log() from locked section of ast_log()!
			    ast_log(LOG_WARNING, "Unable to retrieve local time?\n"); **/
			fprintf(stderr, "ast_log: Unable to retrieve local time for %ld?\n", t);
	} else {
		if (logfiles) {
			f = logfiles;
			while(f) {
				if (f->logflags & (1 << level) && f->f) {
					if ((f->f != stdout) && (f->f != stderr)) {
						time(&t);
						localtime_r(&t,&tm);
						strftime(date, sizeof(date), "%b %e %T", &tm);
						fprintf(f->f, "%s %s[%ld]: File %s, Line %d (%s): ", date, levels[level], pthread_self(), file, line, function);
					} else {
						sprintf(linestr, "%d", line);
						fprintf(f->f, "%s[%ld]: File %s, Line %s (%s): ",
																term_color(tmp, levels[level], colors[level], 0, sizeof(tmp)),
																pthread_self(),
																term_color(tmp2, file, COLOR_BRWHITE, 0, sizeof(tmp2)),
																term_color(tmp3, linestr, COLOR_BRWHITE, 0, sizeof(tmp3)),
																term_color(tmp4, function, COLOR_BRWHITE, 0, sizeof(tmp4)));
					}
					va_start(ap, fmt);
					vfprintf(f->f, fmt, ap);
					va_end(ap);
					fflush(f->f);
				}
				f = f->next;
			}
		} else {
			fprintf(stdout, "%s[%ld]: File %s, Line %d (%s): ", levels[level], pthread_self(), file, line, function);
			va_start(ap, fmt);
			vfprintf(stdout, fmt, ap);
			va_end(ap);
			fflush(stdout);
		}
	}
	ast_pthread_mutex_unlock(&loglock);
}

extern void ast_verbose(const char *fmt, ...)
{
	static char stuff[4096];
	static int pos = 0, opos;
	static int replacelast = 0, complete;
	struct msglist *m;
	struct verb *v;
	va_list ap;
	va_start(ap, fmt);
	ast_pthread_mutex_lock(&msglist_lock);
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
	if (verboser) {
		v = verboser;
		while(v) {
			v->verboser(stuff, opos, replacelast, complete);
			v = v->next;
		}
	} /* else
		fprintf(stdout, stuff + opos); */

	if (fmt[strlen(fmt)-1] != '\n') 
		replacelast = 1;
	else 
		replacelast = pos = 0;
	va_end(ap);
	ast_pthread_mutex_unlock(&msglist_lock);
}

int ast_verbose_dmesg(void (*v)(const char *string, int opos, int replacelast, int complete))
{
	struct msglist *m;
	m = list;
	ast_pthread_mutex_lock(&msglist_lock);
	while(m) {
		/* Send all the existing entries that we have queued (i.e. they're likely to have missed) */
		v(m->msg, 0, 0, 1);
		m = m->next;
	}
	ast_pthread_mutex_unlock(&msglist_lock);
	return 0;
}

int ast_register_verbose(void (*v)(const char *string, int opos, int replacelast, int complete)) 
{
	struct msglist *m;
	struct verb *tmp;
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	if ((tmp = malloc(sizeof (struct verb)))) {
		tmp->verboser = v;
		ast_pthread_mutex_lock(&msglist_lock);
		tmp->next = verboser;
		verboser = tmp;
		m = list;
		while(m) {
			/* Send all the existing entries that we have queued (i.e. they're likely to have missed) */
			v(m->msg, 0, 0, 1);
			m = m->next;
		}
		ast_pthread_mutex_unlock(&msglist_lock);
		return 0;
	}
	return -1;
}

int ast_unregister_verbose(void (*v)(const char *string, int opos, int replacelast, int complete))
{
	int res = -1;
	struct verb *tmp, *tmpl=NULL;
	ast_pthread_mutex_lock(&msglist_lock);
	tmp = verboser;
	while(tmp) {
		if (tmp->verboser == v)	{
			if (tmpl)
				tmpl->next = tmp->next;
			else
				verboser = tmp->next;
			free(tmp);
			break;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	if (tmp)
		res = 0;
	ast_pthread_mutex_unlock(&msglist_lock);
	return res;
}
