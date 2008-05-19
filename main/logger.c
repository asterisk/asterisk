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
 * \brief Asterisk Logger
 * 
 * Logging routines
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*
 * define _ASTERISK_LOGGER_H to prevent the inclusion of logger.h;
 * it redefines LOG_* which we need to define syslog_level_map.
 * Later, we force the inclusion of logger.h again.
 */
#define _ASTERISK_LOGGER_H
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_BKTR
#include <execinfo.h>
#define MAX_BACKTRACE_FRAMES 20
#endif

#define SYSLOG_NAMES /* so we can map syslog facilities names to their numeric values,
		        from <syslog.h> which is included by logger.h */
#include <syslog.h>

static int syslog_level_map[] = {
	LOG_DEBUG,
	LOG_INFO,    /* arbitrary equivalent of LOG_EVENT */
	LOG_NOTICE,
	LOG_WARNING,
	LOG_ERR,
	LOG_DEBUG,
	LOG_DEBUG
};

#define SYSLOG_NLEVELS sizeof(syslog_level_map) / sizeof(int)

#undef _ASTERISK_LOGGER_H	/* now include logger.h */
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"
#include "asterisk/pbx.h"

#if defined(__linux__) && !defined(__NR_gettid)
#include <asm/unistd.h>
#endif

#if defined(__linux__) && defined(__NR_gettid)
#define GETTID() syscall(__NR_gettid)
#else
#define GETTID() getpid()
#endif

static char dateformat[256] = "%b %e %T";		/* Original Asterisk Format */

static char queue_log_name[256] = QUEUELOG;
static char exec_after_rotate[256] = "";

static int filesize_reload_needed;
static int global_logmask = -1;

enum rotatestrategy {
	SEQUENTIAL = 1 << 0,     /* Original method - create a new file, in order */
	ROTATE = 1 << 1,         /* Rotate all files, such that the oldest file has the highest suffix */
	TIMESTAMP = 1 << 2,      /* Append the epoch timestamp onto the end of the archived file */
} rotatestrategy = SEQUENTIAL;

static struct {
	unsigned int queue_log:1;
	unsigned int event_log:1;
} logfiles = { 1, 1 };

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
	AST_LIST_ENTRY(logchannel) list;
};

static AST_RWLIST_HEAD_STATIC(logchannels, logchannel);

enum logmsgtypes {
	LOGMSG_NORMAL = 0,
	LOGMSG_VERBOSE,
};

struct logmsg {
	enum logmsgtypes type;
	char date[256];
	int level;
	const char *file;
	int line;
	const char *function;
	AST_LIST_ENTRY(logmsg) list;
	char str[0];
};

static AST_LIST_HEAD_STATIC(logmsgs, logmsg);
static pthread_t logthread = AST_PTHREADT_NULL;
static ast_cond_t logcond;
static int close_logger_thread;

static FILE *eventlog;
static FILE *qlog;

/*! \brief Logging channels used in the Asterisk logging system */
static char *levels[] = {
	"DEBUG",
	"EVENT",
	"NOTICE",
	"WARNING",
	"ERROR",
	"VERBOSE",
	"DTMF"
};

/*! \brief Colors used in the console for logging */
static int colors[] = {
	COLOR_BRGREEN,
	COLOR_BRBLUE,
	COLOR_YELLOW,
	COLOR_BRRED,
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BRGREEN
};

AST_THREADSTORAGE(verbose_buf);
#define VERBOSE_BUF_INIT_SIZE   256

AST_THREADSTORAGE(log_buf);
#define LOG_BUF_INIT_SIZE       256

static int make_components(const char *s, int lineno)
{
	char *w;
	int res = 0;
	char *stringp = ast_strdupa(s);

	while ((w = strsep(&stringp, ","))) {
		w = ast_skip_blanks(w);
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
		else if (!strcasecmp(w, "dtmf"))
			res |= (1 << __LOG_DTMF);
		else {
			fprintf(stderr, "Logfile Warning: Unknown keyword '%s' at line %d of logger.conf\n", w, lineno);
		}
	}

	return res;
}

static struct logchannel *make_logchannel(const char *channel, const char *components, int lineno)
{
	struct logchannel *chan;
	char *facility;
#ifndef SOLARIS
	CODE *cptr;
#endif

	if (ast_strlen_zero(channel) || !(chan = ast_calloc(1, sizeof(*chan))))
		return NULL;

	if (!strcasecmp(channel, "console")) {
		chan->type = LOGTYPE_CONSOLE;
	} else if (!strncasecmp(channel, "syslog", 6)) {
		/*
		* syntax is:
		*  syslog.facility => level,level,level
		*/
		facility = strchr(channel, '.');
		if (!facility++ || !facility) {
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
			ast_free(chan);
			return NULL;
		}

		chan->type = LOGTYPE_SYSLOG;
		snprintf(chan->filename, sizeof(chan->filename), "%s", channel);
		openlog("asterisk", LOG_PID, chan->facility);
	} else {
		if (channel[0] == '/') {
			if (!ast_strlen_zero(hostname)) { 
				snprintf(chan->filename, sizeof(chan->filename), "%s.%s", channel, hostname);
			} else {
				ast_copy_string(chan->filename, channel, sizeof(chan->filename));
			}
		}		  
		
		if (!ast_strlen_zero(hostname)) {
			snprintf(chan->filename, sizeof(chan->filename), "%s/%s.%s", ast_config_AST_LOG_DIR, channel, hostname);
		} else {
			snprintf(chan->filename, sizeof(chan->filename), "%s/%s", ast_config_AST_LOG_DIR, channel);
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

static void init_logger_chain(int locked)
{
	struct logchannel *chan;
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *s;
	struct ast_flags config_flags = { 0 };

	if (!(cfg = ast_config_load2("logger.conf", "logger", config_flags)))
		return;

	/* delete our list of log channels */
	if (!locked)
		AST_RWLIST_WRLOCK(&logchannels);
	while ((chan = AST_RWLIST_REMOVE_HEAD(&logchannels, list)))
		ast_free(chan);
	if (!locked)
		AST_RWLIST_UNLOCK(&logchannels);
	
	global_logmask = 0;
	errno = 0;
	/* close syslog */
	closelog();
	
	/* If no config file, we're fine, set default options. */
	if (!cfg) {
		if (errno)
			fprintf(stderr, "Unable to open logger.conf: %s; default settings will be used.\n", strerror(errno));
		else
			fprintf(stderr, "Errors detected in logger.conf: see above; default settings will be used.\n");
		if (!(chan = ast_calloc(1, sizeof(*chan))))
			return;
		chan->type = LOGTYPE_CONSOLE;
		chan->logmask = 28; /*warning,notice,error */
		if (!locked)
			AST_RWLIST_WRLOCK(&logchannels);
		AST_RWLIST_INSERT_HEAD(&logchannels, chan, list);
		if (!locked)
			AST_RWLIST_UNLOCK(&logchannels);
		global_logmask |= chan->logmask;
		return;
	}
	
	if ((s = ast_variable_retrieve(cfg, "general", "appendhostname"))) {
		if (ast_true(s)) {
			if (gethostname(hostname, sizeof(hostname) - 1)) {
				ast_copy_string(hostname, "unknown", sizeof(hostname));
				fprintf(stderr, "What box has no hostname???\n");
			}
		} else
			hostname[0] = '\0';
	} else
		hostname[0] = '\0';
	if ((s = ast_variable_retrieve(cfg, "general", "dateformat")))
		ast_copy_string(dateformat, s, sizeof(dateformat));
	else
		ast_copy_string(dateformat, "%b %e %T", sizeof(dateformat));
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log")))
		logfiles.queue_log = ast_true(s);
	if ((s = ast_variable_retrieve(cfg, "general", "event_log")))
		logfiles.event_log = ast_true(s);
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log_name")))
		ast_copy_string(queue_log_name, s, sizeof(queue_log_name));
	if ((s = ast_variable_retrieve(cfg, "general", "exec_after_rotate")))
		ast_copy_string(exec_after_rotate, s, sizeof(exec_after_rotate));
	if ((s = ast_variable_retrieve(cfg, "general", "rotatestrategy"))) {
		if (strcasecmp(s, "timestamp") == 0)
			rotatestrategy = TIMESTAMP;
		else if (strcasecmp(s, "rotate") == 0)
			rotatestrategy = ROTATE;
		else if (strcasecmp(s, "sequential") == 0)
			rotatestrategy = SEQUENTIAL;
		else
			fprintf(stderr, "Unknown rotatestrategy: %s\n", s);
	} else {
		if ((s = ast_variable_retrieve(cfg, "general", "rotatetimestamp"))) {
			rotatestrategy = ast_true(s) ? TIMESTAMP : SEQUENTIAL;
			fprintf(stderr, "rotatetimestamp option has been deprecated.  Please use rotatestrategy instead.\n");
		}
	}

	if (!locked)
		AST_RWLIST_WRLOCK(&logchannels);
	var = ast_variable_browse(cfg, "logfiles");
	for (; var; var = var->next) {
		if (!(chan = make_logchannel(var->name, var->value, var->lineno)))
			continue;
		AST_RWLIST_INSERT_HEAD(&logchannels, chan, list);
		global_logmask |= chan->logmask;
	}
	if (!locked)
		AST_RWLIST_UNLOCK(&logchannels);

	ast_config_destroy(cfg);
}

void ast_child_verbose(int level, const char *fmt, ...)
{
	char *msg = NULL, *emsg = NULL, *sptr, *eptr;
	va_list ap, aq;
	int size;

	/* Don't bother, if the level isn't that high */
	if (option_verbose < level) {
		return;
	}

	va_start(ap, fmt);
	va_copy(aq, ap);
	if ((size = vsnprintf(msg, 0, fmt, ap)) < 0) {
		va_end(ap);
		va_end(aq);
		return;
	}
	va_end(ap);

	if (!(msg = ast_malloc(size + 1))) {
		va_end(aq);
		return;
	}

	vsnprintf(msg, size + 1, fmt, aq);
	va_end(aq);

	if (!(emsg = ast_malloc(size * 2 + 1))) {
		ast_free(msg);
		return;
	}

	for (sptr = msg, eptr = emsg; ; sptr++) {
		if (*sptr == '"') {
			*eptr++ = '\\';
		}
		*eptr++ = *sptr;
		if (*sptr == '\0') {
			break;
		}
	}
	ast_free(msg);

	fprintf(stdout, "verbose \"%s\" %d\n", emsg, level);
	fflush(stdout);
	ast_free(emsg);
}

void ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
{
	va_list ap;
	char qlog_msg[8192];
	int qlog_len;
	char time_str[16];

	if (ast_check_realtime("queue_log")) {
		va_start(ap, fmt);
		vsnprintf(qlog_msg, sizeof(qlog_msg), fmt, ap);
		va_end(ap);
		snprintf(time_str, sizeof(time_str), "%ld", (long)time(NULL));
		ast_store_realtime("queue_log", "time", time_str, 
						"callid", callid, 
						"queuename", queuename, 
						"agent", agent, 
						"event", event,
						"data", qlog_msg,
						NULL);
	} else {
		if (qlog) {
			va_start(ap, fmt);
			qlog_len = snprintf(qlog_msg, sizeof(qlog_msg), "%ld|%s|%s|%s|%s|", (long)time(NULL), callid, queuename, agent, event);
			vsnprintf(qlog_msg + qlog_len, sizeof(qlog_msg) - qlog_len, fmt, ap);
			va_end(ap);
		}
		AST_RWLIST_RDLOCK(&logchannels);
		if (qlog) {
			fprintf(qlog, "%s\n", qlog_msg);
			fflush(qlog);
		}
		AST_RWLIST_UNLOCK(&logchannels);
	}
}

static int rotate_file(const char *filename)
{
	char old[PATH_MAX];
	char new[PATH_MAX];
	int x, y, which, found, res = 0, fd;
	char *suffixes[4] = { "", ".gz", ".bz2", ".Z" };

	switch (rotatestrategy) {
	case SEQUENTIAL:
		for (x = 0; ; x++) {
			snprintf(new, sizeof(new), "%s.%d", filename, x);
			fd = open(new, O_RDONLY);
			if (fd > -1)
				close(fd);
			else
				break;
		}
		if (rename(filename, new)) {
			fprintf(stderr, "Unable to rename file '%s' to '%s'\n", filename, new);
			res = -1;
		}
		break;
	case TIMESTAMP:
		snprintf(new, sizeof(new), "%s.%ld", filename, (long)time(NULL));
		if (rename(filename, new)) {
			fprintf(stderr, "Unable to rename file '%s' to '%s'\n", filename, new);
			res = -1;
		}
		break;
	case ROTATE:
		/* Find the next empty slot, including a possible suffix */
		for (x = 0; ; x++) {
			found = 0;
			for (which = 0; which < sizeof(suffixes) / sizeof(suffixes[0]); which++) {
				snprintf(new, sizeof(new), "%s.%d%s", filename, x, suffixes[which]);
				fd = open(new, O_RDONLY);
				if (fd > -1)
					close(fd);
				else {
					found = 1;
					break;
				}
			}
			if (!found)
				break;
		}

		/* Found an empty slot */
		for (y = x; y > -1; y--) {
			for (which = 0; which < sizeof(suffixes) / sizeof(suffixes[0]); which++) {
				snprintf(old, sizeof(old), "%s.%d%s", filename, y - 1, suffixes[which]);
				fd = open(old, O_RDONLY);
				if (fd > -1) {
					/* Found the right suffix */
					close(fd);
					snprintf(new, sizeof(new), "%s.%d%s", filename, y, suffixes[which]);
					if (rename(old, new)) {
						fprintf(stderr, "Unable to rename file '%s' to '%s'\n", old, new);
						res = -1;
					}
					break;
				}
			}
		}

		/* Finally, rename the current file */
		snprintf(new, sizeof(new), "%s.0", filename);
		if (rename(filename, new)) {
			fprintf(stderr, "Unable to rename file '%s' to '%s'\n", filename, new);
			res = -1;
		}
	}

	if (!ast_strlen_zero(exec_after_rotate)) {
		struct ast_channel *c = ast_channel_alloc(0, 0, "", "", "", "", "", 0, "Logger/rotate");
		char buf[512];
		pbx_builtin_setvar_helper(c, "filename", filename);
		pbx_substitute_variables_helper(c, exec_after_rotate, buf, sizeof(buf));
		system(buf);
		ast_channel_free(c);
	}
	return res;
}

static int reload_logger(int rotate)
{
	char old[PATH_MAX] = "";
	int event_rotate = rotate, queue_rotate = rotate;
	struct logchannel *f;
	int res = 0;
	struct stat st;

	AST_RWLIST_WRLOCK(&logchannels);

	if (eventlog) {
		if (rotate < 0) {
			/* Check filesize - this one typically doesn't need an auto-rotate */
			snprintf(old, sizeof(old), "%s/%s", ast_config_AST_LOG_DIR, EVENTLOG);
			if (stat(old, &st) != 0 || st.st_size > 0x40000000) { /* Arbitrarily, 1 GB */
				fclose(eventlog);
				eventlog = NULL;
			} else
				event_rotate = 0;
		} else {
			fclose(eventlog);
			eventlog = NULL;
		}
	} else
		event_rotate = 0;

	if (qlog) {
		if (rotate < 0) {
			/* Check filesize - this one typically doesn't need an auto-rotate */
			snprintf(old, sizeof(old), "%s/%s", ast_config_AST_LOG_DIR, queue_log_name);
			if (stat(old, &st) != 0 || st.st_size > 0x40000000) { /* Arbitrarily, 1 GB */
				fclose(qlog);
				qlog = NULL;
			} else
				event_rotate = 0;
		} else {
			fclose(qlog);
			qlog = NULL;
		}
	} else 
		queue_rotate = 0;
	qlog = NULL;

	ast_mkdir(ast_config_AST_LOG_DIR, 0777);

	AST_RWLIST_TRAVERSE(&logchannels, f, list) {
		if (f->disabled) {
			f->disabled = 0;	/* Re-enable logging at reload */
			manager_event(EVENT_FLAG_SYSTEM, "LogChannel", "Channel: %s\r\nEnabled: Yes\r\n", f->filename);
		}
		if (f->fileptr && (f->fileptr != stdout) && (f->fileptr != stderr)) {
			fclose(f->fileptr);	/* Close file */
			f->fileptr = NULL;
			if (rotate)
				rotate_file(f->filename);
		}
	}

	filesize_reload_needed = 0;

	init_logger_chain(1 /* locked */);

	if (logfiles.event_log) {
		snprintf(old, sizeof(old), "%s/%s", ast_config_AST_LOG_DIR, EVENTLOG);
		if (event_rotate)
			rotate_file(old);

		eventlog = fopen(old, "a");
		if (eventlog) {
			ast_log(LOG_EVENT, "Restarted Asterisk Event Logger\n");
			ast_verb(1, "Asterisk Event Logger restarted\n");
		} else {
			ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
			res = -1;
		}
	}

	if (logfiles.queue_log) {
		snprintf(old, sizeof(old), "%s/%s", ast_config_AST_LOG_DIR, queue_log_name);
		if (queue_rotate)
			rotate_file(old);

		qlog = fopen(old, "a");
		if (qlog) {
			AST_RWLIST_UNLOCK(&logchannels);
			ast_queue_log("NONE", "NONE", "NONE", "CONFIGRELOAD", "%s", "");
			AST_RWLIST_WRLOCK(&logchannels);
			ast_log(LOG_EVENT, "Restarted Asterisk Queue Logger\n");
			ast_verb(1, "Asterisk Queue Logger restarted\n");
		} else {
			ast_log(LOG_ERROR, "Unable to create queue log: %s\n", strerror(errno));
			res = -1;
		}
	}

	AST_RWLIST_UNLOCK(&logchannels);

	return res;
}

/*! \brief Reload the logger module without rotating log files (also used from loader.c during
	a full Asterisk reload) */
int logger_reload(void)
{
	if(reload_logger(0))
		return RESULT_FAILURE;
	return RESULT_SUCCESS;
}

static char *handle_logger_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "logger reload";
		e->usage = 
			"Usage: logger reload\n"
			"       Reloads the logger subsystem state.  Use after restarting syslogd(8) if you are using syslog logging.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (reload_logger(0)) {
		ast_cli(a->fd, "Failed to reload the logger\n");
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

static char *handle_logger_rotate(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "logger rotate";
		e->usage = 
			"Usage: logger rotate\n"
			"       Rotates and Reopens the log files.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;	
	}
	if (reload_logger(1)) {
		ast_cli(a->fd, "Failed to reload the logger and rotate log files\n");
		return CLI_FAILURE;
	} 
	return CLI_SUCCESS;
}

/*! \brief CLI command to show logging system configuration */
static char *handle_logger_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMATL	"%-35.35s %-8.8s %-9.9s "
	struct logchannel *chan;
	switch (cmd) {
	case CLI_INIT:
		e->command = "logger show channels";
		e->usage = 
			"Usage: logger show channels\n"
			"       List configured logger channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;	
	}
	ast_cli(a->fd, FORMATL, "Channel", "Type", "Status");
	ast_cli(a->fd, "Configuration\n");
	ast_cli(a->fd, FORMATL, "-------", "----", "------");
	ast_cli(a->fd, "-------------\n");
	AST_RWLIST_RDLOCK(&logchannels);
	AST_RWLIST_TRAVERSE(&logchannels, chan, list) {
		ast_cli(a->fd, FORMATL, chan->filename, chan->type == LOGTYPE_CONSOLE ? "Console" : (chan->type == LOGTYPE_SYSLOG ? "Syslog" : "File"),
			chan->disabled ? "Disabled" : "Enabled");
		ast_cli(a->fd, " - ");
		if (chan->logmask & (1 << __LOG_DEBUG)) 
			ast_cli(a->fd, "Debug ");
		if (chan->logmask & (1 << __LOG_DTMF)) 
			ast_cli(a->fd, "DTMF ");
		if (chan->logmask & (1 << __LOG_VERBOSE)) 
			ast_cli(a->fd, "Verbose ");
		if (chan->logmask & (1 << __LOG_WARNING)) 
			ast_cli(a->fd, "Warning ");
		if (chan->logmask & (1 << __LOG_NOTICE)) 
			ast_cli(a->fd, "Notice ");
		if (chan->logmask & (1 << __LOG_ERROR)) 
			ast_cli(a->fd, "Error ");
		if (chan->logmask & (1 << __LOG_EVENT)) 
			ast_cli(a->fd, "Event ");
		ast_cli(a->fd, "\n");
	}
	AST_RWLIST_UNLOCK(&logchannels);
	ast_cli(a->fd, "\n");
 		
	return CLI_SUCCESS;
}

struct verb {
	void (*verboser)(const char *string);
	AST_LIST_ENTRY(verb) list;
};

static AST_RWLIST_HEAD_STATIC(verbosers, verb);

static struct ast_cli_entry cli_logger[] = {
	AST_CLI_DEFINE(handle_logger_show_channels, "List configured log channels"),
	AST_CLI_DEFINE(handle_logger_reload, "Reopens the log files"),
	AST_CLI_DEFINE(handle_logger_rotate, "Rotates and reopens the log files")
};

static int handle_SIGXFSZ(int sig) 
{
	/* Indicate need to reload */
	filesize_reload_needed = 1;
	return 0;
}

static void ast_log_vsyslog(int level, const char *file, int line, const char *function, char *str)
{
	char buf[BUFSIZ];

	if (level >= SYSLOG_NLEVELS) {
		/* we are locked here, so cannot ast_log() */
		fprintf(stderr, "ast_log_vsyslog called with bogus level: %d\n", level);
		return;
	}

	if (level == __LOG_VERBOSE) {
		snprintf(buf, sizeof(buf), "VERBOSE[%ld]: %s", (long)GETTID(), str);
		level = __LOG_DEBUG;
	} else if (level == __LOG_DTMF) {
		snprintf(buf, sizeof(buf), "DTMF[%ld]: %s", (long)GETTID(), str);
		level = __LOG_DEBUG;
	} else {
		snprintf(buf, sizeof(buf), "%s[%ld]: %s:%d in %s: %s",
			 levels[level], (long)GETTID(), file, line, function, str);
	}

	term_strip(buf, buf, strlen(buf) + 1);
	syslog(syslog_level_map[level], "%s", buf);
}

/*! \brief Print a normal log message to the channels */
static void logger_print_normal(struct logmsg *logmsg)
{
	struct logchannel *chan = NULL;
	char buf[BUFSIZ];

	AST_RWLIST_RDLOCK(&logchannels);

	if (logfiles.event_log && logmsg->level == __LOG_EVENT) {
		fprintf(eventlog, "%s asterisk[%ld]: %s", logmsg->date, (long)getpid(), logmsg->str);
		fflush(eventlog);
		AST_RWLIST_UNLOCK(&logchannels);
		return;
	}

	if (!AST_RWLIST_EMPTY(&logchannels)) {
		AST_RWLIST_TRAVERSE(&logchannels, chan, list) {
			/* If the channel is disabled, then move on to the next one */
			if (chan->disabled)
				continue;
			/* Check syslog channels */
			if (chan->type == LOGTYPE_SYSLOG && (chan->logmask & (1 << logmsg->level))) {
				ast_log_vsyslog(logmsg->level, logmsg->file, logmsg->line, logmsg->function, logmsg->str);
			/* Console channels */
			} else if (chan->type == LOGTYPE_CONSOLE && (chan->logmask & (1 << logmsg->level))) {
				char linestr[128];
				char tmp1[80], tmp2[80], tmp3[80], tmp4[80];

				/* If the level is verbose, then skip it */
				if (logmsg->level == __LOG_VERBOSE)
					continue;

				/* Turn the numerical line number into a string */
				snprintf(linestr, sizeof(linestr), "%d", logmsg->line);
				/* Build string to print out */
				snprintf(buf, sizeof(buf), "[%s] %s[%ld]: %s:%s %s: %s",
					 logmsg->date,
					 term_color(tmp1, levels[logmsg->level], colors[logmsg->level], 0, sizeof(tmp1)),
					 (long)GETTID(),
					 term_color(tmp2, logmsg->file, COLOR_BRWHITE, 0, sizeof(tmp2)),
					 term_color(tmp3, linestr, COLOR_BRWHITE, 0, sizeof(tmp3)),
					 term_color(tmp4, logmsg->function, COLOR_BRWHITE, 0, sizeof(tmp4)),
					 logmsg->str);
				/* Print out */
				ast_console_puts_mutable(buf);
			/* File channels */
			} else if (chan->type == LOGTYPE_FILE && (chan->logmask & (1 << logmsg->level))) {
				int res = 0;

				/* If no file pointer exists, skip it */
				if (!chan->fileptr)
					continue;
				
				/* Print out to the file */
				res = fprintf(chan->fileptr, "[%s] %s[%ld] %s: %s",
					      logmsg->date, levels[logmsg->level], (long)GETTID(), logmsg->file, logmsg->str);
				if (res <= 0 && !ast_strlen_zero(logmsg->str)) {
					fprintf(stderr, "**** Asterisk Logging Error: ***********\n");
					if (errno == ENOMEM || errno == ENOSPC)
						fprintf(stderr, "Asterisk logging error: Out of disk space, can't log to log file %s\n", chan->filename);
					else
						fprintf(stderr, "Logger Warning: Unable to write to log file '%s': %s (disabled)\n", chan->filename, strerror(errno));
					manager_event(EVENT_FLAG_SYSTEM, "LogChannel", "Channel: %s\r\nEnabled: No\r\nReason: %d - %s\r\n", chan->filename, errno, strerror(errno));
					chan->disabled = 1;
				} else if (res > 0) {
					fflush(chan->fileptr);
				}
			}
		}
	} else if (logmsg->level != __LOG_VERBOSE) {
		fputs(logmsg->str, stdout);
	}

	AST_RWLIST_UNLOCK(&logchannels);

	/* If we need to reload because of the file size, then do so */
	if (filesize_reload_needed) {
		reload_logger(-1);
		ast_log(LOG_EVENT, "Rotated Logs Per SIGXFSZ (Exceeded file size limit)\n");
		ast_verb(1, "Rotated Logs Per SIGXFSZ (Exceeded file size limit)\n");
	}

	return;
}

/*! \brief Print a verbose message to the verbosers */
static void logger_print_verbose(struct logmsg *logmsg)
{
	struct verb *v = NULL;

	/* Iterate through the list of verbosers and pass them the log message string */
	AST_RWLIST_RDLOCK(&verbosers);
	AST_RWLIST_TRAVERSE(&verbosers, v, list)
		v->verboser(logmsg->str);
	AST_RWLIST_UNLOCK(&verbosers);

	return;
}

/*! \brief Actual logging thread */
static void *logger_thread(void *data)
{
	struct logmsg *next = NULL, *msg = NULL;

	for (;;) {
		/* We lock the message list, and see if any message exists... if not we wait on the condition to be signalled */
		AST_LIST_LOCK(&logmsgs);
		if (AST_LIST_EMPTY(&logmsgs))
			ast_cond_wait(&logcond, &logmsgs.lock);
		next = AST_LIST_FIRST(&logmsgs);
		AST_LIST_HEAD_INIT_NOLOCK(&logmsgs);
		AST_LIST_UNLOCK(&logmsgs);

		/* Otherwise go through and process each message in the order added */
		while ((msg = next)) {
			/* Get the next entry now so that we can free our current structure later */
			next = AST_LIST_NEXT(msg, list);

			/* Depending on the type, send it to the proper function */
			if (msg->type == LOGMSG_NORMAL)
				logger_print_normal(msg);
			else if (msg->type == LOGMSG_VERBOSE)
				logger_print_verbose(msg);

			/* Free the data since we are done */
			ast_free(msg);
		}

		/* If we should stop, then stop */
		if (close_logger_thread)
			break;
	}

	return NULL;
}

int init_logger(void)
{
	char tmp[256];
	int res = 0;

	/* auto rotate if sig SIGXFSZ comes a-knockin */
	(void) signal(SIGXFSZ, (void *) handle_SIGXFSZ);

	/* start logger thread */
	ast_cond_init(&logcond, NULL);
	if (ast_pthread_create(&logthread, NULL, logger_thread, NULL) < 0) {
		ast_cond_destroy(&logcond);
		return -1;
	}

	/* register the logger cli commands */
	ast_cli_register_multiple(cli_logger, sizeof(cli_logger) / sizeof(struct ast_cli_entry));

	ast_mkdir(ast_config_AST_LOG_DIR, 0777);
  
	/* create log channels */
	init_logger_chain(0 /* locked */);

	/* create the eventlog */
	if (logfiles.event_log) {
		snprintf(tmp, sizeof(tmp), "%s/%s", ast_config_AST_LOG_DIR, EVENTLOG);
		eventlog = fopen(tmp, "a");
		if (eventlog) {
			ast_log(LOG_EVENT, "Started Asterisk Event Logger\n");
			ast_verb(1, "Asterisk Event Logger Started %s\n", tmp);
		} else {
			ast_log(LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
			res = -1;
		}
	}

	if (logfiles.queue_log) {
		snprintf(tmp, sizeof(tmp), "%s/%s", ast_config_AST_LOG_DIR, queue_log_name);
		qlog = fopen(tmp, "a");
		ast_queue_log("NONE", "NONE", "NONE", "QUEUESTART", "%s", "");
	}
	return res;
}

void close_logger(void)
{
	struct logchannel *f = NULL;

	/* Stop logger thread */
	AST_LIST_LOCK(&logmsgs);
	close_logger_thread = 1;
	ast_cond_signal(&logcond);
	AST_LIST_UNLOCK(&logmsgs);

	if (logthread != AST_PTHREADT_NULL)
		pthread_join(logthread, NULL);

	AST_RWLIST_WRLOCK(&logchannels);

	if (eventlog) {
		fclose(eventlog);
		eventlog = NULL;
	}

	if (qlog) {
		fclose(qlog);
		qlog = NULL;
	}

	AST_RWLIST_TRAVERSE(&logchannels, f, list) {
		if (f->fileptr && (f->fileptr != stdout) && (f->fileptr != stderr)) {
			fclose(f->fileptr);
			f->fileptr = NULL;
		}
	}

	closelog(); /* syslog */

	AST_RWLIST_UNLOCK(&logchannels);

	return;
}

/*!
 * \brief send log messages to syslog and/or the console
 */
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	struct logmsg *logmsg = NULL;
	struct ast_str *buf = NULL;
	struct ast_tm tm;
	struct timeval tv = ast_tvnow();
	int res = 0;
	va_list ap;

	if (!(buf = ast_str_thread_get(&log_buf, LOG_BUF_INIT_SIZE)))
		return;

	if (AST_RWLIST_EMPTY(&logchannels)) {
		/*
		 * we don't have the logger chain configured yet,
		 * so just log to stdout
		 */
		if (level != __LOG_VERBOSE) {
			int res;
			va_start(ap, fmt);
			res = ast_str_set_va(&buf, BUFSIZ, fmt, ap); /* XXX BUFSIZ ? */
			va_end(ap);
			if (res != AST_DYNSTR_BUILD_FAILED) {
				term_filter_escapes(buf->str);
				fputs(buf->str, stdout);
			}
		}
		return;
	}
	
	/* don't display LOG_DEBUG messages unless option_verbose _or_ option_debug
	   are non-zero; LOG_DEBUG messages can still be displayed if option_debug
	   is zero, if option_verbose is non-zero (this allows for 'level zero'
	   LOG_DEBUG messages to be displayed, if the logmask on any channel
	   allows it)
	*/
	if (!option_verbose && !option_debug && (level == __LOG_DEBUG))
		return;

	/* Ignore anything that never gets logged anywhere */
	if (!(global_logmask & (1 << level)))
		return;
	
	/* Build string */
	va_start(ap, fmt);
	res = ast_str_set_va(&buf, BUFSIZ, fmt, ap);
	va_end(ap);

	/* If the build failed, then abort and free this structure */
	if (res == AST_DYNSTR_BUILD_FAILED)
		return;

	/* Create a new logging message */
	if (!(logmsg = ast_calloc(1, sizeof(*logmsg) + res + 1)))
		return;
	
	/* Copy string over */
	strcpy(logmsg->str, buf->str);

	/* Set type to be normal */
	logmsg->type = LOGMSG_NORMAL;

	/* Create our date/time */
	ast_localtime(&tv, &tm, NULL);
	ast_strftime(logmsg->date, sizeof(logmsg->date), dateformat, &tm);

	/* Copy over data */
	logmsg->level = level;
	logmsg->file = file;
	logmsg->line = line;
	logmsg->function = function;

	/* If the logger thread is active, append it to the tail end of the list - otherwise skip that step */
	if (logthread != AST_PTHREADT_NULL) {
		AST_LIST_LOCK(&logmsgs);
		AST_LIST_INSERT_TAIL(&logmsgs, logmsg, list);
		ast_cond_signal(&logcond);
		AST_LIST_UNLOCK(&logmsgs);
	} else {
		logger_print_normal(logmsg);
		ast_free(logmsg);
	}

	return;
}

void ast_backtrace(void)
{
#ifdef HAVE_BKTR
	int count = 0, i = 0;
	void **addresses;
	char **strings;

	if ((addresses = ast_calloc(MAX_BACKTRACE_FRAMES, sizeof(*addresses)))) {
		count = backtrace(addresses, MAX_BACKTRACE_FRAMES);
		if ((strings = backtrace_symbols(addresses, count))) {
			ast_debug(1, "Got %d backtrace record%c\n", count, count != 1 ? 's' : ' ');
			for (i = 0; i < count; i++) {
#if __WORDSIZE == 32
				ast_log(LOG_DEBUG, "#%d: [%08X] %s\n", i, (unsigned int)addresses[i], strings[i]);
#elif __WORDSIZE == 64
				ast_log(LOG_DEBUG, "#%d: [%016lX] %s\n", i, (unsigned long)addresses[i], strings[i]);
#endif
			}
			free(strings);
		} else {
			ast_debug(1, "Could not allocate memory for backtrace\n");
		}
		ast_free(addresses);
	}
#else
	ast_log(LOG_WARNING, "Must run configure with '--with-execinfo' for stack backtraces.\n");
#endif
}

void ast_verbose(const char *fmt, ...)
{
	struct logmsg *logmsg = NULL;
	struct ast_str *buf = NULL;
	int res = 0;
	va_list ap;

	if (!(buf = ast_str_thread_get(&verbose_buf, VERBOSE_BUF_INIT_SIZE)))
		return;

	if (ast_opt_timestamp) {
		struct timeval tv;
		struct ast_tm tm;
		char date[40];
		char *datefmt;

		tv = ast_tvnow();
		ast_localtime(&tv, &tm, NULL);
		ast_strftime(date, sizeof(date), dateformat, &tm);
		datefmt = alloca(strlen(date) + 3 + strlen(fmt) + 1);
		sprintf(datefmt, "%c[%s] %s", 127, date, fmt);
		fmt = datefmt;
	} else {
		char *tmp = alloca(strlen(fmt) + 2);
		sprintf(tmp, "%c%s", 127, fmt);
		fmt = tmp;
	}

	/* Build string */
	va_start(ap, fmt);
	res = ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	/* If the build failed then we can drop this allocated message */
	if (res == AST_DYNSTR_BUILD_FAILED)
		return;

	if (!(logmsg = ast_calloc(1, sizeof(*logmsg) + res + 1)))
		return;

	strcpy(logmsg->str, buf->str);

	ast_log(LOG_VERBOSE, "%s", logmsg->str + 1);

	/* Set type */
	logmsg->type = LOGMSG_VERBOSE;
	
	/* Add to the list and poke the thread if possible */
	if (logthread != AST_PTHREADT_NULL) {
		AST_LIST_LOCK(&logmsgs);
		AST_LIST_INSERT_TAIL(&logmsgs, logmsg, list);
		ast_cond_signal(&logcond);
		AST_LIST_UNLOCK(&logmsgs);
	} else {
		logger_print_verbose(logmsg);
		ast_free(logmsg);
	}
}

int ast_register_verbose(void (*v)(const char *string)) 
{
	struct verb *verb;

	if (!(verb = ast_malloc(sizeof(*verb))))
		return -1;

	verb->verboser = v;

	AST_RWLIST_WRLOCK(&verbosers);
	AST_RWLIST_INSERT_HEAD(&verbosers, verb, list);
	AST_RWLIST_UNLOCK(&verbosers);
	
	return 0;
}

int ast_unregister_verbose(void (*v)(const char *string))
{
	struct verb *cur;

	AST_RWLIST_WRLOCK(&verbosers);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&verbosers, cur, list) {
		if (cur->verboser == v) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(cur);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&verbosers);
	
	return cur ? 0 : -1;
}
