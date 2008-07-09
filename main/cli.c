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
 * \brief Standard Command Line Interface
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_MODULE_DIR */
#include <sys/signal.h>
#include <signal.h>
#include <ctype.h>
#include <regex.h>

#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/lock.h"
#include "editline/readline/readline.h"
#include "asterisk/threadstorage.h"

/*!
 * \brief map a debug or verbose value to a filename
 */
struct ast_debug_file {
	unsigned int level;
	AST_RWLIST_ENTRY(ast_debug_file) entry;
	char filename[0];
};

AST_RWLIST_HEAD(debug_file_list, ast_debug_file);

/*! list of filenames and their debug settings */
static struct debug_file_list debug_files;
/*! list of filenames and their verbose settings */
static struct debug_file_list verbose_files;

AST_THREADSTORAGE(ast_cli_buf);

/*! \brief Initial buffer size for resulting strings in ast_cli() */
#define AST_CLI_INITLEN   256

void ast_cli(int fd, const char *fmt, ...)
{
	int res;
	struct ast_str *buf;
	va_list ap;

	if (!(buf = ast_str_thread_get(&ast_cli_buf, AST_CLI_INITLEN)))
		return;

	va_start(ap, fmt);
	res = ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	if (res != AST_DYNSTR_BUILD_FAILED)
		ast_carefulwrite(fd, buf->str, strlen(buf->str), 100);
}

unsigned int ast_debug_get_by_file(const char *file) 
{
	struct ast_debug_file *adf;
	unsigned int res = 0;

	AST_RWLIST_RDLOCK(&debug_files);
	AST_LIST_TRAVERSE(&debug_files, adf, entry) {
		if (!strncasecmp(adf->filename, file, strlen(adf->filename))) {
			res = adf->level;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&debug_files);

	return res;
}

unsigned int ast_verbose_get_by_file(const char *file) 
{
	struct ast_debug_file *adf;
	unsigned int res = 0;

	AST_RWLIST_RDLOCK(&verbose_files);
	AST_LIST_TRAVERSE(&verbose_files, adf, entry) {
		if (!strncasecmp(adf->filename, file, strlen(file))) {
			res = adf->level;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&verbose_files);

	return res;
}

static AST_RWLIST_HEAD_STATIC(helpers, ast_cli_entry);

static char *complete_fn(const char *word, int state)
{
	char *c, *d;
	char filename[256];

	if (word[0] == '/')
		ast_copy_string(filename, word, sizeof(filename));
	else
		snprintf(filename, sizeof(filename), "%s/%s", ast_config_AST_MODULE_DIR, word);

	c = d = filename_completion_function(filename, state);
	
	if (c && word[0] != '/')
		c += (strlen(ast_config_AST_MODULE_DIR) + 1);
	if (c)
		c = ast_strdup(c);
	if (d)
		free(d);
	
	return c;
}

static char *handle_load(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	/* "module load <mod>" */
	switch (cmd) {
	case CLI_INIT:
		e->command = "module load";
		e->usage =
			"Usage: module load <module name>\n"
			"       Loads the specified module into Asterisk.\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos != e->args)
			return NULL;
		return complete_fn(a->word, a->n);
	}
	if (a->argc != e->args + 1)
		return CLI_SHOWUSAGE;
	if (ast_load_resource(a->argv[e->args])) {
		ast_cli(a->fd, "Unable to load module %s\n", a->argv[e->args]);
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

static char *handle_load_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res = handle_load(e, cmd, a);
	if (cmd == CLI_INIT)
		e->command = "load";
	return res;
}

static char *handle_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int x;

	switch (cmd) {
	case CLI_INIT:
		e->command = "module reload";
		e->usage =
			"Usage: module reload [module ...]\n"
			"       Reloads configuration files for all listed modules which support\n"
			"       reloading, or for all supported modules if none are listed.\n";
		return NULL;

	case CLI_GENERATE:
		return ast_module_helper(a->line, a->word, a->pos, a->n, a->pos, 1);
	}
	if (a->argc == e->args) {
		ast_module_reload(NULL);
		return CLI_SUCCESS;
	}
	for (x = e->args; x < a->argc; x++) {
		int res = ast_module_reload(a->argv[x]);
		/* XXX reload has multiple error returns, including -1 on error and 2 on success */
		switch (res) {
		case 0:
			ast_cli(a->fd, "No such module '%s'\n", a->argv[x]);
			break;
		case 1:
			ast_cli(a->fd, "Module '%s' does not support reload\n", a->argv[x]);
			break;
		}
	}
	return CLI_SUCCESS;
}

static char *handle_reload_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *s = handle_reload(e, cmd, a);
	if (cmd == CLI_INIT)		/* override command name */
		e->command = "reload";
	return s;
}

/*! 
 * \brief Find the debug or verbose file setting 
 * \arg debug 1 for debug, 0 for verbose
 */
static struct ast_debug_file *find_debug_file(const char *fn, unsigned int debug)
{
	struct ast_debug_file *df = NULL;
	struct debug_file_list *dfl = debug ? &debug_files : &verbose_files;

	AST_LIST_TRAVERSE(dfl, df, entry) {
		if (!strcasecmp(df->filename, fn))
			break;
	}

	return df;
}

static char *handle_verbose(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval;
	int newlevel;
	int atleast = 0;
	int fd = a->fd;
	int argc = a->argc;
	char **argv = a->argv;
	int *dst;
	char *what;
	struct debug_file_list *dfl;
	struct ast_debug_file *adf;
	char *fn;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core set {debug|verbose} [off|atleast]";
		e->usage =
			"Usage: core set {debug|verbose} [atleast] <level> [filename]\n"
			"       core set {debug|verbose} off\n"
			"       Sets level of debug or verbose messages to be displayed or \n"
			"       sets a filename to display debug messages from.\n"
			"	0 or off means no messages should be displayed.\n"
			"	Equivalent to -d[d[...]] or -v[v[v...]] on startup\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}
	/* all the above return, so we proceed with the handler.
	 * we are guaranteed to be called with argc >= e->args;
	 */

	if (argc < e->args)
		return CLI_SHOWUSAGE;
	if (!strcasecmp(argv[e->args - 2], "debug")) {
		dst = &option_debug;
		oldval = option_debug;
		what = "Core debug";
	} else {
		dst = &option_verbose;
		oldval = option_verbose;
		what = "Verbosity";
	}
	if (argc == e->args && !strcasecmp(argv[e->args - 1], "off")) {
		unsigned int debug = (*what == 'C');
		newlevel = 0;

		dfl = debug ? &debug_files : &verbose_files;

		AST_RWLIST_WRLOCK(dfl);
		while ((adf = AST_RWLIST_REMOVE_HEAD(dfl, entry)))
			ast_free(adf);
		ast_clear_flag(&ast_options, debug ? AST_OPT_FLAG_DEBUG_FILE : AST_OPT_FLAG_VERBOSE_FILE);
		AST_RWLIST_UNLOCK(dfl);

		goto done;
	}
	if (!strcasecmp(argv[e->args-1], "atleast"))
		atleast = 1;
	if (argc != e->args + atleast && argc != e->args + atleast + 1)
		return CLI_SHOWUSAGE;
	if (sscanf(argv[e->args + atleast - 1], "%d", &newlevel) != 1)
		return CLI_SHOWUSAGE;
	if (argc == e->args + atleast + 1) {
		unsigned int debug = (*what == 'C');
		dfl = debug ? &debug_files : &verbose_files;

		fn = argv[e->args + atleast];

		AST_RWLIST_WRLOCK(dfl);

		if ((adf = find_debug_file(fn, debug)) && !newlevel) {
			AST_RWLIST_REMOVE(dfl, adf, entry);
			if (AST_RWLIST_EMPTY(dfl))
				ast_clear_flag(&ast_options, debug ? AST_OPT_FLAG_DEBUG_FILE : AST_OPT_FLAG_VERBOSE_FILE);
			AST_RWLIST_UNLOCK(dfl);
			ast_cli(fd, "%s was %d and has been set to 0 for '%s'\n", what, adf->level, fn);
			ast_free(adf);
			return CLI_SUCCESS;
		}

		if (adf) {
			if ((atleast && newlevel < adf->level) || adf->level == newlevel) {
				ast_cli(fd, "%s is %d for '%s'\n", what, adf->level, fn);
				AST_RWLIST_UNLOCK(dfl);
				return CLI_SUCCESS;
			}
		} else if (!(adf = ast_calloc(1, sizeof(*adf) + strlen(fn) + 1))) {
			AST_RWLIST_UNLOCK(dfl);
			return CLI_FAILURE;
		}

		oldval = adf->level;
		adf->level = newlevel;
		strcpy(adf->filename, fn);

		ast_set_flag(&ast_options, debug ? AST_OPT_FLAG_DEBUG_FILE : AST_OPT_FLAG_VERBOSE_FILE);

		AST_RWLIST_INSERT_TAIL(dfl, adf, entry);
		AST_RWLIST_UNLOCK(dfl);

		ast_cli(fd, "%s was %d and has been set to %d for '%s'\n", what, oldval, adf->level, adf->filename);

		return CLI_SUCCESS;
	}

done:
	if (!atleast || newlevel > *dst)
		*dst = newlevel;
	if (oldval > 0 && *dst == 0)
		ast_cli(fd, "%s is now OFF\n", what);
	else if (*dst > 0) {
		if (oldval == *dst)
			ast_cli(fd, "%s is at least %d\n", what, *dst);
		else
			ast_cli(fd, "%s was %d and is now %d\n", what, oldval, *dst);
	}

	return CLI_SUCCESS;
}

static char *handle_logger_mute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "logger mute";
		e->usage = 
			"Usage: logger mute\n"
			"       Disables logging output to the current console, making it possible to\n"
			"       gather information without being disturbed by scrolling lines.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 2 || a->argc > 3)
		return CLI_SHOWUSAGE;

	if (a->argc == 3 && !strcasecmp(a->argv[2], "silent"))
		ast_console_toggle_mute(a->fd, 1);
	else
		ast_console_toggle_mute(a->fd, 0);

	return CLI_SUCCESS;
}

static char *handle_unload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	/* "module unload mod_1 [mod_2 .. mod_N]" */
	int x;
	int force = AST_FORCE_SOFT;
	char *s;

	switch (cmd) {
	case CLI_INIT:
		e->command = "module unload";
		e->usage =
			"Usage: module unload [-f|-h] <module_1> [<module_2> ... ]\n"
			"       Unloads the specified module from Asterisk. The -f\n"
			"       option causes the module to be unloaded even if it is\n"
			"       in use (may cause a crash) and the -h module causes the\n"
			"       module to be unloaded even if the module says it cannot, \n"
			"       which almost always will cause a crash.\n";
		return NULL;

	case CLI_GENERATE:
		return ast_module_helper(a->line, a->word, a->pos, a->n, a->pos, 0);
	}
	if (a->argc < e->args + 1)
		return CLI_SHOWUSAGE;
	x = e->args;	/* first argument */
	s = a->argv[x];
	if (s[0] == '-') {
		if (s[1] == 'f')
			force = AST_FORCE_FIRM;
		else if (s[1] == 'h')
			force = AST_FORCE_HARD;
		else
			return CLI_SHOWUSAGE;
		if (a->argc < e->args + 2)	/* need at least one module name */
			return CLI_SHOWUSAGE;
		x++;	/* skip this argument */
	}

	for (; x < a->argc; x++) {
		if (ast_unload_resource(a->argv[x], force)) {
			ast_cli(a->fd, "Unable to unload resource %s\n", a->argv[x]);
			return CLI_FAILURE;
		}
	}
	return CLI_SUCCESS;
}

static char *handle_unload_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res = handle_unload(e, cmd, a);
	if (cmd == CLI_INIT)
		e->command = "unload";	/* XXX override */
	return res;
}

#define MODLIST_FORMAT  "%-30s %-40.40s %-10d\n"
#define MODLIST_FORMAT2 "%-30s %-40.40s %-10s\n"

AST_MUTEX_DEFINE_STATIC(climodentrylock);
static int climodentryfd = -1;

static int modlist_modentry(const char *module, const char *description, int usecnt, const char *like)
{
	/* Comparing the like with the module */
	if (strcasestr(module, like) ) {
		ast_cli(climodentryfd, MODLIST_FORMAT, module, description, usecnt);
		return 1;
	} 
	return 0;
}

static void print_uptimestr(int fd, struct timeval timeval, const char *prefix, int printsec)
{
	int x; /* the main part - years, weeks, etc. */
	struct ast_str *out;

#define SECOND (1)
#define MINUTE (SECOND*60)
#define HOUR (MINUTE*60)
#define DAY (HOUR*24)
#define WEEK (DAY*7)
#define YEAR (DAY*365)
#define NEEDCOMMA(x) ((x)? ",": "")	/* define if we need a comma */
	if (timeval.tv_sec < 0)	/* invalid, nothing to show */
		return;

	if (printsec)  {	/* plain seconds output */
		ast_cli(fd, "%s: %lu\n", prefix, (u_long)timeval.tv_sec);
		return;
	}
	out = ast_str_alloca(256);
	if (timeval.tv_sec > YEAR) {
		x = (timeval.tv_sec / YEAR);
		timeval.tv_sec -= (x * YEAR);
		ast_str_append(&out, 0, "%d year%s%s ", x, ESS(x),NEEDCOMMA(timeval.tv_sec));
	}
	if (timeval.tv_sec > WEEK) {
		x = (timeval.tv_sec / WEEK);
		timeval.tv_sec -= (x * WEEK);
		ast_str_append(&out, 0, "%d week%s%s ", x, ESS(x),NEEDCOMMA(timeval.tv_sec));
	}
	if (timeval.tv_sec > DAY) {
		x = (timeval.tv_sec / DAY);
		timeval.tv_sec -= (x * DAY);
		ast_str_append(&out, 0, "%d day%s%s ", x, ESS(x),NEEDCOMMA(timeval.tv_sec));
	}
	if (timeval.tv_sec > HOUR) {
		x = (timeval.tv_sec / HOUR);
		timeval.tv_sec -= (x * HOUR);
		ast_str_append(&out, 0, "%d hour%s%s ", x, ESS(x),NEEDCOMMA(timeval.tv_sec));
	}
	if (timeval.tv_sec > MINUTE) {
		x = (timeval.tv_sec / MINUTE);
		timeval.tv_sec -= (x * MINUTE);
		ast_str_append(&out, 0, "%d minute%s%s ", x, ESS(x),NEEDCOMMA(timeval.tv_sec));
	}
	x = timeval.tv_sec;
	if (x > 0 || out->used == 0)	/* if there is nothing, print 0 seconds */
		ast_str_append(&out, 0, "%d second%s ", x, ESS(x));
	ast_cli(fd, "%s: %s\n", prefix, out->str);
}

static char * handle_showuptime(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct timeval curtime = ast_tvnow();
	int printsec;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show uptime [seconds]";
		e->usage =
			"Usage: core show uptime [seconds]\n"
			"       Shows Asterisk uptime information.\n"
			"       The seconds word returns the uptime in seconds only.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}
	/* regular handler */
	if (a->argc == e->args && !strcasecmp(a->argv[e->args-1],"seconds"))
		printsec = 1;
	else if (a->argc == e->args-1)
		printsec = 0;
	else
		return CLI_SHOWUSAGE;
	if (ast_startuptime.tv_sec)
		print_uptimestr(a->fd, ast_tvsub(curtime, ast_startuptime), "System uptime", printsec);
	if (ast_lastreloadtime.tv_sec)
		print_uptimestr(a->fd, ast_tvsub(curtime, ast_lastreloadtime), "Last reload", printsec);
	return CLI_SUCCESS;
}

static char *handle_modlist(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *like;

	switch (cmd) {
	case CLI_INIT:
		e->command = "module show [like]";
		e->usage =
			"Usage: module show [like keyword]\n"
			"       Shows Asterisk modules currently in use, and usage statistics.\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos == e->args)
			return ast_module_helper(a->line, a->word, a->pos, a->n, a->pos, 0);
		else
			return NULL;
	}
	/* all the above return, so we proceed with the handler.
	 * we are guaranteed to have argc >= e->args
	 */
	if (a->argc == e->args - 1)
		like = "";
	else if (a->argc == e->args + 1 && !strcasecmp(a->argv[e->args-1], "like") )
		like = a->argv[e->args];
	else
		return CLI_SHOWUSAGE;
		
	ast_mutex_lock(&climodentrylock);
	climodentryfd = a->fd; /* global, protected by climodentrylock */
	ast_cli(a->fd, MODLIST_FORMAT2, "Module", "Description", "Use Count");
	ast_cli(a->fd,"%d modules loaded\n", ast_update_module_list(modlist_modentry, like));
	climodentryfd = -1;
	ast_mutex_unlock(&climodentrylock);
	return CLI_SUCCESS;
}
#undef MODLIST_FORMAT
#undef MODLIST_FORMAT2

static char *handle_showcalls(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct timeval curtime = ast_tvnow();
	int showuptime, printsec;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show calls [uptime]";
		e->usage =
			"Usage: core show calls [uptime] [seconds]\n"
			"       Lists number of currently active calls and total number of calls\n"
			"       processed through PBX since last restart. If 'uptime' is specified\n"
			"       the system uptime is also displayed. If 'seconds' is specified in\n"
			"       addition to 'uptime', the system uptime is displayed in seconds.\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos != e->args)
			return NULL;
		return a->n == 0  ? ast_strdup("seconds") : NULL;
	}

	/* regular handler */
	if (a->argc >= e->args && !strcasecmp(a->argv[e->args-1],"uptime")) {
		showuptime = 1;

		if (a->argc == e->args+1 && !strcasecmp(a->argv[e->args],"seconds"))
			printsec = 1;
		else if (a->argc == e->args)
			printsec = 0;
		else
			return CLI_SHOWUSAGE;
	} else if (a->argc == e->args-1) {
		showuptime = 0;
		printsec = 0;
	} else
		return CLI_SHOWUSAGE;

	if (option_maxcalls) {
		ast_cli(a->fd, "%d of %d max active call%s (%5.2f%% of capacity)\n",
		   ast_active_calls(), option_maxcalls, ESS(ast_active_calls()),
		   ((double)ast_active_calls() / (double)option_maxcalls) * 100.0);
	} else {
		ast_cli(a->fd, "%d active call%s\n", ast_active_calls(), ESS(ast_active_calls()));
	}
   
	ast_cli(a->fd, "%d call%s processed\n", ast_processed_calls(), ESS(ast_processed_calls()));

	if (ast_startuptime.tv_sec && showuptime) {
		print_uptimestr(a->fd, ast_tvsub(curtime, ast_startuptime), "System uptime", printsec);
	}

	return RESULT_SUCCESS;
}

static char *handle_chanlist(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_STRING  "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define FORMAT_STRING2 "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define CONCISE_FORMAT_STRING  "%s!%s!%s!%d!%s!%s!%s!%s!%s!%d!%s!%s!%s\n"
#define VERBOSE_FORMAT_STRING  "%-20.20s %-20.20s %-16.16s %4d %-7.7s %-12.12s %-25.25s %-15.15s %8.8s %-11.11s %-20.20s\n"
#define VERBOSE_FORMAT_STRING2 "%-20.20s %-20.20s %-16.16s %-4.4s %-7.7s %-12.12s %-25.25s %-15.15s %8.8s %-11.11s %-20.20s\n"

	struct ast_channel *c = NULL;
	int numchans = 0, concise = 0, verbose = 0, count = 0;
	int fd, argc;
	char **argv;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show channels [concise|verbose|count]";
		e->usage =
			"Usage: core show channels [concise|verbose|count]\n"
			"       Lists currently defined channels and some information about them. If\n"
			"       'concise' is specified, the format is abridged and in a more easily\n"
			"       machine parsable format. If 'verbose' is specified, the output includes\n"
			"       more and longer fields. If 'count' is specified only the channel and call\n"
			"       count is output.\n"
			"	The 'concise' option is deprecated and will be removed from future versions\n"
			"	of Asterisk.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}
	fd = a->fd;
	argc = a->argc;
	argv = a->argv;

	if (a->argc == e->args) {
		if (!strcasecmp(argv[e->args-1],"concise"))
			concise = 1;
		else if (!strcasecmp(argv[e->args-1],"verbose"))
			verbose = 1;
		else if (!strcasecmp(argv[e->args-1],"count"))
			count = 1;
		else
			return CLI_SHOWUSAGE;
	} else if (a->argc != e->args - 1)
		return CLI_SHOWUSAGE;

	if (!count) {
		if (!concise && !verbose)
			ast_cli(fd, FORMAT_STRING2, "Channel", "Location", "State", "Application(Data)");
		else if (verbose)
			ast_cli(fd, VERBOSE_FORMAT_STRING2, "Channel", "Context", "Extension", "Priority", "State", "Application", "Data", 
				"CallerID", "Duration", "Accountcode", "BridgedTo");
	}

	while ((c = ast_channel_walk_locked(c)) != NULL) {
		struct ast_channel *bc = ast_bridged_channel(c);
		char durbuf[10] = "-";

		if (!count) {
			if ((concise || verbose)  && c->cdr && !ast_tvzero(c->cdr->start)) {
				int duration = (int)(ast_tvdiff_ms(ast_tvnow(), c->cdr->start) / 1000);
				if (verbose) {
					int durh = duration / 3600;
					int durm = (duration % 3600) / 60;
					int durs = duration % 60;
					snprintf(durbuf, sizeof(durbuf), "%02d:%02d:%02d", durh, durm, durs);
				} else {
					snprintf(durbuf, sizeof(durbuf), "%d", duration);
				}				
			}
			if (concise) {
				ast_cli(fd, CONCISE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
					c->appl ? c->appl : "(None)",
					S_OR(c->data, ""),	/* XXX different from verbose ? */
					S_OR(c->cid.cid_num, ""),
					S_OR(c->accountcode, ""),
					c->amaflags, 
					durbuf,
					bc ? bc->name : "(None)",
					c->uniqueid);
			} else if (verbose) {
				ast_cli(fd, VERBOSE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
					c->appl ? c->appl : "(None)",
					c->data ? S_OR(c->data, "(Empty)" ): "(None)",
					S_OR(c->cid.cid_num, ""),
					durbuf,
					S_OR(c->accountcode, ""),
					bc ? bc->name : "(None)");
			} else {
				char locbuf[40] = "(None)";
				char appdata[40] = "(None)";
				
				if (!ast_strlen_zero(c->context) && !ast_strlen_zero(c->exten)) 
					snprintf(locbuf, sizeof(locbuf), "%s@%s:%d", c->exten, c->context, c->priority);
				if (c->appl)
					snprintf(appdata, sizeof(appdata), "%s(%s)", c->appl, S_OR(c->data, ""));
				ast_cli(fd, FORMAT_STRING, c->name, locbuf, ast_state2str(c->_state), appdata);
			}
		}
		numchans++;
		ast_channel_unlock(c);
	}
	if (!concise) {
		ast_cli(fd, "%d active channel%s\n", numchans, ESS(numchans));
		if (option_maxcalls)
			ast_cli(fd, "%d of %d max active call%s (%5.2f%% of capacity)\n",
				ast_active_calls(), option_maxcalls, ESS(ast_active_calls()),
				((double)ast_active_calls() / (double)option_maxcalls) * 100.0);
		else
			ast_cli(fd, "%d active call%s\n", ast_active_calls(), ESS(ast_active_calls()));

		ast_cli(fd, "%d call%s processed\n", ast_processed_calls(), ESS(ast_processed_calls()));
	}
	return CLI_SUCCESS;
	
#undef FORMAT_STRING
#undef FORMAT_STRING2
#undef CONCISE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING2
}

static char *handle_softhangup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c=NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "soft hangup";
		e->usage =
			"Usage: soft hangup <channel>\n"
			"       Request that a channel be hung up. The hangup takes effect\n"
			"       the next time the driver reads or writes from the channel\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}
	if (a->argc != 3)
		return CLI_SHOWUSAGE;
	c = ast_get_channel_by_name_locked(a->argv[2]);
	if (c) {
		ast_cli(a->fd, "Requested Hangup on channel '%s'\n", c->name);
		ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
		ast_channel_unlock(c);
	} else
		ast_cli(a->fd, "%s is not a known channel\n", a->argv[2]);
	return CLI_SUCCESS;
}

static char *__ast_cli_generator(const char *text, const char *word, int state, int lock);

static char *handle_commandmatchesarray(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *buf, *obuf;
	int buflen = 2048;
	int len = 0;
	char **matches;
	int x, matchlen;
	
	switch (cmd) {
	case CLI_INIT:
		e->command = "_command matchesarray";
		e->usage = 
			"Usage: _command matchesarray \"<line>\" text \n"
			"       This function is used internally to help with command completion and should.\n"
			"       never be called by the user directly.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	if (!(buf = ast_malloc(buflen)))
		return CLI_FAILURE;
	buf[len] = '\0';
	matches = ast_cli_completion_matches(a->argv[2], a->argv[3]);
	if (matches) {
		for (x=0; matches[x]; x++) {
			matchlen = strlen(matches[x]) + 1;
			if (len + matchlen >= buflen) {
				buflen += matchlen * 3;
				obuf = buf;
				if (!(buf = ast_realloc(obuf, buflen))) 
					/* Memory allocation failure...  Just free old buffer and be done */
					ast_free(obuf);
			}
			if (buf)
				len += sprintf( buf + len, "%s ", matches[x]);
			ast_free(matches[x]);
			matches[x] = NULL;
		}
		ast_free(matches);
	}

	if (buf) {
		ast_cli(a->fd, "%s%s",buf, AST_CLI_COMPLETE_EOF);
		ast_free(buf);
	} else
		ast_cli(a->fd, "NULL\n");

	return CLI_SUCCESS;
}



static char *handle_commandnummatches(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int matches = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "_command nummatches";
		e->usage = 
			"Usage: _command nummatches \"<line>\" text \n"
			"       This function is used internally to help with command completion and should.\n"
			"       never be called by the user directly.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	matches = ast_cli_generatornummatches(a->argv[2], a->argv[3]);

	ast_cli(a->fd, "%d", matches);

	return CLI_SUCCESS;
}

static char *handle_commandcomplete(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *buf;
	switch (cmd) {
	case CLI_INIT:
		e->command = "_command complete";
		e->usage = 
			"Usage: _command complete \"<line>\" text state\n"
			"       This function is used internally to help with command completion and should.\n"
			"       never be called by the user directly.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 5)
		return CLI_SHOWUSAGE;
	buf = __ast_cli_generator(a->argv[2], a->argv[3], atoi(a->argv[4]), 0);
	if (buf) {
		ast_cli(a->fd, "%s", buf);
		ast_free(buf);
	} else
		ast_cli(a->fd, "NULL\n");
	return CLI_SUCCESS;
}

static char *handle_core_set_debug_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c = NULL;
	int is_all, is_off = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core set debug channel";
		e->usage =
			"Usage: core set debug channel <all|channel> [off]\n"
			"       Enables/disables debugging on all or on a specific channel.\n";
		return NULL;

	case CLI_GENERATE:
		/* XXX remember to handle the optional "off" */
		if (a->pos != e->args)
			return NULL;
		return a->n == 0 ? ast_strdup("all") : ast_complete_channels(a->line, a->word, a->pos, a->n - 1, e->args);
	}
	/* 'core set debug channel {all|chan_id}' */
	if (a->argc == e->args + 2) {
		if (!strcasecmp(a->argv[e->args + 1], "off"))
			is_off = 1;
		else
			return CLI_SHOWUSAGE;
	} else if (a->argc != e->args + 1)
		return CLI_SHOWUSAGE;

	is_all = !strcasecmp("all", a->argv[e->args]);
	if (is_all) {
		if (is_off) {
			global_fin &= ~DEBUGCHAN_FLAG;
			global_fout &= ~DEBUGCHAN_FLAG;
		} else {
			global_fin |= DEBUGCHAN_FLAG;
			global_fout |= DEBUGCHAN_FLAG;
		}
		c = ast_channel_walk_locked(NULL);
	} else {
		c = ast_get_channel_by_name_locked(a->argv[e->args]);
		if (c == NULL)
			ast_cli(a->fd, "No such channel %s\n", a->argv[e->args]);
	}
	while (c) {
		if (!(c->fin & DEBUGCHAN_FLAG) || !(c->fout & DEBUGCHAN_FLAG)) {
			if (is_off) {
				c->fin &= ~DEBUGCHAN_FLAG;
				c->fout &= ~DEBUGCHAN_FLAG;
			} else {
				c->fin |= DEBUGCHAN_FLAG;
				c->fout |= DEBUGCHAN_FLAG;
			}
			ast_cli(a->fd, "Debugging %s on channel %s\n", is_off ? "disabled" : "enabled", c->name);
		}
		ast_channel_unlock(c);
		if (!is_all)
			break;
		c = ast_channel_walk_locked(c);
	}
	ast_cli(a->fd, "Debugging on new channels is %s\n", is_off ? "disabled" : "enabled");
	return CLI_SUCCESS;
}

static char *handle_debugchan_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res;

	if (cmd == CLI_HANDLER && a->argc != e->args + 1)
		return CLI_SHOWUSAGE;
	res = handle_core_set_debug_channel(e, cmd, a);
	if (cmd == CLI_INIT)
		e->command = "debug channel";
	return res;
}

static char *handle_nodebugchan_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res;
	if (cmd == CLI_HANDLER) {
		if (a->argc != e->args + 1)
			return CLI_SHOWUSAGE;
		/* pretend we have an extra "off" at the end. We can do this as the array
		 * is NULL terminated so we overwrite that entry.
		 */
		a->argv[e->args+1] = "off";
		a->argc++;
	}
	res = handle_core_set_debug_channel(e, cmd, a);
	if (cmd == CLI_INIT)
		e->command = "no debug channel";
	return res;
}
		
static char *handle_showchan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c=NULL;
	struct timeval now;
	struct ast_str *out = ast_str_alloca(2048);
	char cdrtime[256];
	char nf[256], wf[256], rf[256];
	long elapsed_seconds=0;
	int hour=0, min=0, sec=0;
#ifdef CHANNEL_TRACE
	int trace_enabled;
#endif

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show channel";
		e->usage = 
			"Usage: core show channel <channel>\n"
			"       Shows lots of information about the specified channel.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
	}
	
	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	now = ast_tvnow();
	c = ast_get_channel_by_name_locked(a->argv[3]);
	if (!c) {
		ast_cli(a->fd, "%s is not a known channel\n", a->argv[3]);
		return CLI_SUCCESS;
	}
	if (c->cdr) {
		elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
		snprintf(cdrtime, sizeof(cdrtime), "%dh%dm%ds", hour, min, sec);
	} else
		strcpy(cdrtime, "N/A");
	ast_cli(a->fd, 
		" -- General --\n"
		"           Name: %s\n"
		"           Type: %s\n"
		"       UniqueID: %s\n"
		"      Caller ID: %s\n"
		" Caller ID Name: %s\n"
		"    DNID Digits: %s\n"
		"       Language: %s\n"
		"          State: %s (%d)\n"
		"          Rings: %d\n"
		"  NativeFormats: %s\n"
		"    WriteFormat: %s\n"
		"     ReadFormat: %s\n"
		" WriteTranscode: %s\n"
		"  ReadTranscode: %s\n"
		"1st File Descriptor: %d\n"
		"      Frames in: %d%s\n"
		"     Frames out: %d%s\n"
		" Time to Hangup: %ld\n"
		"   Elapsed Time: %s\n"
		"  Direct Bridge: %s\n"
		"Indirect Bridge: %s\n"
		" --   PBX   --\n"
		"        Context: %s\n"
		"      Extension: %s\n"
		"       Priority: %d\n"
		"     Call Group: %llu\n"
		"   Pickup Group: %llu\n"
		"    Application: %s\n"
		"           Data: %s\n"
		"    Blocking in: %s\n",
		c->name, c->tech->type, c->uniqueid,
		S_OR(c->cid.cid_num, "(N/A)"),
		S_OR(c->cid.cid_name, "(N/A)"),
		S_OR(c->cid.cid_dnid, "(N/A)"), 
		c->language,	
		ast_state2str(c->_state), c->_state, c->rings, 
		ast_getformatname_multiple(nf, sizeof(nf), c->nativeformats), 
		ast_getformatname_multiple(wf, sizeof(wf), c->writeformat), 
		ast_getformatname_multiple(rf, sizeof(rf), c->readformat),
		c->writetrans ? "Yes" : "No",
		c->readtrans ? "Yes" : "No",
		c->fds[0],
		c->fin & ~DEBUGCHAN_FLAG, (c->fin & DEBUGCHAN_FLAG) ? " (DEBUGGED)" : "",
		c->fout & ~DEBUGCHAN_FLAG, (c->fout & DEBUGCHAN_FLAG) ? " (DEBUGGED)" : "",
		(long)c->whentohangup.tv_sec,
		cdrtime, c->_bridge ? c->_bridge->name : "<none>", ast_bridged_channel(c) ? ast_bridged_channel(c)->name : "<none>", 
		c->context, c->exten, c->priority, c->callgroup, c->pickupgroup, ( c->appl ? c->appl : "(N/A)" ),
		( c-> data ? S_OR(c->data, "(Empty)") : "(None)"),
		(ast_test_flag(c, AST_FLAG_BLOCKING) ? c->blockproc : "(Not Blocking)"));
	
	if (pbx_builtin_serialize_variables(c, &out))
		ast_cli(a->fd,"      Variables:\n%s\n", out->str);
	if (c->cdr && ast_cdr_serialize_variables(c->cdr, &out, '=', '\n', 1))
		ast_cli(a->fd,"  CDR Variables:\n%s\n", out->str);
#ifdef CHANNEL_TRACE
	trace_enabled = ast_channel_trace_is_enabled(c);
	ast_cli(a->fd, "  Context Trace: %s\n", trace_enabled ? "Enabled" : "Disabled");
	if (trace_enabled && ast_channel_trace_serialize(c, &out))
		ast_cli(a->fd, "          Trace:\n%s\n", out->str);
#endif
	ast_channel_unlock(c);
	return CLI_SUCCESS;
}

/*
 * helper function to generate CLI matches from a fixed set of values.
 * A NULL word is acceptable.
 */
char *ast_cli_complete(const char *word, char *const choices[], int state)
{
	int i, which = 0, len;
	len = ast_strlen_zero(word) ? 0 : strlen(word);

	for (i = 0; choices[i]; i++) {
		if ((!len || !strncasecmp(word, choices[i], len)) && ++which > state)
			return ast_strdup(choices[i]);
	}
	return NULL;
}

char *ast_complete_channels(const char *line, const char *word, int pos, int state, int rpos)
{
	struct ast_channel *c = NULL;
	int which = 0;
	int wordlen;
	char notfound = '\0';
	char *ret = &notfound; /* so NULL can break the loop */

	if (pos != rpos)
		return NULL;

	wordlen = strlen(word);	

	while (ret == &notfound && (c = ast_channel_walk_locked(c))) {
		if (!strncasecmp(word, c->name, wordlen) && ++which > state)
			ret = ast_strdup(c->name);
		ast_channel_unlock(c);
	}
	return ret == &notfound ? NULL : ret;
}

static char *group_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_STRING  "%-25s  %-20s  %-20s\n"

	struct ast_group_info *gi = NULL;
	int numchans = 0;
	regex_t regexbuf;
	int havepattern = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "group show channels";
		e->usage = 
			"Usage: group show channels [pattern]\n"
			"       Lists all currently active channels with channel group(s) specified.\n"
			"       Optional regular expression pattern is matched to group names for each\n"
			"       channel.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 3 || a->argc > 4)
		return CLI_SHOWUSAGE;
	
	if (a->argc == 4) {
		if (regcomp(&regexbuf, a->argv[3], REG_EXTENDED | REG_NOSUB))
			return CLI_SHOWUSAGE;
		havepattern = 1;
	}

	ast_cli(a->fd, FORMAT_STRING, "Channel", "Group", "Category");

	ast_app_group_list_rdlock();
	
	gi = ast_app_group_list_head();
	while (gi) {
		if (!havepattern || !regexec(&regexbuf, gi->group, 0, NULL, 0)) {
			ast_cli(a->fd, FORMAT_STRING, gi->chan->name, gi->group, (ast_strlen_zero(gi->category) ? "(default)" : gi->category));
			numchans++;
		}
		gi = AST_LIST_NEXT(gi, list);
	}
	
	ast_app_group_list_unlock();
	
	if (havepattern)
		regfree(&regexbuf);

	ast_cli(a->fd, "%d active channel%s\n", numchans, ESS(numchans));
	return CLI_SUCCESS;
#undef FORMAT_STRING
}

static struct ast_cli_entry cli_debug_channel_deprecated = AST_CLI_DEFINE(handle_debugchan_deprecated, "Enable debugging on channel");
static struct ast_cli_entry cli_module_load_deprecated = AST_CLI_DEFINE(handle_load_deprecated, "Load a module");
static struct ast_cli_entry cli_module_reload_deprecated = AST_CLI_DEFINE(handle_reload_deprecated, "reload modules by name");
static struct ast_cli_entry cli_module_unload_deprecated = AST_CLI_DEFINE(handle_unload_deprecated, "unload modules by name");

static char *handle_help(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry cli_cli[] = {
	/* Deprecated, but preferred command is now consolidated (and already has a deprecated command for it). */
	AST_CLI_DEFINE(handle_commandcomplete, "Command complete"),
	AST_CLI_DEFINE(handle_commandnummatches, "Returns number of command matches"),
	AST_CLI_DEFINE(handle_commandmatchesarray, "Returns command matches array"),

	AST_CLI_DEFINE(handle_nodebugchan_deprecated, "Disable debugging on channel(s)"),

	AST_CLI_DEFINE(handle_chanlist, "Display information on channels"),

	AST_CLI_DEFINE(handle_showcalls, "Display information on calls"),

	AST_CLI_DEFINE(handle_showchan, "Display information on a specific channel"),

	AST_CLI_DEFINE(handle_core_set_debug_channel, "Enable/disable debugging on a channel",
		.deprecate_cmd = &cli_debug_channel_deprecated),

	AST_CLI_DEFINE(handle_verbose, "Set level of debug/verbose chattiness"),

	AST_CLI_DEFINE(group_show_channels, "Display active channels with group(s)"),

	AST_CLI_DEFINE(handle_help, "Display help list, or specific help on a command"),

	AST_CLI_DEFINE(handle_logger_mute, "Toggle logging output to a console"),

	AST_CLI_DEFINE(handle_modlist, "List modules and info"),

	AST_CLI_DEFINE(handle_load, "Load a module by name", .deprecate_cmd = &cli_module_load_deprecated),

	AST_CLI_DEFINE(handle_reload, "Reload configuration", .deprecate_cmd = &cli_module_reload_deprecated),

	AST_CLI_DEFINE(handle_unload, "Unload a module by name", .deprecate_cmd = &cli_module_unload_deprecated ),

	AST_CLI_DEFINE(handle_showuptime, "Show uptime information"),

	AST_CLI_DEFINE(handle_softhangup, "Request a hangup on a given channel"),
};

/*!
 * Some regexp characters in cli arguments are reserved and used as separators.
 */
static const char cli_rsvd[] = "[]{}|*%";

/*!
 * initialize the _full_cmd string and related parameters,
 * return 0 on success, -1 on error.
 */
static int set_full_cmd(struct ast_cli_entry *e)
{
	int i;
	char buf[80];

	ast_join(buf, sizeof(buf), e->cmda);
	e->_full_cmd = ast_strdup(buf);
	if (!e->_full_cmd) {
		ast_log(LOG_WARNING, "-- cannot allocate <%s>\n", buf);
		return -1;
	}
	e->cmdlen = strcspn(e->_full_cmd, cli_rsvd);
	for (i = 0; e->cmda[i]; i++)
		;
	e->args = i;
	return 0;
}

/*! \brief initialize the _full_cmd string in * each of the builtins. */
void ast_builtins_init(void)
{
	ast_cli_register_multiple(cli_cli, sizeof(cli_cli) / sizeof(struct ast_cli_entry));
}

static struct ast_cli_entry *cli_next(struct ast_cli_entry *e)
{
	if (e == NULL)
		e = AST_LIST_FIRST(&helpers);
	if (e) 
		e = AST_LIST_NEXT(e, list);
	return e;
}

/*!
 * match a word in the CLI entry.
 * returns -1 on mismatch, 0 on match of an optional word,
 * 1 on match of a full word.
 *
 * The pattern can be
 *   any_word		match for equal
 *   [foo|bar|baz]	optionally, one of these words
 *   {foo|bar|baz}	exactly, one of these words
 *   %			any word
 */
static int word_match(const char *cmd, const char *cli_word)
{
	int l;
	char *pos;

	if (ast_strlen_zero(cmd) || ast_strlen_zero(cli_word))
		return -1;
	if (!strchr(cli_rsvd, cli_word[0])) /* normal match */
		return (strcasecmp(cmd, cli_word) == 0) ? 1 : -1;
	/* regexp match, takes [foo|bar] or {foo|bar} */
	l = strlen(cmd);
	/* wildcard match - will extend in the future */
	if (l > 0 && cli_word[0] == '%') {
		return 1;	/* wildcard */
	}
	pos = strcasestr(cli_word, cmd);
	if (pos == NULL) /* not found, say ok if optional */
		return cli_word[0] == '[' ? 0 : -1;
	if (pos == cli_word)	/* no valid match at the beginning */
		return -1;
	if (strchr(cli_rsvd, pos[-1]) && strchr(cli_rsvd, pos[l]))
		return 1;	/* valid match */
	return -1;	/* not found */
}

/*! \brief if word is a valid prefix for token, returns the pos-th
 * match as a malloced string, or NULL otherwise.
 * Always tell in *actual how many matches we got.
 */
static char *is_prefix(const char *word, const char *token,
	int pos, int *actual)
{
	int lw;
	char *s, *t1;

	*actual = 0;
	if (ast_strlen_zero(token))
		return NULL;
	if (ast_strlen_zero(word))
		word = "";	/* dummy */
	lw = strlen(word);
	if (strcspn(word, cli_rsvd) != lw)
		return NULL;	/* no match if word has reserved chars */
	if (strchr(cli_rsvd, token[0]) == NULL) {	/* regular match */
		if (strncasecmp(token, word, lw))	/* no match */
			return NULL;
		*actual = 1;
		return (pos != 0) ? NULL : ast_strdup(token);
	}
	/* now handle regexp match */

	/* Wildcard always matches, so we never do is_prefix on them */

	t1 = ast_strdupa(token + 1);	/* copy, skipping first char */
	while (pos >= 0 && (s = strsep(&t1, cli_rsvd)) && *s) {
		if (*s == '%')	/* wildcard */
			continue;
		if (strncasecmp(s, word, lw))	/* no match */
			continue;
		(*actual)++;
		if (pos-- == 0)
			return ast_strdup(s);
	}
	return NULL;
}

/*!
 * \brief locate a cli command in the 'helpers' list (which must be locked).
 * exact has 3 values:
 *      0       returns if the search key is equal or longer than the entry.
 *		note that trailing optional arguments are skipped.
 *      -1      true if the mismatch is on the last word XXX not true!
 *      1       true only on complete, exact match.
 *
 * The search compares word by word taking care of regexps in e->cmda
 */
static struct ast_cli_entry *find_cli(char *const cmds[], int match_type)
{
	int matchlen = -1;	/* length of longest match so far */
	struct ast_cli_entry *cand = NULL, *e=NULL;

	while ( (e = cli_next(e)) ) {
		/* word-by word regexp comparison */
		char * const *src = cmds;
		char * const *dst = e->cmda;
		int n = 0;
		for (;; dst++, src += n) {
			n = word_match(*src, *dst);
			if (n < 0)
				break;
		}
		if (ast_strlen_zero(*dst) || ((*dst)[0] == '[' && ast_strlen_zero(dst[1]))) {
			/* no more words in 'e' */
			if (ast_strlen_zero(*src))	/* exact match, cannot do better */
				break;
			/* Here, cmds has more words than the entry 'e' */
			if (match_type != 0)	/* but we look for almost exact match... */
				continue;	/* so we skip this one. */
			/* otherwise we like it (case 0) */
		} else {	/* still words in 'e' */
			if (ast_strlen_zero(*src))
				continue; /* cmds is shorter than 'e', not good */
			/* Here we have leftover words in cmds and 'e',
			 * but there is a mismatch. We only accept this one if match_type == -1
			 * and this is the last word for both.
			 */
			if (match_type != -1 || !ast_strlen_zero(src[1]) ||
			    !ast_strlen_zero(dst[1]))	/* not the one we look for */
				continue;
			/* good, we are in case match_type == -1 and mismatch on last word */
		}
		if (src - cmds > matchlen) {	/* remember the candidate */
			matchlen = src - cmds;
			cand = e;
		}
	}
	return e ? e : cand;
}

static char *find_best(char *argv[])
{
	static char cmdline[80];
	int x;
	/* See how close we get, then print the candidate */
	char *myargv[AST_MAX_CMD_LEN];
	for (x=0;x<AST_MAX_CMD_LEN;x++)
		myargv[x]=NULL;
	AST_RWLIST_RDLOCK(&helpers);
	for (x=0;argv[x];x++) {
		myargv[x] = argv[x];
		if (!find_cli(myargv, -1))
			break;
	}
	AST_RWLIST_UNLOCK(&helpers);
	ast_join(cmdline, sizeof(cmdline), myargv);
	return cmdline;
}

static int __ast_cli_unregister(struct ast_cli_entry *e, struct ast_cli_entry *ed)
{
	if (e->deprecate_cmd) {
		__ast_cli_unregister(e->deprecate_cmd, e);
	}
	if (e->inuse) {
		ast_log(LOG_WARNING, "Can't remove command that is in use\n");
	} else {
		AST_RWLIST_WRLOCK(&helpers);
		AST_RWLIST_REMOVE(&helpers, e, list);
		AST_RWLIST_UNLOCK(&helpers);
		ast_free(e->_full_cmd);
		e->_full_cmd = NULL;
		if (e->handler) {
			/* this is a new-style entry. Reset fields and free memory. */
			bzero((char **)(e->cmda), sizeof(e->cmda));
			ast_free(e->command);
			e->command = NULL;
			e->usage = NULL;
		}
	}
	return 0;
}

static int __ast_cli_register(struct ast_cli_entry *e, struct ast_cli_entry *ed)
{
	struct ast_cli_entry *cur;
	int i, lf, ret = -1;

	struct ast_cli_args a;	/* fake argument */
	char **dst = (char **)e->cmda;	/* need to cast as the entry is readonly */
	char *s;

	bzero (&a, sizeof(a));
	e->handler(e, CLI_INIT, &a);
	/* XXX check that usage and command are filled up */
	s = ast_skip_blanks(e->command);
	s = e->command = ast_strdup(s);
	for (i=0; !ast_strlen_zero(s) && i < AST_MAX_CMD_LEN-1; i++) {
		*dst++ = s;	/* store string */
		s = ast_skip_nonblanks(s);
		if (*s == '\0')	/* we are done */
			break;
		*s++ = '\0';
		s = ast_skip_blanks(s);
	}
	*dst++ = NULL;
	
	AST_RWLIST_WRLOCK(&helpers);
	
	if (find_cli(e->cmda, 1)) {
		ast_log(LOG_WARNING, "Command '%s' already registered (or something close enough)\n", e->_full_cmd);
		goto done;
	}
	if (set_full_cmd(e))
		goto done;
	if (!ed) {
		e->deprecated = 0;
	} else {
		e->deprecated = 1;
		e->summary = ed->summary;
		e->usage = ed->usage;
		/* XXX If command A deprecates command B, and command B deprecates command C...
		   Do we want to show command A or command B when telling the user to use new syntax?
		   This currently would show command A.
		   To show command B, you just need to always use ed->_full_cmd.
		 */
		e->_deprecated_by = S_OR(ed->_deprecated_by, ed->_full_cmd);
	}

	lf = e->cmdlen;
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&helpers, cur, list) {
		int len = cur->cmdlen;
		if (lf < len)
			len = lf;
		if (strncasecmp(e->_full_cmd, cur->_full_cmd, len) < 0) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(e, list); 
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!cur)
		AST_RWLIST_INSERT_TAIL(&helpers, e, list); 
	ret = 0;	/* success */

done:
	AST_RWLIST_UNLOCK(&helpers);

	if (e->deprecate_cmd) {
		/* This command deprecates another command.  Register that one also. */
		__ast_cli_register(e->deprecate_cmd, e);
	}
	
	return ret;
}

/* wrapper function, so we can unregister deprecated commands recursively */
int ast_cli_unregister(struct ast_cli_entry *e)
{
	return __ast_cli_unregister(e, NULL);
}

/* wrapper function, so we can register deprecated commands recursively */
int ast_cli_register(struct ast_cli_entry *e)
{
	return __ast_cli_register(e, NULL);
}

/*
 * register/unregister an array of entries.
 */
int ast_cli_register_multiple(struct ast_cli_entry *e, int len)
{
	int i, res = 0;

	for (i = 0; i < len; i++)
		res |= ast_cli_register(e + i);

	return res;
}

int ast_cli_unregister_multiple(struct ast_cli_entry *e, int len)
{
	int i, res = 0;

	for (i = 0; i < len; i++)
		res |= ast_cli_unregister(e + i);

	return res;
}


/*! \brief helper for final part of handle_help
 *  if locked = 1, assume the list is already locked
 */
static char *help1(int fd, char *match[], int locked)
{
	char matchstr[80] = "";
	struct ast_cli_entry *e = NULL;
	int len = 0;
	int found = 0;

	if (match) {
		ast_join(matchstr, sizeof(matchstr), match);
		len = strlen(matchstr);
	}
	if (!locked)
		AST_RWLIST_RDLOCK(&helpers);
	while ( (e = cli_next(e)) ) {
		/* Hide commands that start with '_' */
		if (e->_full_cmd[0] == '_')
			continue;
		/* Hide commands that are marked as deprecated. */
		if (e->deprecated)
			continue;
		if (match && strncasecmp(matchstr, e->_full_cmd, len))
			continue;
		ast_cli(fd, "%30.30s %s\n", e->_full_cmd, S_OR(e->summary, "<no description available>"));
		found++;
	}
	if (!locked)
		AST_RWLIST_UNLOCK(&helpers);
	if (!found && matchstr[0])
		ast_cli(fd, "No such command '%s'.\n", matchstr);
	return CLI_SUCCESS;
}

static char *handle_help(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char fullcmd[80];
	struct ast_cli_entry *my_e;
	char *res = CLI_SUCCESS;

	if (cmd == CLI_INIT) {
		e->command = "help";
		e->usage =
			"Usage: help [topic]\n"
			"       When called with a topic as an argument, displays usage\n"
			"       information on the given command. If called without a\n"
			"       topic, it provides a list of commands.\n";
		return NULL;

	} else if (cmd == CLI_GENERATE) {
		/* skip first 4 or 5 chars, "help " */
		int l = strlen(a->line);

		if (l > 5)
			l = 5;
		/* XXX watch out, should stop to the non-generator parts */
		return __ast_cli_generator(a->line + l, a->word, a->n, 0);
	}
	if (a->argc == 1)
		return help1(a->fd, NULL, 0);

	AST_RWLIST_RDLOCK(&helpers);
	my_e = find_cli(a->argv + 1, 1);	/* try exact match first */
	if (!my_e) {
		res = help1(a->fd, a->argv + 1, 1 /* locked */);
		AST_RWLIST_UNLOCK(&helpers);
		return res;
	}
	if (my_e->usage)
		ast_cli(a->fd, "%s", my_e->usage);
	else {
		ast_join(fullcmd, sizeof(fullcmd), a->argv+1);
		ast_cli(a->fd, "No help text available for '%s'.\n", fullcmd);
	}
	AST_RWLIST_UNLOCK(&helpers);
	return res;
}

static char *parse_args(const char *s, int *argc, char *argv[], int max, int *trailingwhitespace)
{
	char *dup, *cur;
	int x = 0;
	int quoted = 0;
	int escaped = 0;
	int whitespace = 1;
	int dummy = 0;

	if (trailingwhitespace == NULL)
		trailingwhitespace = &dummy;
	*trailingwhitespace = 0;
	if (s == NULL)	/* invalid, though! */
		return NULL;
	/* make a copy to store the parsed string */
	if (!(dup = ast_strdup(s)))
		return NULL;

	cur = dup;
	/* scan the original string copying into cur when needed */
	for (; *s ; s++) {
		if (x >= max - 1) {
			ast_log(LOG_WARNING, "Too many arguments, truncating at %s\n", s);
			break;
		}
		if (*s == '"' && !escaped) {
			quoted = !quoted;
			if (quoted && whitespace) {
				/* start a quoted string from previous whitespace: new argument */
				argv[x++] = cur;
				whitespace = 0;
			}
		} else if ((*s == ' ' || *s == '\t') && !(quoted || escaped)) {
			/* If we are not already in whitespace, and not in a quoted string or
			   processing an escape sequence, and just entered whitespace, then
			   finalize the previous argument and remember that we are in whitespace
			*/
			if (!whitespace) {
				*cur++ = '\0';
				whitespace = 1;
			}
		} else if (*s == '\\' && !escaped) {
			escaped = 1;
		} else {
			if (whitespace) {
				/* we leave whitespace, and are not quoted. So it's a new argument */
				argv[x++] = cur;
				whitespace = 0;
			}
			*cur++ = *s;
			escaped = 0;
		}
	}
	/* Null terminate */
	*cur++ = '\0';
	/* XXX put a NULL in the last argument, because some functions that take
	 * the array may want a null-terminated array.
	 * argc still reflects the number of non-NULL entries.
	 */
	argv[x] = NULL;
	*argc = x;
	*trailingwhitespace = whitespace;
	return dup;
}

/*! \brief Return the number of unique matches for the generator */
int ast_cli_generatornummatches(const char *text, const char *word)
{
	int matches = 0, i = 0;
	char *buf = NULL, *oldbuf = NULL;

	while ((buf = ast_cli_generator(text, word, i++))) {
		if (!oldbuf || strcmp(buf,oldbuf))
			matches++;
		if (oldbuf)
			ast_free(oldbuf);
		oldbuf = buf;
	}
	if (oldbuf)
		ast_free(oldbuf);
	return matches;
}

char **ast_cli_completion_matches(const char *text, const char *word)
{
	char **match_list = NULL, *retstr, *prevstr;
	size_t match_list_len, max_equal, which, i;
	int matches = 0;

	/* leave entry 0 free for the longest common substring */
	match_list_len = 1;
	while ((retstr = ast_cli_generator(text, word, matches)) != NULL) {
		if (matches + 1 >= match_list_len) {
			match_list_len <<= 1;
			if (!(match_list = ast_realloc(match_list, match_list_len * sizeof(*match_list))))
				return NULL;
		}
		match_list[++matches] = retstr;
	}

	if (!match_list)
		return match_list; /* NULL */

	/* Find the longest substring that is common to all results
	 * (it is a candidate for completion), and store a copy in entry 0.
	 */
	prevstr = match_list[1];
	max_equal = strlen(prevstr);
	for (which = 2; which <= matches; which++) {
		for (i = 0; i < max_equal && toupper(prevstr[i]) == toupper(match_list[which][i]); i++)
			continue;
		max_equal = i;
	}

	if (!(retstr = ast_malloc(max_equal + 1)))
		return NULL;
	
	ast_copy_string(retstr, match_list[1], max_equal + 1);
	match_list[0] = retstr;

	/* ensure that the array is NULL terminated */
	if (matches + 1 >= match_list_len) {
		if (!(match_list = ast_realloc(match_list, (match_list_len + 1) * sizeof(*match_list))))
			return NULL;
	}
	match_list[matches + 1] = NULL;

	return match_list;
}

/*! \brief returns true if there are more words to match */
static int more_words (char * const *dst)
{
	int i;
	for (i = 0; dst[i]; i++) {
		if (dst[i][0] != '[')
			return -1;
	}
	return 0;
}
	
/*
 * generate the entry at position 'state'
 */
static char *__ast_cli_generator(const char *text, const char *word, int state, int lock)
{
	char *argv[AST_MAX_ARGS];
	struct ast_cli_entry *e = NULL;
	int x = 0, argindex, matchlen;
	int matchnum=0;
	char *ret = NULL;
	char matchstr[80] = "";
	int tws = 0;
	/* Split the argument into an array of words */
	char *dup = parse_args(text, &x, argv, sizeof(argv) / sizeof(argv[0]), &tws);

	if (!dup)	/* malloc error */
		return NULL;

	/* Compute the index of the last argument (could be an empty string) */
	argindex = (!ast_strlen_zero(word) && x>0) ? x-1 : x;

	/* rebuild the command, ignore terminating white space and flatten space */
	ast_join(matchstr, sizeof(matchstr)-1, argv);
	matchlen = strlen(matchstr);
	if (tws) {
		strcat(matchstr, " "); /* XXX */
		if (matchlen)
			matchlen++;
	}
	if (lock)
		AST_RWLIST_RDLOCK(&helpers);
	while ( (e = cli_next(e)) ) {
		/* XXX repeated code */
		int src = 0, dst = 0, n = 0;

		if (e->command[0] == '_')
			continue;

		/*
		 * Try to match words, up to and excluding the last word, which
		 * is either a blank or something that we want to extend.
		 */
		for (;src < argindex; dst++, src += n) {
			n = word_match(argv[src], e->cmda[dst]);
			if (n < 0)
				break;
		}

		if (src != argindex && more_words(e->cmda + dst))	/* not a match */
			continue;
		ret = is_prefix(argv[src], e->cmda[dst], state - matchnum, &n);
		matchnum += n;	/* this many matches here */
		if (ret) {
			/*
			 * argv[src] is a valid prefix of the next word in this
			 * command. If this is also the correct entry, return it.
			 */
			if (matchnum > state)
				break;
			ast_free(ret);
			ret = NULL;
		} else if (ast_strlen_zero(e->cmda[dst])) {
			/*
			 * This entry is a prefix of the command string entered
			 * (only one entry in the list should have this property).
			 * Run the generator if one is available. In any case we are done.
			 */
			if (e->handler) {	/* new style command */
				struct ast_cli_args a = {
					.line = matchstr, .word = word,
					.pos = argindex,
					.n = state - matchnum };
				ret = e->handler(e, CLI_GENERATE, &a);
			}
			if (ret)
				break;
		}
	}
	if (lock)
		AST_RWLIST_UNLOCK(&helpers);
	ast_free(dup);
	return ret;
}

char *ast_cli_generator(const char *text, const char *word, int state)
{
	return __ast_cli_generator(text, word, state, 1);
}

int ast_cli_command(int fd, const char *s)
{
	char *args[AST_MAX_ARGS + 1];
	struct ast_cli_entry *e;
	int x;
	char *dup = parse_args(s, &x, args + 1, AST_MAX_ARGS, NULL);
	char *retval = NULL;
	struct ast_cli_args a = {
		.fd = fd, .argc = x, .argv = args+1 };

	if (dup == NULL)
		return -1;

	if (x < 1)	/* We need at least one entry, otherwise ignore */
		goto done;

	AST_RWLIST_RDLOCK(&helpers);
	e = find_cli(args + 1, 0);
	if (e)
		ast_atomic_fetchadd_int(&e->inuse, 1);
	AST_RWLIST_UNLOCK(&helpers);
	if (e == NULL) {
		ast_cli(fd, "No such command '%s' (type 'help %s' for other possible commands)\n", s, find_best(args + 1));
		goto done;
	}
	/*
	 * Within the handler, argv[-1] contains a pointer to the ast_cli_entry.
	 * Remember that the array returned by parse_args is NULL-terminated.
	 */
	args[0] = (char *)e;

	retval = e->handler(e, CLI_HANDLER, &a);

	if (retval == CLI_SHOWUSAGE) {
		ast_cli(fd, "%s", S_OR(e->usage, "Invalid usage, but no usage information available.\n"));
		AST_RWLIST_RDLOCK(&helpers);
		if (e->deprecated)
			ast_cli(fd, "The '%s' command is deprecated and will be removed in a future release. Please use '%s' instead.\n", e->_full_cmd, e->_deprecated_by);
		AST_RWLIST_UNLOCK(&helpers);
	} else {
		if (retval == CLI_FAILURE)
			ast_cli(fd, "Command '%s' failed.\n", s);
		AST_RWLIST_RDLOCK(&helpers);
		if (e->deprecated == 1) {
			ast_cli(fd, "The '%s' command is deprecated and will be removed in a future release. Please use '%s' instead.\n", e->_full_cmd, e->_deprecated_by);
			e->deprecated = 2;
		}
		AST_RWLIST_UNLOCK(&helpers);
	}
	ast_atomic_fetchadd_int(&e->inuse, -1);
done:
	ast_free(dup);
	return 0;
}

int ast_cli_command_multiple(int fd, size_t size, const char *s)
{
	char cmd[512];
	int x, y = 0, count = 0;

	for (x = 0; x < size; x++) {
		cmd[y] = s[x];
		y++;
		if (s[x] == '\0') {
			ast_cli_command(fd, cmd);
			y = 0;
			count++;
		}
	}
	return count;
}
