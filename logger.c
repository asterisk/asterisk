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

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <asterisk/lock.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/term.h>
#include <asterisk/cli.h>
#include <asterisk/utils.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include "asterisk.h"
#include "astconf.h"

#define SYSLOG_NAMES /* so we can map syslog facilities names to their numeric values,
		        from <syslog.h> which is included by logger.h */
#include <syslog.h>
static int syslog_level_map[] = {
	LOG_DEBUG,
	LOG_INFO,    /* arbitrary equivalent of LOG_EVENT */
	LOG_NOTICE,
	LOG_WARNING,
	LOG_ERR,
        LOG_DEBUG
};
#define SYSLOG_NLEVELS 6

#include <asterisk/logger.h>

#define MAX_MSG_QUEUE 200

static char dateformat[256] = "%b %e %T";		/* Original Asterisk Format */
AST_MUTEX_DEFINE_STATIC(msglist_lock);
AST_MUTEX_DEFINE_STATIC(loglock);
static int pending_logger_reload = 0;

static struct msglist {
	char *msg;
	struct msglist *next;
} *list = NULL, *last = NULL;

struct logchannel {
        int logmask;
        int facility; /* syslog */
	int syslog; /* syslog flag */
        int console;  /* console logging */
	FILE *fileptr; /* logfile logging */
	char filename[256];
        struct logchannel *next;
};

static struct logchannel *logchannels = NULL;

static int msgcnt = 0;

static FILE *eventlog = NULL;

static char *levels[] = {
       "DEBUG",
       "EVENT",
       "NOTICE",
       "WARNING",
       "ERROR",
       "VERBOSE"
};

static int colors[] = {
       COLOR_BRGREEN,
       COLOR_BRBLUE,
       COLOR_YELLOW,
       COLOR_BRRED,
       COLOR_RED,
       COLOR_GREEN
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
	    if (!strcasecmp(w, "error")) 
		res |= (1 << __LOG_ERROR);
	    else if (!strcasecmp(w, "warning"))
		res |= (1 << __LOG_WARNING);
	    else if (!strcasecmp(w, "notice"))
		res |= (1 << __LOG_NOTICE);
	    else if (!strcasecmp(w, "event"))
		res |= (1 << __LOG_EVENT);
	    else if (!strcasecmp(w, "debug"))
		res |= (1 << __LOG_DEBUG);
	    else if (!strcasecmp(w, "verbose"))
		res |= (1 << __LOG_VERBOSE);
	    else {
		fprintf(stderr, "Logfile Warning: Unknown keyword '%s' at line %d of logger.conf\n", w, lineno);
	    }
	    w = strsep(&stringp, ",");
	}
	return res;
}

static struct logchannel *make_logchannel(char *channel, char *components, int lineno)
{
	struct logchannel *chan;
	char *facility;
	CODE *cptr;

	if (ast_strlen_zero(channel))
		return NULL;
	chan = malloc(sizeof(struct logchannel));

	if (chan) {
		memset(chan, 0, sizeof(struct logchannel));
		if (!strcasecmp(channel, "console")) {
		    chan->console = 1;
		} else if (!strncasecmp(channel, "syslog", 6)) {
		    /*
		     * syntax is:
		     *  syslog.facility => level,level,level
		     */
		    facility = strchr(channel, '.');
		    if(!facility++ || !facility) {
			facility = "local0";
		    }
		    /*
		     * Walk through the list of facilitynames (defined in sys/syslog.h)
		     * to see if we can find the one we have been given
		     */
		    chan->facility = -1;
		    cptr = facilitynames;
		    while (cptr->c_name) {
			if (!strncasecmp(facility, cptr->c_name, sizeof(cptr->c_name))) {
			    chan->facility = cptr->c_val;
			    break;
			}
			cptr++;
		    }
		    if (0 > chan->facility) {
			fprintf(stderr, "Logger Warning: bad syslog facility in logger.conf\n");
			free(chan);
			return NULL;
		    }

		    chan->syslog = 1;
		    openlog("asterisk", LOG_PID, chan->facility);
		} else {
			if (channel[0] == '/') 
				strncpy(chan->filename, channel, sizeof(chan->filename) - 1);
			else
				snprintf(chan->filename, sizeof(chan->filename), "%s/%s", (char *)ast_config_AST_LOG_DIR, channel);
			chan->fileptr = fopen(chan->filename, "a");
			if (!chan->fileptr) {
				/* Can't log here, since we're called with a lock */
				fprintf(stderr, "Logger Warning: Unable to open log file '%s': %s\n", chan->filename, strerror(errno));
			}
		}
		chan->logmask = make_components(components, lineno);
	}
	return chan;
}

static void init_logger_chain(void)
{
	struct logchannel *chan, *cur;
	struct ast_config *cfg;
	struct ast_variable *var;
	char *s;

	/* delete our list of log channels */
	ast_mutex_lock(&loglock);
	chan = logchannels;
	while (chan) {
	    cur = chan->next;
	    free(chan);
	    chan = cur;
	}
	logchannels = NULL;
	ast_mutex_unlock(&loglock);

	/* close syslog */
	closelog();

	cfg = ast_load("logger.conf");
	
	/* If no config file, we're fine */
	if (!cfg)
	    return;

	ast_mutex_lock(&loglock);
	if ((s = ast_variable_retrieve(cfg, "general", "dateformat"))) {
		(void)strncpy(dateformat,s,sizeof(dateformat));
	}
	var = ast_variable_browse(cfg, "logfiles");
	while(var) {
		chan = make_logchannel(var->name, var->value, var->lineno);
		if (chan) {
			chan->next = logchannels;
			logchannels = chan;
		}
		var = var->next;
	}

	ast_destroy(cfg);
	ast_mutex_unlock(&loglock);
}

static FILE *qlog = NULL;
AST_MUTEX_DEFINE_STATIC(qloglock);

void ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
{
	va_list ap;
	ast_mutex_lock(&qloglock);
	if (qlog) {
		va_start(ap, fmt);
		fprintf(qlog, "%ld|%s|%s|%s|%s|", (long)time(NULL), callid, queuename, agent, event);
		vfprintf(qlog, fmt, ap);
		fprintf(qlog, "\n");
		va_end(ap);
		fflush(qlog);
	}
	ast_mutex_unlock(&qloglock);
}

static void queue_log_init(void)
{
	char filename[256];
	int reloaded = 0;
	ast_mutex_lock(&qloglock);
	if (qlog) {
		reloaded = 1;
		fclose(qlog);
		qlog = NULL;
	}
	snprintf(filename, sizeof(filename), "%s/%s", (char *)ast_config_AST_LOG_DIR, "queue_log");
	qlog = fopen(filename, "a");
	ast_mutex_unlock(&qloglock);
	if (reloaded) 
		ast_queue_log("NONE", "NONE", "NONE", "CONFIGRELOAD", "%s", "");
	else
		ast_queue_log("NONE", "NONE", "NONE", "QUEUESTART", "%s", "");
}

int reload_logger(int rotate)
{
	char old[AST_CONFIG_MAX_PATH];
	char new[AST_CONFIG_MAX_PATH];
	struct logchannel *f;
	FILE *myf;

	int x;
	ast_mutex_lock(&loglock);
	if (eventlog) 
		fclose(eventlog);
	else 
		rotate = 0;
	eventlog = NULL;



	mkdir((char *)ast_config_AST_LOG_DIR, 0755);
	snprintf(old, sizeof(old), "%s/%s", (char *)ast_config_AST_LOG_DIR, EVENTLOG);

	if(rotate) {
		for(x=0;;x++) {
			snprintf(new, sizeof(new), "%s/%s.%d", (char *)ast_config_AST_LOG_DIR, EVENTLOG,x);
			myf = fopen((char *)new, "r");
			if(myf) 
				fclose(myf);
			else
				break;
		}
	
		/* do it */
		if (rename(old,new))
			fprintf(stderr, "Unable to rename file '%s' to '%s'\n", old, new);
	}

	eventlog = fopen(old, "a");

	f = logchannels;
	while(f) {
		if (f->fileptr && (f->fileptr != stdout) && (f->fileptr != stderr)) {
			fclose(f->fileptr);
			f->fileptr = NULL;
			if(rotate) {
				strncpy(old, f->filename, sizeof(old));
	
				for(x=0;;x++) {
					snprintf(new, sizeof(new), "%s.%d", f->filename, x);
					myf = fopen((char *)new, "r");
					if (myf) {
						fclose(myf);
					} else {
						break;
					}
				}
	    
				/* do it */
				if (rename(old,new))
					fprintf(stderr, "Unable to rename file '%s' to '%s'\n", old, new);
			}
		}
		f = f->next;
	}

	ast_mutex_unlock(&loglock);

	queue_log_init();

	if (eventlog) {
		init_logger_chain();
		ast_log(LOG_EVENT, "Restarted Asterisk Event Logger\n");
		if (option_verbose)
			ast_verbose("Asterisk Event Logger restarted\n");
		return 0;
	} else 
		ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	init_logger_chain();
	pending_logger_reload = 0;
	return -1;
}

static int handle_logger_reload(int fd, int argc, char *argv[])
{
	if(reload_logger(0))
	{
		ast_cli(fd, "Failed to reloadthe logger\n");
		return RESULT_FAILURE;
	}
	else
		return RESULT_SUCCESS;
}

static int handle_logger_rotate(int fd, int argc, char *argv[])
{
	if(reload_logger(1))
	{
		ast_cli(fd, "Failed to reloadthe logger\n");
		return RESULT_FAILURE;
	}
	else
		return RESULT_SUCCESS;
}

static struct verb {
	void (*verboser)(const char *string, int opos, int replacelast, int complete);
	struct verb *next;
} *verboser = NULL;


static char logger_reload_help[] =
"Usage: logger reload\n"
"       Reloads the logger subsystem state.  Use after restarting syslogd(8)\n";

static char logger_rotate_help[] =
"Usage: logger rotate\n"
"       Rotates and Reopens the log files.\n";

static struct ast_cli_entry reload_logger_cli = 
	{ { "logger", "reload", NULL }, 
	handle_logger_reload, "Reopens the log files",
	logger_reload_help };

static struct ast_cli_entry rotate_logger_cli = 
	{ { "logger", "rotate", NULL }, 
	handle_logger_rotate, "Rotates and reopens the log files",
	logger_rotate_help };

static int handle_SIGXFSZ(int sig) {
	/* Indicate need to reload */
	pending_logger_reload = 1;
    return 0;
}

int init_logger(void)
{
	char tmp[256];

	/* auto rotate if sig SIGXFSZ comes a-knockin */
	(void) signal(SIGXFSZ,(void *) handle_SIGXFSZ);

	/* register the relaod logger cli command */
	ast_cli_register(&reload_logger_cli);
	ast_cli_register(&rotate_logger_cli);

	/* initialize queue logger */
	queue_log_init();

	/* create the eventlog */
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

	/* create log channels */
	init_logger_chain();
	return -1;
}

static void ast_log_vsyslog(int level, const char *file, int line, const char *function, const char *fmt, va_list args) {
    char buf[BUFSIZ];

    if(level >= SYSLOG_NLEVELS) {
	    /* we are locked here, so cannot ast_log() */
	    fprintf(stderr, "ast_log_vsyslog called with bogus level: %d\n", level);
	    return;
    }
    if(level == __LOG_VERBOSE) {
	snprintf(buf, sizeof(buf), "VERBOSE[%ld]: ", (long)pthread_self());
	level = __LOG_DEBUG;
    } else {
	snprintf(buf, sizeof(buf), "%s[%ld]: %s:%d in %s: ",
		 levels[level], (long)pthread_self(), file, line, function);
    }
    vsnprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), fmt, args);
    syslog(syslog_level_map[level], "%s", buf);
}

/*
 * send log messages to syslog and/or the console
 */
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
    struct logchannel *chan;
    char buf[BUFSIZ];
    time_t t;
    struct tm tm;
    char date[256];

    va_list ap;
	
    if (!option_verbose && !option_debug && (level == __LOG_DEBUG)) {
	return;
    }

    /* begin critical section */
    ast_mutex_lock(&loglock);

    time(&t);
    localtime_r(&t, &tm);
    strftime(date, sizeof(date), dateformat, &tm);


    if (level == __LOG_EVENT) {
	    va_start(ap, fmt);

	    fprintf(eventlog, "%s asterisk[%d]: ", date, getpid());
	    vfprintf(eventlog, fmt, ap);
	    fflush(eventlog);

	    va_end(ap);
	    ast_mutex_unlock(&loglock);
	    return;
    }

    if (logchannels) {
	chan = logchannels;
	while(chan) {
	    if (chan->syslog && (chan->logmask & (1 << level))) {
		va_start(ap, fmt);
		ast_log_vsyslog(level, file, line, function, fmt, ap);
		va_end(ap);
	    } else if ((chan->logmask & (1 << level)) && (chan->console)) {
		char linestr[128];
		char tmp1[80], tmp2[80], tmp3[80], tmp4[80];

		if(level != __LOG_VERBOSE) {
		    sprintf(linestr, "%d", line);
		    snprintf(buf, sizeof(buf), "%s %s[%ld]: %s:%s %s: ",
			     date,
			     term_color(tmp1, levels[level], colors[level], 0, sizeof(tmp1)),
			     (long)pthread_self(),
			     term_color(tmp2, file, COLOR_BRWHITE, 0, sizeof(tmp2)),
			     term_color(tmp3, linestr, COLOR_BRWHITE, 0, sizeof(tmp3)),
			     term_color(tmp4, function, COLOR_BRWHITE, 0, sizeof(tmp4)));
		    
		    ast_console_puts(buf);
		    va_start(ap, fmt);
		    vsnprintf(buf, sizeof(buf), fmt, ap);
		    va_end(ap);
		    ast_console_puts(buf);
		}
	    } else if ((chan->logmask & (1 << level)) && (chan->fileptr)) {
		    snprintf(buf, sizeof(buf), "%s %s[%ld]: ", date,
			     levels[level], (long)pthread_self());
		    fprintf(chan->fileptr, buf);
		    va_start(ap, fmt);
		    vsnprintf(buf, sizeof(buf), fmt, ap);
		    va_end(ap);
		    fputs(buf, chan->fileptr);
		    fflush(chan->fileptr);
	    }
	    chan = chan->next;
	}
    } else {
	    /* 
	     * we don't have the logger chain configured yet,
	     * so just log to stdout 
	     */
		if (level != __LOG_VERBOSE) {
		    va_start(ap, fmt);
		    vsnprintf(buf, sizeof(buf), fmt, ap);
		    va_end(ap);
		    fputs(buf, stdout);
		}
    }

    ast_mutex_unlock(&loglock);
    /* end critical section */
	if (pending_logger_reload) {
	    reload_logger(1);
	    ast_log(LOG_EVENT,"Rotated Logs Per SIGXFSZ\n");
	    if (option_verbose)
		    ast_verbose("Rotated Logs Per SIGXFSZ\n");
	}
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
	ast_mutex_lock(&msglist_lock);
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
				ast_log(LOG_ERROR, "Out of memory\n");
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

	ast_log(LOG_VERBOSE, stuff);

	if (fmt[strlen(fmt)-1] != '\n') 
		replacelast = 1;
	else 
		replacelast = pos = 0;
	va_end(ap);

	ast_mutex_unlock(&msglist_lock);
}

int ast_verbose_dmesg(void (*v)(const char *string, int opos, int replacelast, int complete))
{
	struct msglist *m;
	m = list;
	ast_mutex_lock(&msglist_lock);
	while(m) {
		/* Send all the existing entries that we have queued (i.e. they're likely to have missed) */
		v(m->msg, 0, 0, 1);
		m = m->next;
	}
	ast_mutex_unlock(&msglist_lock);
	return 0;
}

int ast_register_verbose(void (*v)(const char *string, int opos, int replacelast, int complete)) 
{
	struct msglist *m;
	struct verb *tmp;
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	if ((tmp = malloc(sizeof (struct verb)))) {
		tmp->verboser = v;
		ast_mutex_lock(&msglist_lock);
		tmp->next = verboser;
		verboser = tmp;
		m = list;
		while(m) {
			/* Send all the existing entries that we have queued (i.e. they're likely to have missed) */
			v(m->msg, 0, 0, 1);
			m = m->next;
		}
		ast_mutex_unlock(&msglist_lock);
		return 0;
	}
	return -1;
}

int ast_unregister_verbose(void (*v)(const char *string, int opos, int replacelast, int complete))
{
	int res = -1;
	struct verb *tmp, *tmpl=NULL;
	ast_mutex_lock(&msglist_lock);
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
	ast_mutex_unlock(&msglist_lock);
	return res;
}
