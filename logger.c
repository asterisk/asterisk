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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#define SYSLOG_NAMES /* so we can map syslog facilities names to their numeric values,
		        from <syslog.h> which is included by logger.h */
#include <syslog.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"

static int syslog_level_map[] = {
	LOG_DEBUG,
	LOG_INFO,    /* arbitrary equivalent of LOG_EVENT */
	LOG_NOTICE,
	LOG_WARNING,
	LOG_ERR,
	LOG_DEBUG
};

#define SYSLOG_NLEVELS 6

#include "asterisk/logger.h"

#define MAX_MSG_QUEUE 200

#if defined(__linux__) && defined(__NR_gettid)
#include <asm/unistd.h>
#define GETTID() syscall(__NR_gettid)
#else
#define GETTID() getpid()
#endif

static char dateformat[256] = "%b %e %T";		/* Original Asterisk Format */

AST_MUTEX_DEFINE_STATIC(msglist_lock);
AST_MUTEX_DEFINE_STATIC(loglock);
static int filesize_reload_needed = 0;
static int global_logmask = -1;

static struct {
	unsigned int queue_log:1;
	unsigned int event_log:1;
} logfiles = { 1, 1 };

static struct msglist {
	char *msg;
	struct msglist *next;
} *list = NULL, *last = NULL;

static char hostname[MAXHOSTNAMELEN];

enum logtypes {
	LOGTYPE_SYSLOG,
	LOGTYPE_FILE,
	LOGTYPE_CONSOLE,
};

struct logchannel {
	int logmask;			/* What to log to this channel */
	int disabled;			/* If this channel is disabled or not */
	int facility; 			/* syslog facility */
	enum logtypes type;		/* Type of log channel */
	FILE *fileptr;			/* logfile logging file pointer */
	char filename[256];		/* Filename */
	struct logchannel *next;	/* Next channel in chain */
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
#ifndef SOLARIS
	CODE *cptr;
#endif

	if (ast_strlen_zero(channel))
		return NULL;
	chan = malloc(sizeof(struct logchannel));

	if (!chan)	/* Can't allocate memory */
		return NULL;

	memset(chan, 0, sizeof(struct logchannel));
	if (!strcasecmp(channel, "console")) {
		chan->type = LOGTYPE_CONSOLE;
	} else if (!strncasecmp(channel, "syslog", 6)) {
		/*
		* syntax is:
		*  syslog.facility => level,level,level
		*/
		facility = strchr(channel, '.');
		if(!facility++ || !facility) {
			facility = "local0";
		}

#ifndef SOLARIS
		/*
 		* Walk through the list of facilitynames (defined in sys/syslog.h)
		* to see if we can find the one we have been given
		*/
		chan->facility = -1;
 		cptr = facilitynames;
		while (cptr->c_name) {
			if (!strcasecmp(facility, cptr->c_name)) {
		 		chan->facility = cptr->c_val;
				break;
			}
			cptr++;
		}
#else
		chan->facility = -1;
		if (!strcasecmp(facility, "kern")) 
			chan->facility = LOG_KERN;
		else if (!strcasecmp(facility, "USER")) 
			chan->facility = LOG_USER;
		else if (!strcasecmp(facility, "MAIL")) 
			chan->facility = LOG_MAIL;
		else if (!strcasecmp(facility, "DAEMON")) 
			chan->facility = LOG_DAEMON;
		else if (!strcasecmp(facility, "AUTH")) 
			chan->facility = LOG_AUTH;
		else if (!strcasecmp(facility, "SYSLOG")) 
			chan->facility = LOG_SYSLOG;
		else if (!strcasecmp(facility, "LPR")) 
			chan->facility = LOG_LPR;
		else if (!strcasecmp(facility, "NEWS")) 
			chan->facility = LOG_NEWS;
		else if (!strcasecmp(facility, "UUCP")) 
			chan->facility = LOG_UUCP;
		else if (!strcasecmp(facility, "CRON")) 
			chan->facility = LOG_CRON;
		else if (!strcasecmp(facility, "LOCAL0")) 
			chan->facility = LOG_LOCAL0;
		else if (!strcasecmp(facility, "LOCAL1")) 
			chan->facility = LOG_LOCAL1;
		else if (!strcasecmp(facility, "LOCAL2")) 
			chan->facility = LOG_LOCAL2;
		else if (!strcasecmp(facility, "LOCAL3")) 
			chan->facility = LOG_LOCAL3;
		else if (!strcasecmp(facility, "LOCAL4")) 
			chan->facility = LOG_LOCAL4;
		else if (!strcasecmp(facility, "LOCAL5")) 
			chan->facility = LOG_LOCAL5;
		else if (!strcasecmp(facility, "LOCAL6")) 
			chan->facility = LOG_LOCAL6;
		else if (!strcasecmp(facility, "LOCAL7")) 
			chan->facility = LOG_LOCAL7;
#endif /* Solaris */

		if (0 > chan->facility) {
			fprintf(stderr, "Logger Warning: bad syslog facility in logger.conf\n");
			free(chan);
			return NULL;
		}

		chan->type = LOGTYPE_SYSLOG;
		snprintf(chan->filename, sizeof(chan->filename), "%s", channel);
		openlog("asterisk", LOG_PID, chan->facility);
	} else {
		if (channel[0] == '/') {
			if(!ast_strlen_zero(hostname)) { 
				snprintf(chan->filename, sizeof(chan->filename) - 1,"%s.%s", channel, hostname);
			} else {
				strncpy(chan->filename, channel, sizeof(chan->filename) - 1);
			}
		}		  
		
		if(!ast_strlen_zero(hostname)) {
			snprintf(chan->filename, sizeof(chan->filename), "%s/%s.%s",(char *)ast_config_AST_LOG_DIR, channel, hostname);
		} else {
			snprintf(chan->filename, sizeof(chan->filename), "%s/%s", (char *)ast_config_AST_LOG_DIR, channel);
		}
		chan->fileptr = fopen(chan->filename, "a");
		if (!chan->fileptr) {
			/* Can't log here, since we're called with a lock */
			fprintf(stderr, "Logger Warning: Unable to open log file '%s': %s\n", chan->filename, strerror(errno));
		} 
		chan->type = LOGTYPE_FILE;
	}
	chan->logmask = make_components(components, lineno);
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
	
	global_logmask = 0;
	/* close syslog */
	closelog();
	
	cfg = ast_config_load("logger.conf");
	
	/* If no config file, we're fine */
	if (!cfg)
		return;
	
	ast_mutex_lock(&loglock);
	if ((s = ast_variable_retrieve(cfg, "general", "appendhostname"))) {
		if(ast_true(s)) {
			if(gethostname(hostname, sizeof(hostname)-1)) {
				strncpy(hostname, "unknown", sizeof(hostname)-1);
				ast_log(LOG_WARNING, "What box has no hostname???\n");
			}
		} else
			hostname[0] = '\0';
	} else
		hostname[0] = '\0';
	if ((s = ast_variable_retrieve(cfg, "general", "dateformat"))) {
		strncpy(dateformat, s, sizeof(dateformat) - 1);
	} else
		strncpy(dateformat, "%b %e %T", sizeof(dateformat) - 1);
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log"))) {
		logfiles.queue_log = ast_true(s);
	}
	if ((s = ast_variable_retrieve(cfg, "general", "event_log"))) {
		logfiles.event_log = ast_true(s);
	}

	var = ast_variable_browse(cfg, "logfiles");
	while(var) {
		chan = make_logchannel(var->name, var->value, var->lineno);
		if (chan) {
			chan->next = logchannels;
			logchannels = chan;
			global_logmask |= chan->logmask;
		}
		var = var->next;
	}

	ast_config_destroy(cfg);
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
	if (logfiles.queue_log) {
		qlog = fopen(filename, "a");
	}
	ast_mutex_unlock(&qloglock);
	if (reloaded) 
		ast_queue_log("NONE", "NONE", "NONE", "CONFIGRELOAD", "%s", "");
	else
		ast_queue_log("NONE", "NONE", "NONE", "QUEUESTART", "%s", "");
}

int reload_logger(int rotate)
{
	char old[AST_CONFIG_MAX_PATH] = "";
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

	if (logfiles.event_log) {
		if (rotate) {
			for (x=0;;x++) {
				snprintf(new, sizeof(new), "%s/%s.%d", (char *)ast_config_AST_LOG_DIR, EVENTLOG,x);
				myf = fopen((char *)new, "r");
				if (myf) 	/* File exists */
					fclose(myf);
				else
					break;
			}
	
			/* do it */
			if (rename(old,new))
				fprintf(stderr, "Unable to rename file '%s' to '%s'\n", old, new);
		}

		eventlog = fopen(old, "a");
	}

	f = logchannels;
	while(f) {
		if (f->disabled) {
			f->disabled = 0;	/* Re-enable logging at reload */
			manager_event(EVENT_FLAG_SYSTEM, "LogChannel", "Channel: %s\r\nEnabled: Yes\r\n", f->filename);
		}
		if (f->fileptr && (f->fileptr != stdout) && (f->fileptr != stderr)) {
			fclose(f->fileptr);	/* Close file */
			f->fileptr = NULL;
			if(rotate) {
				strncpy(old, f->filename, sizeof(old) - 1);
	
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

	filesize_reload_needed = 0;

	queue_log_init();
	init_logger_chain();

	if (logfiles.event_log) {
		if (eventlog) {
			ast_log(LOG_EVENT, "Restarted Asterisk Event Logger\n");
			if (option_verbose)
				ast_verbose("Asterisk Event Logger restarted\n");
			return 0;
		} else 
			ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	}
	return -1;
}

static int handle_logger_reload(int fd, int argc, char *argv[])
{
	if(reload_logger(0)) {
		ast_cli(fd, "Failed to reload the logger\n");
		return RESULT_FAILURE;
	} else
		return RESULT_SUCCESS;
}

static int handle_logger_rotate(int fd, int argc, char *argv[])
{
	if(reload_logger(1)) {
		ast_cli(fd, "Failed to reload the logger and rotate log files\n");
		return RESULT_FAILURE;
	} else
		return RESULT_SUCCESS;
}

/*--- handle_logger_show_channels: CLI command to show logging system 
 	configuration */
static int handle_logger_show_channels(int fd, int argc, char *argv[])
{
#define FORMATL	"%-35.35s %-8.8s %-9.9s "
	struct logchannel *chan;

	ast_mutex_lock(&loglock);

	chan = logchannels;
	ast_cli(fd,FORMATL, "Channel", "Type", "Status");
	ast_cli(fd, "Configuration\n");
	ast_cli(fd,FORMATL, "-------", "----", "------");
	ast_cli(fd, "-------------\n");
	while (chan) {
		ast_cli(fd, FORMATL, chan->filename, chan->type==LOGTYPE_CONSOLE ? "Console" : (chan->type==LOGTYPE_SYSLOG ? "Syslog" : "File"),
			chan->disabled ? "Disabled" : "Enabled");
		ast_cli(fd, " - ");
		if (chan->logmask & (1 << __LOG_DEBUG)) 
			ast_cli(fd, "Debug ");
		if (chan->logmask & (1 << __LOG_VERBOSE)) 
			ast_cli(fd, "Verbose ");
		if (chan->logmask & (1 << __LOG_WARNING)) 
			ast_cli(fd, "Warning ");
		if (chan->logmask & (1 << __LOG_NOTICE)) 
			ast_cli(fd, "Notice ");
		if (chan->logmask & (1 << __LOG_ERROR)) 
			ast_cli(fd, "Error ");
		if (chan->logmask & (1 << __LOG_EVENT)) 
			ast_cli(fd, "Event ");
		ast_cli(fd, "\n");
		chan = chan->next;
	}
	ast_cli(fd, "\n");

	ast_mutex_unlock(&loglock);
 		
	return RESULT_SUCCESS;
}

static struct verb {
	void (*verboser)(const char *string, int opos, int replacelast, int complete);
	struct verb *next;
} *verboser = NULL;


static char logger_reload_help[] =
"Usage: logger reload\n"
"       Reloads the logger subsystem state.  Use after restarting syslogd(8) if you are using syslog logging.\n";

static char logger_rotate_help[] =
"Usage: logger rotate\n"
"       Rotates and Reopens the log files.\n";

static char logger_show_channels_help[] =
"Usage: logger show channels\n"
"       Show configured logger channels.\n";

static struct ast_cli_entry logger_show_channels_cli = 
	{ { "logger", "show", "channels", NULL }, 
	handle_logger_show_channels, "List configured log channels",
	logger_show_channels_help };

static struct ast_cli_entry reload_logger_cli = 
	{ { "logger", "reload", NULL }, 
	handle_logger_reload, "Reopens the log files",
	logger_reload_help };

static struct ast_cli_entry rotate_logger_cli = 
	{ { "logger", "rotate", NULL }, 
	handle_logger_rotate, "Rotates and reopens the log files",
	logger_rotate_help };

static int handle_SIGXFSZ(int sig) 
{
	/* Indicate need to reload */
	filesize_reload_needed = 1;
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
	ast_cli_register(&logger_show_channels_cli);

	/* initialize queue logger */
	queue_log_init();

	/* create log channels */
	init_logger_chain();

	/* create the eventlog */
	if (logfiles.event_log) {
		mkdir((char *)ast_config_AST_LOG_DIR, 0755);
		snprintf(tmp, sizeof(tmp), "%s/%s", (char *)ast_config_AST_LOG_DIR, EVENTLOG);
		eventlog = fopen((char *)tmp, "a");
		if (eventlog) {
			ast_log(LOG_EVENT, "Started Asterisk Event Logger\n");
			if (option_verbose)
				ast_verbose("Asterisk Event Logger Started %s\n",(char *)tmp);
			return 0;
		} else 
			ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	}

	return -1;
}

void close_logger(void)
{
	struct msglist *m, *tmp;

	ast_mutex_lock(&msglist_lock);
	m = list;
	while(m) {
		if (m->msg) {
			free(m->msg);
		}
		tmp = m->next;
		free(m);
		m = tmp;
	}
	list = last = NULL;
	msgcnt = 0;
	ast_mutex_unlock(&msglist_lock);
	return;
}

static void strip_coloring(char *str)
{
	char *src, *dest, *end;
	
	if (!str)
		return;

	/* find the first potential escape sequence in the string */

	src = strchr(str, '\033');
	if (!src)
		return;

	dest = src;
	while (*src) {
		/* at the top of this loop, *src will always be an ESC character */
		if ((src[1] == '[') && ((end = strchr(src + 2, 'm'))))
			src = end + 1;
		else
			*dest++ = *src++;

		/* copy characters, checking for ESC as we go */
		while (*src && (*src != '\033'))
			*dest++ = *src++;
	}

	*dest = '\0';
}

static void ast_log_vsyslog(int level, const char *file, int line, const char *function, const char *fmt, va_list args) 
{
	char buf[BUFSIZ];
	char *s;

	if (level >= SYSLOG_NLEVELS) {
		/* we are locked here, so cannot ast_log() */
		fprintf(stderr, "ast_log_vsyslog called with bogus level: %d\n", level);
		return;
	}
	if (level == __LOG_VERBOSE) {
		snprintf(buf, sizeof(buf), "VERBOSE[%ld]: ", (long)GETTID());
		level = __LOG_DEBUG;
	} else {
		snprintf(buf, sizeof(buf), "%s[%ld]: %s:%d in %s: ",
			 levels[level], (long)GETTID(), file, line, function);
	}
	s = buf + strlen(buf);
	vsnprintf(s, sizeof(buf) - strlen(buf), fmt, args);
	strip_coloring(s);
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
	
	/* don't display LOG_DEBUG messages unless option_verbose _or_ option_debug
	   are non-zero; LOG_DEBUG messages can still be displayed if option_debug
	   is zero, if option_verbose is non-zero (this allows for 'level zero'
	   LOG_DEBUG messages to be displayed, if the logmask on any channel
	   allows it)
	*/
	if (!option_verbose && !option_debug && (level == __LOG_DEBUG)) {
		return;
	}

	/* Ignore anything that never gets logged anywhere */
	if (!(global_logmask & (1 << level)))
		return;
	
	/* Ignore anything other than the currently debugged file if there is one */
	if ((level == __LOG_DEBUG) && !ast_strlen_zero(debug_filename) && strcasecmp(debug_filename, file))
		return;

	/* begin critical section */
	ast_mutex_lock(&loglock);

	time(&t);
	localtime_r(&t, &tm);
	strftime(date, sizeof(date), dateformat, &tm);

	if (logfiles.event_log && level == __LOG_EVENT) {
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
		while(chan && !chan->disabled) {
			/* Check syslog channels */
			if (chan->type == LOGTYPE_SYSLOG && (chan->logmask & (1 << level))) {
				va_start(ap, fmt);
				ast_log_vsyslog(level, file, line, function, fmt, ap);
				va_end(ap);
			/* Console channels */
			} else if ((chan->logmask & (1 << level)) && (chan->type == LOGTYPE_CONSOLE)) {
				char linestr[128];
				char tmp1[80], tmp2[80], tmp3[80], tmp4[80];

				if (level != __LOG_VERBOSE) {
					sprintf(linestr, "%d", line);
					snprintf(buf, sizeof(buf), option_timestamp ? "[%s] %s[%ld]: %s:%s %s: " : "%s %s[%ld]: %s:%s %s: ",
						date,
						term_color(tmp1, levels[level], colors[level], 0, sizeof(tmp1)),
						(long)GETTID(),
						term_color(tmp2, file, COLOR_BRWHITE, 0, sizeof(tmp2)),
						term_color(tmp3, linestr, COLOR_BRWHITE, 0, sizeof(tmp3)),
						term_color(tmp4, function, COLOR_BRWHITE, 0, sizeof(tmp4)));
					
					ast_console_puts(buf);
					va_start(ap, fmt);
					vsnprintf(buf, sizeof(buf), fmt, ap);
					va_end(ap);
					ast_console_puts(buf);
				}
			/* File channels */
			} else if ((chan->logmask & (1 << level)) && (chan->fileptr)) {
				int res;
				snprintf(buf, sizeof(buf), option_timestamp ? "[%s] %s[%ld]: " : "%s %s[%ld] %s: ", date,
					levels[level], (long)GETTID(), file);
				res = fprintf(chan->fileptr, buf);
				if (res <= 0 && buf[0] != '\0') {	/* Error, no characters printed */
					fprintf(stderr,"**** Asterisk Logging Error: ***********\n");
					if (errno == ENOMEM || errno == ENOSPC) {
						fprintf(stderr, "Asterisk logging error: Out of disk space, can't log to log file %s\n", chan->filename);
					} else
						fprintf(stderr, "Logger Warning: Unable to write to log file '%s': %s (disabled)\n", chan->filename, strerror(errno));
					manager_event(EVENT_FLAG_SYSTEM, "LogChannel", "Channel: %s\r\nEnabled: No\r\nReason: %d - %s\r\n", chan->filename, errno, strerror(errno));
					chan->disabled = 1;	
				} else {
					/* No error message, continue printing */
					va_start(ap, fmt);
					vsnprintf(buf, sizeof(buf), fmt, ap);
					va_end(ap);
					strip_coloring(buf);
					fputs(buf, chan->fileptr);
					fflush(chan->fileptr);
				}
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
	if (filesize_reload_needed) {
		reload_logger(1);
		ast_log(LOG_EVENT,"Rotated Logs Per SIGXFSZ (Exceeded file size limit)\n");
		if (option_verbose)
			ast_verbose("Rotated Logs Per SIGXFSZ (Exceeded file size limit)\n");
	}
}

extern void ast_verbose(const char *fmt, ...)
{
	static char stuff[4096];
	static int pos = 0, opos;
	static int replacelast = 0, complete;
	struct msglist *m;
	struct verb *v;
	time_t t;
	struct tm tm;
	char date[40];
	char *datefmt;
	
	va_list ap;
	va_start(ap, fmt);
	ast_mutex_lock(&msglist_lock);
	time(&t);
	localtime_r(&t, &tm);
	strftime(date, sizeof(date), dateformat, &tm);

	if (option_timestamp) {
		datefmt = alloca(strlen(date) + 3 + strlen(fmt) + 1);
		if (datefmt) {
			sprintf(datefmt, "[%s] %s", date, fmt);
			fmt = datefmt;
		}
	}
	vsnprintf(stuff + pos, sizeof(stuff) - pos, fmt, ap);
	opos = pos;
	pos = strlen(stuff);


	if (stuff[strlen(stuff)-1] == '\n') 
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
	ast_log(LOG_VERBOSE, "%s", stuff);
	if (strlen(stuff)) {
		if (stuff[strlen(stuff)-1] != '\n') 
			replacelast = 1;
		else 
			replacelast = pos = 0;
	}
	va_end(ap);

	ast_mutex_unlock(&msglist_lock);
}

int ast_verbose_dmesg(void (*v)(const char *string, int opos, int replacelast, int complete))
{
	struct msglist *m;
	ast_mutex_lock(&msglist_lock);
	m = list;
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
