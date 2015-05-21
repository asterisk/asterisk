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

/*! \li \ref logger.c uses the configuration file \ref logger.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page logger.conf logger.conf
 * \verbinclude logger.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

/* When we include logger.h again it will trample on some stuff in syslog.h, but
 * nothing we care about in here. */
#include <syslog.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/astobj2.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/syslog.h"
#include "asterisk/buildinfo.h"
#include "asterisk/ast_version.h"
#include "asterisk/backtrace.h"

/*** DOCUMENTATION
 ***/

static char dateformat[256] = "%b %e %T";		/* Original Asterisk Format */

static char queue_log_name[256] = QUEUELOG;
static char exec_after_rotate[256] = "";

static int filesize_reload_needed;
static unsigned int global_logmask = 0xFFFF;
static int queuelog_init;
static int logger_initialized;
static volatile int next_unique_callid; /* Used to assign unique call_ids to calls */
static int display_callids;
static void unique_callid_cleanup(void *data);

struct ast_callid {
	int call_identifier; /* Numerical value of the call displayed in the logs */
};

AST_THREADSTORAGE_CUSTOM(unique_callid, NULL, unique_callid_cleanup);

static enum rotatestrategy {
	NONE = 0,                /* Do not rotate log files at all, instead rely on external mechanisms */
	SEQUENTIAL = 1 << 0,     /* Original method - create a new file, in order */
	ROTATE = 1 << 1,         /* Rotate all files, such that the oldest file has the highest suffix */
	TIMESTAMP = 1 << 2,      /* Append the epoch timestamp onto the end of the archived file */
} rotatestrategy = SEQUENTIAL;

static struct {
	unsigned int queue_log:1;
	unsigned int queue_log_to_file:1;
	unsigned int queue_adaptive_realtime:1;
	unsigned int queue_log_realtime_use_gmt:1;
} logfiles = { 1 };

static char hostname[MAXHOSTNAMELEN];
AST_THREADSTORAGE_RAW(in_safe_log);

enum logtypes {
	LOGTYPE_SYSLOG,
	LOGTYPE_FILE,
	LOGTYPE_CONSOLE,
};

struct logchannel {
	/*! What to log to this channel */
	unsigned int logmask;
	/*! If this channel is disabled or not */
	int disabled;
	/*! syslog facility */
	int facility;
	/*! Verbosity level. (-1 if use option_verbose for the level.) */
	int verbosity;
	/*! Type of log channel */
	enum logtypes type;
	/*! logfile logging file pointer */
	FILE *fileptr;
	/*! Filename */
	char filename[PATH_MAX];
	/*! field for linking to list */
	AST_LIST_ENTRY(logchannel) list;
	/*! Line number from configuration file */
	int lineno;
	/*! Whether this log channel was created dynamically */
	int dynamic;
	/*! Components (levels) from last config load */
	char components[0];
};

static AST_RWLIST_HEAD_STATIC(logchannels, logchannel);

enum logmsgtypes {
	LOGMSG_NORMAL = 0,
	LOGMSG_VERBOSE,
};

struct logmsg {
	enum logmsgtypes type;
	int level;
	int line;
	int lwp;
	struct ast_callid *callid;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(date);
		AST_STRING_FIELD(file);
		AST_STRING_FIELD(function);
		AST_STRING_FIELD(message);
		AST_STRING_FIELD(level_name);
	);
	AST_LIST_ENTRY(logmsg) list;
};

static void logmsg_free(struct logmsg *msg)
{
	if (msg->callid) {
		ast_callid_unref(msg->callid);
	}
	ast_free(msg);
}

static AST_LIST_HEAD_STATIC(logmsgs, logmsg);
static pthread_t logthread = AST_PTHREADT_NULL;
static ast_cond_t logcond;
static int close_logger_thread = 0;

static FILE *qlog;

/*! \brief Logging channels used in the Asterisk logging system
 *
 * The first 16 levels are reserved for system usage, and the remaining
 * levels are reserved for usage by dynamic levels registered via
 * ast_logger_register_level.
 */

/* Modifications to this array are protected by the rwlock in the
 * logchannels list.
 */

static char *levels[NUMLOGLEVELS] = {
	"DEBUG",
	"---EVENT---",		/* no longer used */
	"NOTICE",
	"WARNING",
	"ERROR",
	"VERBOSE",
	"DTMF",
};

/*! \brief Colors used in the console for logging */
static const int colors[NUMLOGLEVELS] = {
	COLOR_BRGREEN,
	COLOR_BRBLUE,		/* no longer used */
	COLOR_YELLOW,
	COLOR_BRRED,
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BRGREEN,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
	COLOR_BRBLUE,
};

AST_THREADSTORAGE(verbose_buf);
AST_THREADSTORAGE(verbose_build_buf);
#define VERBOSE_BUF_INIT_SIZE   256

AST_THREADSTORAGE(log_buf);
#define LOG_BUF_INIT_SIZE       256

static void make_components(struct logchannel *chan)
{
	char *w;
	unsigned int logmask = 0;
	char *stringp = ast_strdupa(chan->components);
	unsigned int x;
	unsigned int verb_level;

	/* Default to using option_verbose as the verbosity level of the logging channel.  */
	verb_level = -1;

	while ((w = strsep(&stringp, ","))) {
		w = ast_strip(w);
		if (ast_strlen_zero(w)) {
			continue;
		}
		if (!strcmp(w, "*")) {
			logmask = 0xFFFFFFFF;
		} else if (!strncasecmp(w, "verbose(", 8)) {
			if (levels[__LOG_VERBOSE] && sscanf(w + 8, "%30u)", &verb_level) == 1) {
				logmask |= (1 << __LOG_VERBOSE);
			}
		} else {
			for (x = 0; x < ARRAY_LEN(levels); ++x) {
				if (levels[x] && !strcasecmp(w, levels[x])) {
					logmask |= (1 << x);
					break;
				}
			}
		}
	}
	if (chan->type == LOGTYPE_CONSOLE) {
		/*
		 * Force to use the root console verbose level so if the
		 * user specified any verbose level then it does not interfere
		 * with calculating the ast_verb_sys_level value.
		 */
		chan->verbosity = -1;
	} else {
		chan->verbosity = verb_level;
	}
	chan->logmask = logmask;
}

static struct logchannel *make_logchannel(const char *channel, const char *components, int lineno, int dynamic)
{
	struct logchannel *chan;
	char *facility;
	struct ast_tm tm;
	struct timeval now = ast_tvnow();
	char datestring[256];

	if (ast_strlen_zero(channel) || !(chan = ast_calloc(1, sizeof(*chan) + strlen(components) + 1)))
		return NULL;

	strcpy(chan->components, components);
	chan->lineno = lineno;
	chan->dynamic = dynamic;

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

		chan->facility = ast_syslog_facility(facility);

		if (chan->facility < 0) {
			fprintf(stderr, "Logger Warning: bad syslog facility in logger.conf\n");
			ast_free(chan);
			return NULL;
		}

		chan->type = LOGTYPE_SYSLOG;
		ast_copy_string(chan->filename, channel, sizeof(chan->filename));
		openlog("asterisk", LOG_PID, chan->facility);
	} else {
		const char *log_dir_prefix = "";
		const char *log_dir_separator = "";

		if (channel[0] != '/') {
			log_dir_prefix = ast_config_AST_LOG_DIR;
			log_dir_separator = "/";
		}

		if (!ast_strlen_zero(hostname)) {
			snprintf(chan->filename, sizeof(chan->filename), "%s%s%s.%s",
				log_dir_prefix, log_dir_separator, channel, hostname);
		} else {
			snprintf(chan->filename, sizeof(chan->filename), "%s%s%s",
				log_dir_prefix, log_dir_separator, channel);
		}

		if (!(chan->fileptr = fopen(chan->filename, "a"))) {
			/* Can't do real logging here since we're called with a lock
			 * so log to any attached consoles */
			ast_console_puts_mutable("ERROR: Unable to open log file '", __LOG_ERROR);
			ast_console_puts_mutable(chan->filename, __LOG_ERROR);
			ast_console_puts_mutable("': ", __LOG_ERROR);
			ast_console_puts_mutable(strerror(errno), __LOG_ERROR);
			ast_console_puts_mutable("'\n", __LOG_ERROR);
			ast_free(chan);
			return NULL;
		} else {
			/* Create our date/time */
			ast_localtime(&now, &tm, NULL);
			ast_strftime(datestring, sizeof(datestring), dateformat, &tm);

			fprintf(chan->fileptr, "[%s] Asterisk %s built by %s @ %s on a %s running %s on %s\n",
				datestring, ast_get_version(), ast_build_user, ast_build_hostname,
				ast_build_machine, ast_build_os, ast_build_date);
			fflush(chan->fileptr);
		}
		chan->type = LOGTYPE_FILE;
	}
	make_components(chan);

	return chan;
}

/* \brief Read config, setup channels.
 * \param locked The logchannels list is locked and this is a reload
 * \param altconf Alternate configuration file to read.
 *
 * \retval 0 Success
 * \retval -1 No config found or Failed
 */
static int init_logger_chain(int locked, const char *altconf)
{
	struct logchannel *chan;
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *s;
	struct ast_flags config_flags = { 0 };

	if (!(cfg = ast_config_load2(S_OR(altconf, "logger.conf"), "logger", config_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		cfg = NULL;
	}

	if (!locked) {
		AST_RWLIST_WRLOCK(&logchannels);
	}

	/* Set defaults */
	hostname[0] = '\0';
	display_callids = 1;
	memset(&logfiles, 0, sizeof(logfiles));
	logfiles.queue_log = 1;
	ast_copy_string(dateformat, "%b %e %T", sizeof(dateformat));
	ast_copy_string(queue_log_name, QUEUELOG, sizeof(queue_log_name));
	exec_after_rotate[0] = '\0';
	rotatestrategy = SEQUENTIAL;

	/* delete our list of log channels */
	while ((chan = AST_RWLIST_REMOVE_HEAD(&logchannels, list))) {
		ast_free(chan);
	}
	global_logmask = 0;
	if (!locked) {
		AST_RWLIST_UNLOCK(&logchannels);
	}

	errno = 0;
	/* close syslog */
	closelog();

	/* If no config file, we're fine, set default options. */
	if (!cfg) {
		if (!(chan = ast_calloc(1, sizeof(*chan)))) {
			fprintf(stderr, "Failed to initialize default logging\n");
			return -1;
		}
		chan->type = LOGTYPE_CONSOLE;
		chan->logmask = __LOG_WARNING | __LOG_NOTICE | __LOG_ERROR;

		if (!locked) {
			AST_RWLIST_WRLOCK(&logchannels);
		}
		AST_RWLIST_INSERT_HEAD(&logchannels, chan, list);
		global_logmask |= chan->logmask;
		if (!locked) {
			AST_RWLIST_UNLOCK(&logchannels);
		}

		return -1;
	}

	if ((s = ast_variable_retrieve(cfg, "general", "appendhostname"))) {
		if (ast_true(s)) {
			if (gethostname(hostname, sizeof(hostname) - 1)) {
				ast_copy_string(hostname, "unknown", sizeof(hostname));
				fprintf(stderr, "What box has no hostname???\n");
			}
		}
	}
	if ((s = ast_variable_retrieve(cfg, "general", "display_callids"))) {
		display_callids = ast_true(s);
	}
	if ((s = ast_variable_retrieve(cfg, "general", "dateformat"))) {
		ast_copy_string(dateformat, s, sizeof(dateformat));
	}
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log"))) {
		logfiles.queue_log = ast_true(s);
	}
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log_to_file"))) {
		logfiles.queue_log_to_file = ast_true(s);
	}
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log_name"))) {
		ast_copy_string(queue_log_name, s, sizeof(queue_log_name));
	}
	if ((s = ast_variable_retrieve(cfg, "general", "queue_log_realtime_use_gmt"))) {
		logfiles.queue_log_realtime_use_gmt = ast_true(s);
	}
	if ((s = ast_variable_retrieve(cfg, "general", "exec_after_rotate"))) {
		ast_copy_string(exec_after_rotate, s, sizeof(exec_after_rotate));
	}
	if ((s = ast_variable_retrieve(cfg, "general", "rotatestrategy"))) {
		if (strcasecmp(s, "timestamp") == 0) {
			rotatestrategy = TIMESTAMP;
		} else if (strcasecmp(s, "rotate") == 0) {
			rotatestrategy = ROTATE;
		} else if (strcasecmp(s, "sequential") == 0) {
			rotatestrategy = SEQUENTIAL;
		} else if (strcasecmp(s, "none") == 0) {
			rotatestrategy = NONE;
		} else {
			fprintf(stderr, "Unknown rotatestrategy: %s\n", s);
		}
	} else {
		if ((s = ast_variable_retrieve(cfg, "general", "rotatetimestamp"))) {
			rotatestrategy = ast_true(s) ? TIMESTAMP : SEQUENTIAL;
			fprintf(stderr, "rotatetimestamp option has been deprecated.  Please use rotatestrategy instead.\n");
		}
	}

	if (!locked) {
		AST_RWLIST_WRLOCK(&logchannels);
	}
	var = ast_variable_browse(cfg, "logfiles");
	for (; var; var = var->next) {
		if (!(chan = make_logchannel(var->name, var->value, var->lineno, 0))) {
			/* Print error message directly to the consoles since the lock is held
			 * and we don't want to unlock with the list partially built */
			ast_console_puts_mutable("ERROR: Unable to create log channel '", __LOG_ERROR);
			ast_console_puts_mutable(var->name, __LOG_ERROR);
			ast_console_puts_mutable("'\n", __LOG_ERROR);
			continue;
		}
		AST_RWLIST_INSERT_HEAD(&logchannels, chan, list);
		global_logmask |= chan->logmask;
	}

	if (qlog) {
		fclose(qlog);
		qlog = NULL;
	}

	if (!locked) {
		AST_RWLIST_UNLOCK(&logchannels);
	}

	ast_config_destroy(cfg);

	return 0;
}

void ast_child_verbose(int level, const char *fmt, ...)
{
	char *msg = NULL, *emsg = NULL, *sptr, *eptr;
	va_list ap, aq;
	int size;

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
	struct timeval tv;
	struct ast_tm tm;
	char qlog_msg[8192];
	int qlog_len;
	char time_str[30];

	if (!logger_initialized) {
		/* You are too early.  We are not open yet! */
		return;
	}
	if (!queuelog_init) {
		/* We must initialize now since someone is trying to log something. */
		logger_queue_start();
	}

	if (ast_check_realtime("queue_log")) {
		tv = ast_tvnow();
		ast_localtime(&tv, &tm, logfiles.queue_log_realtime_use_gmt ? "GMT" : NULL);
		ast_strftime(time_str, sizeof(time_str), "%F %T.%6q", &tm);
		va_start(ap, fmt);
		vsnprintf(qlog_msg, sizeof(qlog_msg), fmt, ap);
		va_end(ap);
		if (logfiles.queue_adaptive_realtime) {
			AST_DECLARE_APP_ARGS(args,
				AST_APP_ARG(data)[5];
			);
			AST_NONSTANDARD_APP_ARGS(args, qlog_msg, '|');
			/* Ensure fields are large enough to receive data */
			ast_realtime_require_field("queue_log",
				"data1", RQ_CHAR, strlen(S_OR(args.data[0], "")),
				"data2", RQ_CHAR, strlen(S_OR(args.data[1], "")),
				"data3", RQ_CHAR, strlen(S_OR(args.data[2], "")),
				"data4", RQ_CHAR, strlen(S_OR(args.data[3], "")),
				"data5", RQ_CHAR, strlen(S_OR(args.data[4], "")),
				SENTINEL);

			/* Store the log */
			ast_store_realtime("queue_log", "time", time_str,
				"callid", callid,
				"queuename", queuename,
				"agent", agent,
				"event", event,
				"data1", S_OR(args.data[0], ""),
				"data2", S_OR(args.data[1], ""),
				"data3", S_OR(args.data[2], ""),
				"data4", S_OR(args.data[3], ""),
				"data5", S_OR(args.data[4], ""),
				SENTINEL);
		} else {
			ast_store_realtime("queue_log", "time", time_str,
				"callid", callid,
				"queuename", queuename,
				"agent", agent,
				"event", event,
				"data", qlog_msg,
				SENTINEL);
		}

		if (!logfiles.queue_log_to_file) {
			return;
		}
	}

	if (qlog) {
		va_start(ap, fmt);
		qlog_len = snprintf(qlog_msg, sizeof(qlog_msg), "%ld|%s|%s|%s|%s|", (long)time(NULL), callid, queuename, agent, event);
		vsnprintf(qlog_msg + qlog_len, sizeof(qlog_msg) - qlog_len, fmt, ap);
		va_end(ap);
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
	case NONE:
		/* No rotation */
		break;
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
		} else {
			filename = new;
		}
		break;
	case TIMESTAMP:
		snprintf(new, sizeof(new), "%s.%ld", filename, (long)time(NULL));
		if (rename(filename, new)) {
			fprintf(stderr, "Unable to rename file '%s' to '%s'\n", filename, new);
			res = -1;
		} else {
			filename = new;
		}
		break;
	case ROTATE:
		/* Find the next empty slot, including a possible suffix */
		for (x = 0; ; x++) {
			found = 0;
			for (which = 0; which < ARRAY_LEN(suffixes); which++) {
				snprintf(new, sizeof(new), "%s.%d%s", filename, x, suffixes[which]);
				fd = open(new, O_RDONLY);
				if (fd > -1) {
					close(fd);
					found = 1;
					break;
				}
			}
			if (!found) {
				break;
			}
		}

		/* Found an empty slot */
		for (y = x; y > 0; y--) {
			for (which = 0; which < ARRAY_LEN(suffixes); which++) {
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
		} else {
			filename = new;
		}
	}

	if (!ast_strlen_zero(exec_after_rotate)) {
		struct ast_channel *c = ast_dummy_channel_alloc();
		char buf[512];

		pbx_builtin_setvar_helper(c, "filename", filename);
		pbx_substitute_variables_helper(c, exec_after_rotate, buf, sizeof(buf));
		if (c) {
			c = ast_channel_unref(c);
		}
		if (ast_safe_system(buf) == -1) {
			ast_log(LOG_WARNING, "error executing '%s'\n", buf);
		}
	}
	return res;
}

/*!
 * \internal
 * \brief Start the realtime queue logging if configured.
 *
 * \retval TRUE if not to open queue log file.
 */
static int logger_queue_rt_start(void)
{
	if (ast_check_realtime("queue_log")) {
		if (!ast_realtime_require_field("queue_log",
			"time", RQ_DATETIME, 26,
			"data1", RQ_CHAR, 20,
			"data2", RQ_CHAR, 20,
			"data3", RQ_CHAR, 20,
			"data4", RQ_CHAR, 20,
			"data5", RQ_CHAR, 20,
			SENTINEL)) {
			logfiles.queue_adaptive_realtime = 1;
		} else {
			logfiles.queue_adaptive_realtime = 0;
		}

		if (!logfiles.queue_log_to_file) {
			/* Don't open the log file. */
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Rotate the queue log file and restart.
 *
 * \param queue_rotate Log queue rotation mode.
 *
 * \note Assumes logchannels is write locked on entry.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int logger_queue_restart(int queue_rotate)
{
	int res = 0;
	char qfname[PATH_MAX];

	if (logger_queue_rt_start()) {
		return res;
	}

	snprintf(qfname, sizeof(qfname), "%s/%s", ast_config_AST_LOG_DIR, queue_log_name);
	if (qlog) {
		/* Just in case it was still open. */
		fclose(qlog);
		qlog = NULL;
	}
	if (queue_rotate) {
		rotate_file(qfname);
	}

	/* Open the log file. */
	qlog = fopen(qfname, "a");
	if (!qlog) {
		ast_log(LOG_ERROR, "Unable to create queue log: %s\n", strerror(errno));
		res = -1;
	}
	return res;
}

static int reload_logger(int rotate, const char *altconf)
{
	int queue_rotate = rotate;
	struct logchannel *f;
	int res = 0;

	AST_RWLIST_WRLOCK(&logchannels);

	if (qlog) {
		if (rotate < 0) {
			/* Check filesize - this one typically doesn't need an auto-rotate */
			if (ftello(qlog) > 0x40000000) { /* Arbitrarily, 1 GB */
				fclose(qlog);
				qlog = NULL;
			} else {
				queue_rotate = 0;
			}
		} else {
			fclose(qlog);
			qlog = NULL;
		}
	} else {
		queue_rotate = 0;
	}

	ast_mkdir(ast_config_AST_LOG_DIR, 0777);

	AST_RWLIST_TRAVERSE(&logchannels, f, list) {
		if (f->disabled) {
			f->disabled = 0;	/* Re-enable logging at reload */
			/*** DOCUMENTATION
				<managerEventInstance>
					<synopsis>Raised when a logging channel is re-enabled after a reload operation.</synopsis>
					<syntax>
						<parameter name="Channel">
							<para>The name of the logging channel.</para>
						</parameter>
					</syntax>
				</managerEventInstance>
			***/
			manager_event(EVENT_FLAG_SYSTEM, "LogChannel", "Channel: %s\r\nEnabled: Yes\r\n", f->filename);
		}
		if (f->fileptr && (f->fileptr != stdout) && (f->fileptr != stderr)) {
			int rotate_this = 0;
			if (rotatestrategy != NONE && ftello(f->fileptr) > 0x40000000) { /* Arbitrarily, 1 GB */
				/* Be more proactive about rotating massive log files */
				rotate_this = 1;
			}
			fclose(f->fileptr);	/* Close file */
			f->fileptr = NULL;
			if (rotate || rotate_this) {
				rotate_file(f->filename);
			}
		}
	}

	filesize_reload_needed = 0;

	init_logger_chain(1 /* locked */, altconf);

	ast_unload_realtime("queue_log");
	if (logfiles.queue_log) {
		res = logger_queue_restart(queue_rotate);
		AST_RWLIST_UNLOCK(&logchannels);
		ast_verb_update();
		ast_queue_log("NONE", "NONE", "NONE", "CONFIGRELOAD", "%s", "");
		ast_verb(1, "Asterisk Queue Logger restarted\n");
	} else {
		AST_RWLIST_UNLOCK(&logchannels);
		ast_verb_update();
	}

	return res;
}

/*! \brief Reload the logger module without rotating log files (also used from loader.c during
	a full Asterisk reload) */
int logger_reload(void)
{
	if (reload_logger(0, NULL)) {
		return RESULT_FAILURE;
	}
	return RESULT_SUCCESS;
}

static char *handle_logger_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "logger reload";
		e->usage =
			"Usage: logger reload [<alt-conf>]\n"
			"       Reloads the logger subsystem state.  Use after restarting syslogd(8) if you are using syslog logging.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (reload_logger(0, a->argc == 3 ? a->argv[2] : NULL)) {
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
	if (reload_logger(1, NULL)) {
		ast_cli(a->fd, "Failed to reload the logger and rotate log files\n");
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

int ast_logger_rotate()
{
	return reload_logger(1, NULL);
}

static char *handle_logger_set_level(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int x;
	int state;
	int level = -1;

	switch (cmd) {
	case CLI_INIT:
		e->command = "logger set level {DEBUG|NOTICE|WARNING|ERROR|VERBOSE|DTMF} {on|off}";
		e->usage =
			"Usage: logger set level {DEBUG|NOTICE|WARNING|ERROR|VERBOSE|DTMF} {on|off}\n"
			"       Set a specific log level to enabled/disabled for this console.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 5)
		return CLI_SHOWUSAGE;

	AST_RWLIST_WRLOCK(&logchannels);

	for (x = 0; x < ARRAY_LEN(levels); x++) {
		if (levels[x] && !strcasecmp(a->argv[3], levels[x])) {
			level = x;
			break;
		}
	}

	AST_RWLIST_UNLOCK(&logchannels);

	state = ast_true(a->argv[4]) ? 1 : 0;

	if (level != -1) {
		ast_console_toggle_loglevel(a->fd, level, state);
		ast_cli(a->fd, "Logger status for '%s' has been set to '%s'.\n", levels[level], state ? "on" : "off");
	} else
		return CLI_SHOWUSAGE;

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
		unsigned int level;

		ast_cli(a->fd, FORMATL, chan->filename, chan->type == LOGTYPE_CONSOLE ? "Console" : (chan->type == LOGTYPE_SYSLOG ? "Syslog" : "File"),
			chan->disabled ? "Disabled" : "Enabled");
		ast_cli(a->fd, " - ");
		for (level = 0; level < ARRAY_LEN(levels); level++) {
			if ((chan->logmask & (1 << level)) && levels[level]) {
				ast_cli(a->fd, "%s ", levels[level]);
			}
		}
		ast_cli(a->fd, "\n");
	}
	AST_RWLIST_UNLOCK(&logchannels);
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static char *handle_logger_add_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct logchannel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "logger add channel";
		e->usage =
			"Usage: logger add channel <name> <levels>\n"
			"       Adds a temporary logger channel. This logger channel\n"
			"       will exist until removed or until Asterisk is restarted.\n"
			"       <levels> is a comma-separated list of desired logger\n"
			"       levels such as: verbose,warning,error\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 5) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_WRLOCK(&logchannels);
	AST_RWLIST_TRAVERSE(&logchannels, chan, list) {
		if (!strcmp(chan->filename, a->argv[3])) {
			break;
		}
	}

	if (chan) {
		AST_RWLIST_UNLOCK(&logchannels);
		ast_cli(a->fd, "Logger channel '%s' already exists\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	chan = make_logchannel(a->argv[3], a->argv[4], 0, 1);
	if (chan) {
		AST_RWLIST_INSERT_HEAD(&logchannels, chan, list);
		global_logmask |= chan->logmask;
		AST_RWLIST_UNLOCK(&logchannels);
		return CLI_SUCCESS;
	}

	AST_RWLIST_UNLOCK(&logchannels);
	ast_cli(a->fd, "ERROR: Unable to create log channel '%s'\n", a->argv[3]);

	return CLI_FAILURE;
}

static char *handle_logger_remove_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct logchannel *chan;
	int gen_count = 0;
	char *gen_ret = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "logger remove channel";
		e->usage =
			"Usage: logger remove channel <name>\n"
			"       Removes a temporary logger channel.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->argc > 4 || (a->argc == 4 && a->pos > 3)) {
			return NULL;
		}
		AST_RWLIST_RDLOCK(&logchannels);
		AST_RWLIST_TRAVERSE(&logchannels, chan, list) {
			if (chan->dynamic && (ast_strlen_zero(a->argv[3])
				|| !strncmp(a->argv[3], chan->filename, strlen(a->argv[3])))) {
				if (gen_count == a->n) {
					gen_ret = ast_strdup(chan->filename);
					break;
				}
				gen_count++;
			}
		}
		AST_RWLIST_UNLOCK(&logchannels);
		return gen_ret;
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_WRLOCK(&logchannels);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&logchannels, chan, list) {
		if (chan->dynamic && !strcmp(chan->filename, a->argv[3])) {
			AST_RWLIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&logchannels);

	if (!chan) {
		ast_cli(a->fd, "Unable to find dynamic logger channel '%s'\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Removed dynamic logger channel '%s'\n", chan->filename);
	if (chan->fileptr) {
		fclose(chan->fileptr);
		chan->fileptr = NULL;
	}
	ast_free(chan);
	chan = NULL;

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
	AST_CLI_DEFINE(handle_logger_rotate, "Rotates and reopens the log files"),
	AST_CLI_DEFINE(handle_logger_set_level, "Enables/Disables a specific logging level for this console"),
	AST_CLI_DEFINE(handle_logger_add_channel, "Adds a new logging channel"),
	AST_CLI_DEFINE(handle_logger_remove_channel, "Removes a logging channel"),
};

static void _handle_SIGXFSZ(int sig)
{
	/* Indicate need to reload */
	filesize_reload_needed = 1;
}

static struct sigaction handle_SIGXFSZ = {
	.sa_handler = _handle_SIGXFSZ,
	.sa_flags = SA_RESTART,
};

static void ast_log_vsyslog(struct logmsg *msg)
{
	char buf[BUFSIZ];
	int syslog_level = ast_syslog_priority_from_loglevel(msg->level);
	char call_identifier_str[13];

	if (msg->callid) {
		snprintf(call_identifier_str, sizeof(call_identifier_str), "[C-%08x]", (unsigned)msg->callid->call_identifier);
	} else {
		call_identifier_str[0] = '\0';
	}

	if (syslog_level < 0) {
		/* we are locked here, so cannot ast_log() */
		fprintf(stderr, "ast_log_vsyslog called with bogus level: %d\n", msg->level);
		return;
	}

	snprintf(buf, sizeof(buf), "%s[%d]%s: %s:%d in %s: %s",
		 levels[msg->level], msg->lwp, call_identifier_str, msg->file, msg->line, msg->function, msg->message);

	term_strip(buf, buf, strlen(buf) + 1);
	syslog(syslog_level, "%s", buf);
}

static char *logger_strip_verbose_magic(const char *message, int level)
{
	const char *begin, *end;
	char *stripped_message, *dst;
	char magic = -(level + 1);

	if (!(stripped_message = ast_malloc(strlen(message) + 1))) {
		return NULL;
	}

	begin = message;
	dst = stripped_message;
	do {
		end = strchr(begin, magic);
		if (end) {
			size_t len = end - begin;
			memcpy(dst, begin, len);
			begin = end + 1;
			dst += len;
		} else {
			strcpy(dst, begin); /* safe */
			break;
		}
	} while (1);

	return stripped_message;
}

/*! \brief Print a normal log message to the channels */
static void logger_print_normal(struct logmsg *logmsg)
{
	struct logchannel *chan = NULL;
	char buf[BUFSIZ];
	struct verb *v = NULL;
	char *tmpmsg;
	int level = 0;

	if (logmsg->level == __LOG_VERBOSE) {

		/* Iterate through the list of verbosers and pass them the log message string */
		AST_RWLIST_RDLOCK(&verbosers);
		AST_RWLIST_TRAVERSE(&verbosers, v, list)
			v->verboser(logmsg->message);
		AST_RWLIST_UNLOCK(&verbosers);

		level = VERBOSE_MAGIC2LEVEL(logmsg->message);

		tmpmsg = logger_strip_verbose_magic(logmsg->message, level);
		if (tmpmsg) {
			ast_string_field_set(logmsg, message, tmpmsg);
			ast_free(tmpmsg);
		}
	}

	AST_RWLIST_RDLOCK(&logchannels);

	if (!AST_RWLIST_EMPTY(&logchannels)) {
		AST_RWLIST_TRAVERSE(&logchannels, chan, list) {
			char call_identifier_str[13];

			if (logmsg->callid) {
				snprintf(call_identifier_str, sizeof(call_identifier_str), "[C-%08x]", (unsigned)logmsg->callid->call_identifier);
			} else {
				call_identifier_str[0] = '\0';
			}


			/* If the channel is disabled, then move on to the next one */
			if (chan->disabled) {
				continue;
			}
			if (logmsg->level == __LOG_VERBOSE
				&& (((chan->verbosity < 0) ? option_verbose : chan->verbosity)) < level) {
				continue;
			}

			/* Check syslog channels */
			if (chan->type == LOGTYPE_SYSLOG && (chan->logmask & (1 << logmsg->level))) {
				ast_log_vsyslog(logmsg);
			/* Console channels */
			} else if (chan->type == LOGTYPE_CONSOLE && (chan->logmask & (1 << logmsg->level))) {
				char linestr[128];

				/* If the level is verbose, then skip it */
				if (logmsg->level == __LOG_VERBOSE)
					continue;

				/* Turn the numerical line number into a string */
				snprintf(linestr, sizeof(linestr), "%d", logmsg->line);
				/* Build string to print out */
				snprintf(buf, sizeof(buf), "[%s] " COLORIZE_FMT "[%d]%s: " COLORIZE_FMT ":" COLORIZE_FMT " " COLORIZE_FMT ": %s",
					 logmsg->date,
					 COLORIZE(colors[logmsg->level], 0, logmsg->level_name),
					 logmsg->lwp,
					 call_identifier_str,
					 COLORIZE(COLOR_BRWHITE, 0, logmsg->file),
					 COLORIZE(COLOR_BRWHITE, 0, linestr),
					 COLORIZE(COLOR_BRWHITE, 0, logmsg->function),
					 logmsg->message);
				/* Print out */
				ast_console_puts_mutable(buf, logmsg->level);
			/* File channels */
			} else if (chan->type == LOGTYPE_FILE && (chan->logmask & (1 << logmsg->level))) {
				int res = 0;

				/* If no file pointer exists, skip it */
				if (!chan->fileptr) {
					continue;
				}

				/* Print out to the file */
				res = fprintf(chan->fileptr, "[%s] %s[%d]%s %s: %s",
					      logmsg->date, logmsg->level_name, logmsg->lwp, call_identifier_str,
					      logmsg->file, term_strip(buf, logmsg->message, BUFSIZ));
				if (res <= 0 && !ast_strlen_zero(logmsg->message)) {
					fprintf(stderr, "**** Asterisk Logging Error: ***********\n");
					if (errno == ENOMEM || errno == ENOSPC)
						fprintf(stderr, "Asterisk logging error: Out of disk space, can't log to log file %s\n", chan->filename);
					else
						fprintf(stderr, "Logger Warning: Unable to write to log file '%s': %s (disabled)\n", chan->filename, strerror(errno));
					/*** DOCUMENTATION
						<managerEventInstance>
							<synopsis>Raised when a logging channel is disabled.</synopsis>
							<syntax>
								<parameter name="Channel">
									<para>The name of the logging channel.</para>
								</parameter>
							</syntax>
						</managerEventInstance>
					***/
					manager_event(EVENT_FLAG_SYSTEM, "LogChannel", "Channel: %s\r\nEnabled: No\r\nReason: %d - %s\r\n", chan->filename, errno, strerror(errno));
					chan->disabled = 1;
				} else if (res > 0) {
					fflush(chan->fileptr);
				}
			}
		}
	} else if (logmsg->level != __LOG_VERBOSE) {
		fputs(logmsg->message, stdout);
	}

	AST_RWLIST_UNLOCK(&logchannels);

	/* If we need to reload because of the file size, then do so */
	if (filesize_reload_needed) {
		reload_logger(-1, NULL);
		ast_verb(1, "Rotated Logs Per SIGXFSZ (Exceeded file size limit)\n");
	}

	return;
}

/*! \brief Actual logging thread */
static void *logger_thread(void *data)
{
	struct logmsg *next = NULL, *msg = NULL;

	for (;;) {
		/* We lock the message list, and see if any message exists... if not we wait on the condition to be signalled */
		AST_LIST_LOCK(&logmsgs);
		if (AST_LIST_EMPTY(&logmsgs)) {
			if (close_logger_thread) {
				AST_LIST_UNLOCK(&logmsgs);
				break;
			} else {
				ast_cond_wait(&logcond, &logmsgs.lock);
			}
		}
		next = AST_LIST_FIRST(&logmsgs);
		AST_LIST_HEAD_INIT_NOLOCK(&logmsgs);
		AST_LIST_UNLOCK(&logmsgs);

		/* Otherwise go through and process each message in the order added */
		while ((msg = next)) {
			/* Get the next entry now so that we can free our current structure later */
			next = AST_LIST_NEXT(msg, list);

			/* Depending on the type, send it to the proper function */
			logger_print_normal(msg);

			/* Free the data since we are done */
			logmsg_free(msg);
		}
	}

	return NULL;
}

/*!
 * \internal
 * \brief Initialize the logger queue.
 *
 * \note Assumes logchannels is write locked on entry.
 *
 * \return Nothing
 */
static void logger_queue_init(void)
{
	ast_unload_realtime("queue_log");
	if (logfiles.queue_log) {
		char qfname[PATH_MAX];

		if (logger_queue_rt_start()) {
			return;
		}

		/* Open the log file. */
		snprintf(qfname, sizeof(qfname), "%s/%s", ast_config_AST_LOG_DIR,
			queue_log_name);
		if (qlog) {
			/* Just in case it was already open. */
			fclose(qlog);
		}
		qlog = fopen(qfname, "a");
		if (!qlog) {
			ast_log(LOG_ERROR, "Unable to create queue log: %s\n", strerror(errno));
		}
	}
}

/*!
 * \brief Start the ast_queue_log() logger.
 *
 * \note Called when the system is fully booted after startup
 * so preloaded realtime modules can get up.
 *
 * \return Nothing
 */
void logger_queue_start(void)
{
	/* Must not be called before the logger is initialized. */
	ast_assert(logger_initialized);

	AST_RWLIST_WRLOCK(&logchannels);
	if (!queuelog_init) {
		logger_queue_init();
		queuelog_init = 1;
		AST_RWLIST_UNLOCK(&logchannels);
		ast_queue_log("NONE", "NONE", "NONE", "QUEUESTART", "%s", "");
	} else {
		AST_RWLIST_UNLOCK(&logchannels);
	}
}

int init_logger(void)
{
	int res;
	/* auto rotate if sig SIGXFSZ comes a-knockin */
	sigaction(SIGXFSZ, &handle_SIGXFSZ, NULL);

	/* Re-initialize the logmsgs mutex.  The recursive mutex can be accessed prior
 	 * to Asterisk being forked into the background, which can cause the thread
 	 * ID tracked by the underlying pthread mutex to be different than the ID of
 	 * the thread that unlocks the mutex.  Since init_logger is called after the
 	 * fork, it is safe to initialize the mutex here for future accesses.
 	 */
	ast_mutex_destroy(&logmsgs.lock);
	ast_mutex_init(&logmsgs.lock);
	ast_cond_init(&logcond, NULL);

	/* start logger thread */
	if (ast_pthread_create(&logthread, NULL, logger_thread, NULL) < 0) {
		ast_cond_destroy(&logcond);
		return -1;
	}

	/* register the logger cli commands */
	ast_cli_register_multiple(cli_logger, ARRAY_LEN(cli_logger));

	ast_mkdir(ast_config_AST_LOG_DIR, 0777);

	/* create log channels */
	res = init_logger_chain(0 /* locked */, NULL);
	ast_verb_update();
	logger_initialized = 1;
	if (res) {
		ast_log(LOG_ERROR, "Errors detected in logger.conf.  Default console logging is being used.\n");
	}

	return 0;
}

void close_logger(void)
{
	struct logchannel *f = NULL;
	struct verb *cur = NULL;

	ast_cli_unregister_multiple(cli_logger, ARRAY_LEN(cli_logger));

	logger_initialized = 0;

	/* Stop logger thread */
	AST_LIST_LOCK(&logmsgs);
	close_logger_thread = 1;
	ast_cond_signal(&logcond);
	AST_LIST_UNLOCK(&logmsgs);

	if (logthread != AST_PTHREADT_NULL)
		pthread_join(logthread, NULL);

	AST_RWLIST_WRLOCK(&verbosers);
	while ((cur = AST_LIST_REMOVE_HEAD(&verbosers, list))) {
		ast_free(cur);
	}
	AST_RWLIST_UNLOCK(&verbosers);

	AST_RWLIST_WRLOCK(&logchannels);

	if (qlog) {
		fclose(qlog);
		qlog = NULL;
	}

	while ((f = AST_LIST_REMOVE_HEAD(&logchannels, list))) {
		if (f->fileptr && (f->fileptr != stdout) && (f->fileptr != stderr)) {
			fclose(f->fileptr);
			f->fileptr = NULL;
		}
		ast_free(f);
	}

	closelog(); /* syslog */

	AST_RWLIST_UNLOCK(&logchannels);
}

void ast_callid_strnprint(char *buffer, size_t buffer_size, struct ast_callid *callid)
{
	snprintf(buffer, buffer_size, "[C-%08x]", (unsigned)callid->call_identifier);
}

struct ast_callid *ast_create_callid(void)
{
	struct ast_callid *call;

	call = ao2_alloc_options(sizeof(*call), NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!call) {
		ast_log(LOG_ERROR, "Could not allocate callid struct.\n");
		return NULL;
	}

	call->call_identifier = ast_atomic_fetchadd_int(&next_unique_callid, +1);
#ifdef TEST_FRAMEWORK
	ast_debug(3, "CALL_ID [C-%08x] created by thread.\n", (unsigned)call->call_identifier);
#endif
	return call;
}

struct ast_callid *ast_read_threadstorage_callid(void)
{
	struct ast_callid **callid;

	callid = ast_threadstorage_get(&unique_callid, sizeof(*callid));
	if (callid && *callid) {
		ast_callid_ref(*callid);
		return *callid;
	}

	return NULL;

}

int ast_callid_threadassoc_change(struct ast_callid *callid)
{
	struct ast_callid **id = ast_threadstorage_get(&unique_callid, sizeof(*id));

	if (!id) {
		ast_log(LOG_ERROR, "Failed to allocate thread storage.\n");
		return -1;
	}

	if (*id && (*id != callid)) {
#ifdef TEST_FRAMEWORK
		ast_debug(3, "CALL_ID [C-%08x] being removed from thread.\n", (unsigned)(*id)->call_identifier);
#endif
		*id = ast_callid_unref(*id);
		*id = NULL;
	}

	if (!(*id) && callid) {
		/* callid will be unreffed at thread destruction */
		ast_callid_ref(callid);
		*id = callid;
#ifdef TEST_FRAMEWORK
		ast_debug(3, "CALL_ID [C-%08x] bound to thread.\n", (unsigned)callid->call_identifier);
#endif
	}

	return 0;
}

int ast_callid_threadassoc_add(struct ast_callid *callid)
{
	struct ast_callid **pointing;

	pointing = ast_threadstorage_get(&unique_callid, sizeof(*pointing));
	if (!(pointing)) {
		ast_log(LOG_ERROR, "Failed to allocate thread storage.\n");
		return -1;
	}

	if (!(*pointing)) {
		/* callid will be unreffed at thread destruction */
		ast_callid_ref(callid);
		*pointing = callid;
#ifdef TEST_FRAMEWORK
		ast_debug(3, "CALL_ID [C-%08x] bound to thread.\n", (unsigned)callid->call_identifier);
#endif
	} else {
		ast_log(LOG_WARNING, "Attempted to ast_callid_threadassoc_add on thread already associated with a callid.\n");
		return 1;
	}

	return 0;
}

int ast_callid_threadassoc_remove(void)
{
	struct ast_callid **pointing;

	pointing = ast_threadstorage_get(&unique_callid, sizeof(*pointing));
	if (!(pointing)) {
		ast_log(LOG_ERROR, "Failed to allocate thread storage.\n");
		return -1;
	}

	if (!(*pointing)) {
		ast_log(LOG_ERROR, "Tried to clean callid thread storage with no callid in thread storage.\n");
		return -1;
	} else {
#ifdef TEST_FRAMEWORK
		ast_debug(3, "CALL_ID [C-%08x] being removed from thread.\n", (unsigned)(*pointing)->call_identifier);
#endif
		*pointing = ast_callid_unref(*pointing);
		return 0;
	}
}

int ast_callid_threadstorage_auto(struct ast_callid **callid)
{
	struct ast_callid *tmp;

	/* Start by trying to see if a callid is available from thread storage */
	tmp = ast_read_threadstorage_callid();
	if (tmp) {
		*callid = tmp;
		return 0;
	}

	/* If that failed, try to create a new one and bind it. */
	tmp = ast_create_callid();
	if (tmp) {
		ast_callid_threadassoc_add(tmp);
		*callid = tmp;
		return 1;
	}

	/* If neither worked, then something must have gone wrong. */
	return -1;
}

void ast_callid_threadstorage_auto_clean(struct ast_callid *callid, int callid_created)
{
	if (callid) {
		/* If the callid was created rather than simply grabbed from the thread storage, we need to unbind here. */
		if (callid_created == 1) {
			ast_callid_threadassoc_remove();
		}
		callid = ast_callid_unref(callid);
	}
}

/*!
 * \internal
 * \brief thread storage cleanup function for unique_callid
 */
static void unique_callid_cleanup(void *data)
{
	struct ast_callid **callid = data;

	if (*callid) {
		ast_callid_unref(*callid);
	}

	ast_free(data);
}

/*!
 * \brief send log messages to syslog and/or the console
 */
static void __attribute__((format(printf, 6, 0))) ast_log_full(int level, const char *file, int line, const char *function, struct ast_callid *callid, const char *fmt, va_list ap)
{
	struct logmsg *logmsg = NULL;
	struct ast_str *buf = NULL;
	struct ast_tm tm;
	struct timeval now = ast_tvnow();
	int res = 0;
	char datestring[256];

	if (!(buf = ast_str_thread_get(&log_buf, LOG_BUF_INIT_SIZE)))
		return;

	if (level != __LOG_VERBOSE && AST_RWLIST_EMPTY(&logchannels)) {
		/*
		 * we don't have the logger chain configured yet,
		 * so just log to stdout
		 */
		int result;
		result = ast_str_set_va(&buf, BUFSIZ, fmt, ap); /* XXX BUFSIZ ? */
		if (result != AST_DYNSTR_BUILD_FAILED) {
			term_filter_escapes(ast_str_buffer(buf));
			fputs(ast_str_buffer(buf), stdout);
		}
		return;
	}

	/* Ignore anything that never gets logged anywhere */
	if (level != __LOG_VERBOSE && !(global_logmask & (1 << level)))
		return;

	/* Build string */
	res = ast_str_set_va(&buf, BUFSIZ, fmt, ap);

	/* If the build failed, then abort and free this structure */
	if (res == AST_DYNSTR_BUILD_FAILED)
		return;

	/* Create a new logging message */
	if (!(logmsg = ast_calloc_with_stringfields(1, struct logmsg, res + 128)))
		return;

	/* Copy string over */
	ast_string_field_set(logmsg, message, ast_str_buffer(buf));

	/* Set type */
	if (level == __LOG_VERBOSE) {
		logmsg->type = LOGMSG_VERBOSE;
	} else {
		logmsg->type = LOGMSG_NORMAL;
	}

	if (display_callids && callid) {
		logmsg->callid = ast_callid_ref(callid);
		/* callid will be unreffed at logmsg destruction */
	}

	/* Create our date/time */
	ast_localtime(&now, &tm, NULL);
	ast_strftime(datestring, sizeof(datestring), dateformat, &tm);
	ast_string_field_set(logmsg, date, datestring);

	/* Copy over data */
	logmsg->level = level;
	logmsg->line = line;
	ast_string_field_set(logmsg, level_name, levels[level]);
	ast_string_field_set(logmsg, file, file);
	ast_string_field_set(logmsg, function, function);
	logmsg->lwp = ast_get_tid();

	/* If the logger thread is active, append it to the tail end of the list - otherwise skip that step */
	if (logthread != AST_PTHREADT_NULL) {
		AST_LIST_LOCK(&logmsgs);
		if (close_logger_thread) {
			/* Logger is either closing or closed.  We cannot log this message. */
			logmsg_free(logmsg);
		} else {
			AST_LIST_INSERT_TAIL(&logmsgs, logmsg, list);
			ast_cond_signal(&logcond);
		}
		AST_LIST_UNLOCK(&logmsgs);
	} else {
		logger_print_normal(logmsg);
		logmsg_free(logmsg);
	}
}

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	struct ast_callid *callid;
	va_list ap;

	callid = ast_read_threadstorage_callid();

	va_start(ap, fmt);
	if (level == __LOG_VERBOSE) {
		__ast_verbose_ap(file, line, function, 0, callid, fmt, ap);
	} else {
		ast_log_full(level, file, line, function, callid, fmt, ap);
	}
	va_end(ap);

	if (callid) {
		ast_callid_unref(callid);
	}
}

void ast_log_safe(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	va_list ap;
	void *recursed = ast_threadstorage_get_ptr(&in_safe_log);
	struct ast_callid *callid;

	if (recursed) {
		return;
	}

	if (ast_threadstorage_set_ptr(&in_safe_log, (void*)1)) {
		/* We've failed to set the flag that protects against
		 * recursion, so bail. */
		return;
	}

	callid = ast_read_threadstorage_callid();

	va_start(ap, fmt);
	ast_log_full(level, file, line, function, callid, fmt, ap);
	va_end(ap);

	if (callid) {
		ast_callid_unref(callid);
	}

	/* Clear flag so the next allocation failure can be logged. */
	ast_threadstorage_set_ptr(&in_safe_log, NULL);
}

void ast_log_callid(int level, const char *file, int line, const char *function, struct ast_callid *callid, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ast_log_full(level, file, line, function, callid, fmt, ap);
	va_end(ap);
}


void ast_log_backtrace(void)
{
#ifdef HAVE_BKTR
	struct ast_bt *bt;
	int i = 0;
	char **strings;

	if (!(bt = ast_bt_create())) {
		ast_log(LOG_WARNING, "Unable to allocate space for backtrace structure\n");
		return;
	}

	if ((strings = ast_bt_get_symbols(bt->addresses, bt->num_frames))) {
		ast_verbose("Got %d backtrace record%c\n", bt->num_frames, bt->num_frames != 1 ? 's' : ' ');
		for (i = 3; i < bt->num_frames - 2; i++) {
			ast_verbose("#%d: [%p] %s\n", i - 3, bt->addresses[i], strings[i]);
		}

		ast_std_free(strings);
	} else {
		ast_verbose("Could not allocate memory for backtrace\n");
	}
	ast_bt_destroy(bt);
#else
	ast_log(LOG_WARNING, "Must run configure with '--with-execinfo' for stack backtraces.\n");
#endif /* defined(HAVE_BKTR) */
}

void __ast_verbose_ap(const char *file, int line, const char *func, int level, struct ast_callid *callid, const char *fmt, va_list ap)
{
	const char *p;
	struct ast_str *prefixed, *buf;
	int res = 0;
	signed char magic = level > 9 ? -10 : -level - 1; /* 0 => -1, 1 => -2, etc.  Can't pass NUL, as it is EOS-delimiter */

	/* For compatibility with modules still calling ast_verbose() directly instead of using ast_verb() */
	if (level < 0) {
		if (!strncmp(fmt, VERBOSE_PREFIX_4, strlen(VERBOSE_PREFIX_4))) {
			magic = -5;
		} else if (!strncmp(fmt, VERBOSE_PREFIX_3, strlen(VERBOSE_PREFIX_3))) {
			magic = -4;
		} else if (!strncmp(fmt, VERBOSE_PREFIX_2, strlen(VERBOSE_PREFIX_2))) {
			magic = -3;
		} else if (!strncmp(fmt, VERBOSE_PREFIX_1, strlen(VERBOSE_PREFIX_1))) {
			magic = -2;
		} else {
			magic = -1;
		}
	}

	if (!(prefixed = ast_str_thread_get(&verbose_buf, VERBOSE_BUF_INIT_SIZE)) ||
	    !(buf = ast_str_thread_get(&verbose_build_buf, VERBOSE_BUF_INIT_SIZE))) {
		return;
	}

	res = ast_str_set_va(&buf, 0, fmt, ap);
	/* If the build failed then we can drop this allocated message */
	if (res == AST_DYNSTR_BUILD_FAILED) {
		return;
	}

	ast_str_reset(prefixed);

	/* for every newline found in the buffer add verbose prefix data */
	fmt = ast_str_buffer(buf);
	do {
		if (!(p = strchr(fmt, '\n'))) {
			p = strchr(fmt, '\0') - 1;
		}
		++p;

		ast_str_append(&prefixed, 0, "%c", (char)magic);
		ast_str_append_substr(&prefixed, 0, fmt, p - fmt);
		fmt = p;
	} while (p && *p);

	ast_log_callid(__LOG_VERBOSE, file, line, func, callid, "%s", ast_str_buffer(prefixed));
}

void __ast_verbose(const char *file, int line, const char *func, int level, const char *fmt, ...)
{
	struct ast_callid *callid;
	va_list ap;

	callid = ast_read_threadstorage_callid();

	va_start(ap, fmt);
	__ast_verbose_ap(file, line, func, level, callid, fmt, ap);
	va_end(ap);

	if (callid) {
		ast_callid_unref(callid);
	}
}

void __ast_verbose_callid(const char *file, int line, const char *func, int level, struct ast_callid *callid, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	__ast_verbose_ap(file, line, func, level, callid, fmt, ap);
	va_end(ap);
}

/* No new code should use this directly, but we have the ABI for backwards compat */
#undef ast_verbose
void __attribute__((format(printf, 1,2))) ast_verbose(const char *fmt, ...);
void ast_verbose(const char *fmt, ...)
{
	struct ast_callid *callid;
	va_list ap;

	callid = ast_read_threadstorage_callid();

	va_start(ap, fmt);
	__ast_verbose_ap("", 0, "", 0, callid, fmt, ap);
	va_end(ap);

	if (callid) {
		ast_callid_unref(callid);
	}
}

/*! Console verbosity level node. */
struct verb_console {
	/*! List node link */
	AST_LIST_ENTRY(verb_console) node;
	/*! Console verbosity level. */
	int *level;
};

/*! Registered console verbosity levels */
static AST_RWLIST_HEAD_STATIC(verb_consoles, verb_console);

/*! ast_verb_update() reentrancy protection lock. */
AST_MUTEX_DEFINE_STATIC(verb_update_lock);

void ast_verb_update(void)
{
	struct logchannel *log;
	struct verb_console *console;
	int verb_level;

	ast_mutex_lock(&verb_update_lock);

	AST_RWLIST_RDLOCK(&verb_consoles);

	/* Default to the root console verbosity. */
	verb_level = option_verbose;

	/* Determine max remote console level. */
	AST_LIST_TRAVERSE(&verb_consoles, console, node) {
		if (verb_level < *console->level) {
			verb_level = *console->level;
		}
	}
	AST_RWLIST_UNLOCK(&verb_consoles);

	/* Determine max logger channel level. */
	AST_RWLIST_RDLOCK(&logchannels);
	AST_RWLIST_TRAVERSE(&logchannels, log, list) {
		if (verb_level < log->verbosity) {
			verb_level = log->verbosity;
		}
	}
	AST_RWLIST_UNLOCK(&logchannels);

	ast_verb_sys_level = verb_level;

	ast_mutex_unlock(&verb_update_lock);
}

/*!
 * \internal
 * \brief Unregister a console verbose level.
 *
 * \param console Which console to unregister.
 *
 * \return Nothing
 */
static void verb_console_unregister(struct verb_console *console)
{
	AST_RWLIST_WRLOCK(&verb_consoles);
	console = AST_RWLIST_REMOVE(&verb_consoles, console, node);
	AST_RWLIST_UNLOCK(&verb_consoles);
	if (console) {
		ast_verb_update();
	}
}

static void verb_console_free(void *v_console)
{
	struct verb_console *console = v_console;

	verb_console_unregister(console);
	ast_free(console);
}

/*! Thread specific console verbosity level node. */
AST_THREADSTORAGE_CUSTOM(my_verb_console, NULL, verb_console_free);

void ast_verb_console_register(int *level)
{
	struct verb_console *console;

	console = ast_threadstorage_get(&my_verb_console, sizeof(*console));
	if (!console || !level) {
		return;
	}
	console->level = level;

	AST_RWLIST_WRLOCK(&verb_consoles);
	AST_RWLIST_INSERT_HEAD(&verb_consoles, console, node);
	AST_RWLIST_UNLOCK(&verb_consoles);
	ast_verb_update();
}

void ast_verb_console_unregister(void)
{
	struct verb_console *console;

	console = ast_threadstorage_get(&my_verb_console, sizeof(*console));
	if (!console) {
		return;
	}
	verb_console_unregister(console);
}

int ast_verb_console_get(void)
{
	struct verb_console *console;
	int verb_level;

	console = ast_threadstorage_get(&my_verb_console, sizeof(*console));
	AST_RWLIST_RDLOCK(&verb_consoles);
	if (!console) {
		verb_level = 0;
	} else if (console->level) {
		verb_level = *console->level;
	} else {
		verb_level = option_verbose;
	}
	AST_RWLIST_UNLOCK(&verb_consoles);
	return verb_level;
}

void ast_verb_console_set(int verb_level)
{
	struct verb_console *console;

	console = ast_threadstorage_get(&my_verb_console, sizeof(*console));
	if (!console) {
		return;
	}

	AST_RWLIST_WRLOCK(&verb_consoles);
	if (console->level) {
		*console->level = verb_level;
	} else {
		option_verbose = verb_level;
	}
	AST_RWLIST_UNLOCK(&verb_consoles);
	ast_verb_update();
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

static void update_logchannels(void)
{
	struct logchannel *cur;

	AST_RWLIST_WRLOCK(&logchannels);

	global_logmask = 0;

	AST_RWLIST_TRAVERSE(&logchannels, cur, list) {
		make_components(cur);
		global_logmask |= cur->logmask;
	}

	AST_RWLIST_UNLOCK(&logchannels);
}

int ast_logger_register_level(const char *name)
{
	unsigned int level;
	unsigned int available = 0;

	AST_RWLIST_WRLOCK(&logchannels);

	for (level = 0; level < ARRAY_LEN(levels); level++) {
		if ((level >= 16) && !available && !levels[level]) {
			available = level;
			continue;
		}

		if (levels[level] && !strcasecmp(levels[level], name)) {
			ast_log(LOG_WARNING,
				"Unable to register dynamic logger level '%s': a standard logger level uses that name.\n",
				name);
			AST_RWLIST_UNLOCK(&logchannels);

			return -1;
		}
	}

	if (!available) {
		ast_log(LOG_WARNING,
			"Unable to register dynamic logger level '%s'; maximum number of levels registered.\n",
			name);
		AST_RWLIST_UNLOCK(&logchannels);

		return -1;
	}

	levels[available] = ast_strdup(name);

	AST_RWLIST_UNLOCK(&logchannels);

	ast_debug(1, "Registered dynamic logger level '%s' with index %u.\n", name, available);

	update_logchannels();

	return available;
}

void ast_logger_unregister_level(const char *name)
{
	unsigned int found = 0;
	unsigned int x;

	AST_RWLIST_WRLOCK(&logchannels);

	for (x = 16; x < ARRAY_LEN(levels); x++) {
		if (!levels[x]) {
			continue;
		}

		if (strcasecmp(levels[x], name)) {
			continue;
		}

		found = 1;
		break;
	}

	if (found) {
		/* take this level out of the global_logmask, to ensure that no new log messages
		 * will be queued for it
		 */

		global_logmask &= ~(1 << x);

		ast_free(levels[x]);
		levels[x] = NULL;
		AST_RWLIST_UNLOCK(&logchannels);

		ast_debug(1, "Unregistered dynamic logger level '%s' with index %u.\n", name, x);

		update_logchannels();
	} else {
		AST_RWLIST_UNLOCK(&logchannels);
	}
}

const char *ast_logger_get_dateformat(void)
{
	return dateformat;
}

