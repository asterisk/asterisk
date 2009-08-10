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
#include <pwd.h>
#include <grp.h>

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
 * \brief List of restrictions per user.
 */
struct cli_perm {
	unsigned int permit:1;				/*!< 1=Permit 0=Deny */
	char *command;				/*!< Command name (to apply restrictions) */
	AST_LIST_ENTRY(cli_perm) list;
};

AST_LIST_HEAD_NOLOCK(cli_perm_head, cli_perm);

/*! \brief list of users to apply restrictions. */
struct usergroup_cli_perm {
	int uid;				/*!< User ID (-1 disabled) */
	int gid;				/*!< Group ID (-1 disabled) */
	struct cli_perm_head *perms;		/*!< List of permissions. */
	AST_LIST_ENTRY(usergroup_cli_perm) list;/*!< List mechanics */
};
/*! \brief CLI permissions config file. */
static const char perms_config[] = "cli_permissions.conf";
/*! \brief Default permissions value 1=Permit 0=Deny */
static int cli_default_perm = 1;

/*! \brief mutex used to prevent a user from running the 'cli reload permissions' command while
 * it is already running. */
AST_MUTEX_DEFINE_STATIC(permsconfiglock);
/*! \brief  List of users and permissions. */
static AST_RWLIST_HEAD_STATIC(cli_perms, usergroup_cli_perm);

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

	if (res != AST_DYNSTR_BUILD_FAILED) {
		ast_carefulwrite(fd, ast_str_buffer(buf), ast_str_strlen(buf), 100);
	}
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

/*! \internal
 *  \brief Check if the user with 'uid' and 'gid' is allow to execute 'command',
 *	   if command starts with '_' then not check permissions, just permit
 *	   to run the 'command'.
 *	   if uid == -1 or gid == -1 do not check permissions.
 *	   if uid == -2 and gid == -2 is because rasterisk client didn't send
 *	   the credentials, so the cli_default_perm will be applied.
 *  \param uid User ID.
 *  \param gid Group ID.
 *  \param command Command name to check permissions.
 *  \retval 1 if has permission
 *  \retval 0 if it is not allowed.
 */
static int cli_has_permissions(int uid, int gid, const char *command)
{
	struct usergroup_cli_perm *user_perm;
	struct cli_perm *perm;
	/* set to the default permissions general option. */
	int isallowg = cli_default_perm, isallowu = -1, ispattern;
	regex_t regexbuf;

	/* if uid == -1 or gid == -1 do not check permissions.
	   if uid == -2 and gid == -2 is because rasterisk client didn't send
	   the credentials, so the cli_default_perm will be applied. */
	if ((uid == CLI_NO_PERMS && gid == CLI_NO_PERMS) || command[0] == '_') {
		return 1;
	}

	if (gid < 0 && uid < 0) {
		return cli_default_perm;
	}

	AST_RWLIST_RDLOCK(&cli_perms);
	AST_LIST_TRAVERSE(&cli_perms, user_perm, list) {
		if (user_perm->gid != gid && user_perm->uid != uid) {
			continue;
		}
		AST_LIST_TRAVERSE(user_perm->perms, perm, list) {
			if (strcasecmp(perm->command, "all") && strncasecmp(perm->command, command, strlen(perm->command))) {
				/* if the perm->command is a pattern, check it against command. */
				ispattern = !regcomp(&regexbuf, perm->command, REG_EXTENDED | REG_NOSUB | REG_ICASE);
				if (ispattern && regexec(&regexbuf, command, 0, NULL, 0)) {
					regfree(&regexbuf);
					continue;
				}
				if (!ispattern) {
					continue;
				}
				regfree(&regexbuf);
			}
			if (user_perm->uid == uid) {
				/* this is a user definition. */
				isallowu = perm->permit;
			} else {
				/* otherwise is a group definition. */
				isallowg = perm->permit;
			}
		}
	}
	AST_RWLIST_UNLOCK(&cli_perms);
	if (isallowu > -1) {
		/* user definition override group definition. */
		isallowg = isallowu;
	}

	return isallowg;
}

static AST_RWLIST_HEAD_STATIC(helpers, ast_cli_entry);

static char *complete_fn(const char *word, int state)
{
	char *c, *d;
	char filename[PATH_MAX];

	if (word[0] == '/')
		ast_copy_string(filename, word, sizeof(filename));
	else
		snprintf(filename, sizeof(filename), "%s/%s", ast_config_AST_MODULE_DIR, word);

	c = d = filename_completion_function(filename, state);
	
	if (c && word[0] != '/')
		c += (strlen(ast_config_AST_MODULE_DIR) + 1);
	if (c)
		c = ast_strdup(c);

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

static char *complete_number(const char *partial, unsigned int min, unsigned int max, int n)
{
	int i, count = 0;
	unsigned int prospective[2];
	unsigned int part = strtoul(partial, NULL, 10);
	char next[12];

	if (part < min || part > max) {
		return NULL;
	}

	for (i = 0; i < 21; i++) {
		if (i == 0) {
			prospective[0] = prospective[1] = part;
		} else if (part == 0 && !ast_strlen_zero(partial)) {
			break;
		} else if (i < 11) {
			prospective[0] = prospective[1] = part * 10 + (i - 1);
		} else {
			prospective[0] = (part * 10 + (i - 11)) * 10;
			prospective[1] = prospective[0] + 9;
		}
		if (i < 11 && (prospective[0] < min || prospective[0] > max)) {
			continue;
		} else if (prospective[1] < min || prospective[0] > max) {
			continue;
		}

		if (++count > n) {
			if (i < 11) {
				snprintf(next, sizeof(next), "%u", prospective[0]);
			} else {
				snprintf(next, sizeof(next), "%u...", prospective[0] / 10);
			}
			return ast_strdup(next);
		}
	}
	return NULL;
}

static char *handle_verbose(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval;
	int newlevel;
	int atleast = 0;
	int fd = a->fd;
	int argc = a->argc;
	const char * const *argv = a->argv;
	const char *argv3 = a->argv ? S_OR(a->argv[3], "") : "";
	int *dst;
	char *what;
	struct debug_file_list *dfl;
	struct ast_debug_file *adf;
	const char *fn;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core set {debug|verbose}";
		e->usage =
			"Usage: core set {debug|verbose} [atleast] <level> [filename]\n"
			"       core set {debug|verbose} off\n"
			"       Sets level of debug or verbose messages to be displayed or \n"
			"       sets a filename to display debug messages from.\n"
			"	0 or off means no messages should be displayed.\n"
			"	Equivalent to -d[d[...]] or -v[v[v...]] on startup\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos == 3 || (a->pos == 4 && !strcasecmp(a->argv[3], "atleast"))) {
			const char *pos = a->pos == 3 ? argv3 : S_OR(a->argv[4], "");
			int numbermatch = (ast_strlen_zero(pos) || strchr("123456789", pos[0])) ? 0 : 21;
			if (a->n < 21 && numbermatch == 0) {
				return complete_number(pos, 0, 0x7fffffff, a->n);
			} else if (pos[0] == '0') {
				if (a->n == 0) {
					return ast_strdup("0");
				} else {
					return NULL;
				}
			} else if (a->n == (21 - numbermatch)) {
				if (a->pos == 3 && !strncasecmp(argv3, "off", strlen(argv3))) {
					return ast_strdup("off");
				} else if (a->pos == 3 && !strncasecmp(argv3, "atleast", strlen(argv3))) {
					return ast_strdup("atleast");
				}
			} else if (a->n == (22 - numbermatch) && a->pos == 3 && ast_strlen_zero(argv3)) {
				return ast_strdup("atleast");
			}
		} else if (a->pos == 4 || (a->pos == 5 && !strcasecmp(argv3, "atleast"))) {
			return ast_complete_source_filename(a->pos == 4 ? S_OR(a->argv[4], "") : S_OR(a->argv[5], ""), a->n);
		}
		return NULL;
	}
	/* all the above return, so we proceed with the handler.
	 * we are guaranteed to be called with argc >= e->args;
	 */

	if (argc <= e->args)
		return CLI_SHOWUSAGE;
	if (!strcasecmp(argv[e->args - 1], "debug")) {
		dst = &option_debug;
		oldval = option_debug;
		what = "Core debug";
	} else {
		dst = &option_verbose;
		oldval = option_verbose;
		what = "Verbosity";
	}
	if (argc == e->args + 1 && !strcasecmp(argv[e->args], "off")) {
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
	if (!strcasecmp(argv[e->args], "atleast"))
		atleast = 1;
	if (argc != e->args + atleast + 1 && argc != e->args + atleast + 2)
		return CLI_SHOWUSAGE;
	if (sscanf(argv[e->args + atleast], "%30d", &newlevel) != 1)
		return CLI_SHOWUSAGE;
	if (argc == e->args + atleast + 2) {
		unsigned int debug = (*what == 'C');
		dfl = debug ? &debug_files : &verbose_files;

		fn = argv[e->args + atleast + 1];

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
	const char *s;

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
	if (x > 0 || ast_str_strlen(out) == 0)	/* if there is nothing, print 0 seconds */
		ast_str_append(&out, 0, "%d second%s ", x, ESS(x));
	ast_cli(fd, "%s: %s\n", prefix, ast_str_buffer(out));
}

static struct ast_cli_entry *cli_next(struct ast_cli_entry *e)
{
	if (e) {
		return AST_LIST_NEXT(e, list);
	} else {
		return AST_LIST_FIRST(&helpers);
	}
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
	const char *like;

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
#define CONCISE_FORMAT_STRING  "%s!%s!%s!%d!%s!%s!%s!%s!%s!%s!%d!%s!%s!%s\n"
#define VERBOSE_FORMAT_STRING  "%-20.20s %-20.20s %-16.16s %4d %-7.7s %-12.12s %-25.25s %-15.15s %8.8s %-11.11s %-11.11s %-20.20s\n"
#define VERBOSE_FORMAT_STRING2 "%-20.20s %-20.20s %-16.16s %-4.4s %-7.7s %-12.12s %-25.25s %-15.15s %8.8s %-11.11s %-11.11s %-20.20s\n"

	struct ast_channel *c = NULL;
	int numchans = 0, concise = 0, verbose = 0, count = 0;
	struct ast_channel_iterator *iter = NULL;

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

	if (a->argc == e->args) {
		if (!strcasecmp(a->argv[e->args-1],"concise"))
			concise = 1;
		else if (!strcasecmp(a->argv[e->args-1],"verbose"))
			verbose = 1;
		else if (!strcasecmp(a->argv[e->args-1],"count"))
			count = 1;
		else
			return CLI_SHOWUSAGE;
	} else if (a->argc != e->args - 1)
		return CLI_SHOWUSAGE;

	if (!count) {
		if (!concise && !verbose)
			ast_cli(a->fd, FORMAT_STRING2, "Channel", "Location", "State", "Application(Data)");
		else if (verbose)
			ast_cli(a->fd, VERBOSE_FORMAT_STRING2, "Channel", "Context", "Extension", "Priority", "State", "Application", "Data", 
				"CallerID", "Duration", "Accountcode", "PeerAccount", "BridgedTo");
	}

	if (!count && !(iter = ast_channel_iterator_all_new(0))) {
		return CLI_FAILURE;
	}

	for (; iter && (c = ast_channel_iterator_next(iter)); ast_channel_unref(c)) {
		struct ast_channel *bc;
		char durbuf[10] = "-";

		ast_channel_lock(c);

		bc = ast_bridged_channel(c);

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
				ast_cli(a->fd, CONCISE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
					c->appl ? c->appl : "(None)",
					S_OR(c->data, ""),	/* XXX different from verbose ? */
					S_OR(c->cid.cid_num, ""),
					S_OR(c->accountcode, ""),
					S_OR(c->peeraccount, ""),
					c->amaflags, 
					durbuf,
					bc ? bc->name : "(None)",
					c->uniqueid);
			} else if (verbose) {
				ast_cli(a->fd, VERBOSE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
					c->appl ? c->appl : "(None)",
					c->data ? S_OR(c->data, "(Empty)" ): "(None)",
					S_OR(c->cid.cid_num, ""),
					durbuf,
					S_OR(c->accountcode, ""),
					S_OR(c->peeraccount, ""),
					bc ? bc->name : "(None)");
			} else {
				char locbuf[40] = "(None)";
				char appdata[40] = "(None)";
				
				if (!ast_strlen_zero(c->context) && !ast_strlen_zero(c->exten)) 
					snprintf(locbuf, sizeof(locbuf), "%s@%s:%d", c->exten, c->context, c->priority);
				if (c->appl)
					snprintf(appdata, sizeof(appdata), "%s(%s)", c->appl, S_OR(c->data, ""));
				ast_cli(a->fd, FORMAT_STRING, c->name, locbuf, ast_state2str(c->_state), appdata);
			}
		}
		ast_channel_unlock(c);
	}

	if (iter) {
		ast_channel_iterator_destroy(iter);
	}

	if (!concise) {
		numchans = ast_active_channels();
		ast_cli(a->fd, "%d active channel%s\n", numchans, ESS(numchans));
		if (option_maxcalls)
			ast_cli(a->fd, "%d of %d max active call%s (%5.2f%% of capacity)\n",
				ast_active_calls(), option_maxcalls, ESS(ast_active_calls()),
				((double)ast_active_calls() / (double)option_maxcalls) * 100.0);
		else
			ast_cli(a->fd, "%d active call%s\n", ast_active_calls(), ESS(ast_active_calls()));

		ast_cli(a->fd, "%d call%s processed\n", ast_processed_calls(), ESS(ast_processed_calls()));
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
		e->command = "channel request hangup";
		e->usage =
			"Usage: channel request hangup <channel>\n"
			"       Request that a channel be hung up. The hangup takes effect\n"
			"       the next time the driver reads or writes from the channel\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, e->args);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if ((c = ast_channel_get_by_name(a->argv[3]))) {
		ast_channel_lock(c);
		ast_cli(a->fd, "Requested Hangup on channel '%s'\n", c->name);
		ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
		ast_channel_unlock(c);
		c = ast_channel_unref(c);
	} else {
		ast_cli(a->fd, "%s is not a known channel\n", a->argv[3]);
	}

	return CLI_SUCCESS;
}

/*! \brief handles CLI command 'cli show permissions' */
static char *handle_cli_show_permissions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct usergroup_cli_perm *cp;
	struct cli_perm *perm;
	struct passwd *pw = NULL;
	struct group *gr = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "cli show permissions";
		e->usage =
			"Usage: cli show permissions\n"
			"       Shows CLI configured permissions.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_RWLIST_RDLOCK(&cli_perms);
	AST_LIST_TRAVERSE(&cli_perms, cp, list) {
		if (cp->uid >= 0) {
			pw = getpwuid(cp->uid);
			if (pw) {
				ast_cli(a->fd, "user: %s [uid=%d]\n", pw->pw_name, cp->uid);
			}
		} else {
			gr = getgrgid(cp->gid);
			if (gr) {
				ast_cli(a->fd, "group: %s [gid=%d]\n", gr->gr_name, cp->gid);
			}
		}
		ast_cli(a->fd, "Permissions:\n");
		if (cp->perms) {
			AST_LIST_TRAVERSE(cp->perms, perm, list) {
				ast_cli(a->fd, "\t%s -> %s\n", perm->permit ? "permit" : "deny", perm->command);
			}
		}
		ast_cli(a->fd, "\n");
	}
	AST_RWLIST_UNLOCK(&cli_perms);

	return CLI_SUCCESS;
}

/*! \brief handles CLI command 'cli reload permissions' */
static char *handle_cli_reload_permissions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "cli reload permissions";
		e->usage =
			"Usage: cli reload permissions\n"
			"       Reload the 'cli_permissions.conf' file.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli_perms_init(1);

	return CLI_SUCCESS;
}

/*! \brief handles CLI command 'cli check permissions' */
static char *handle_cli_check_permissions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct passwd *pw = NULL;
	struct group *gr;
	int gid = -1, uid = -1;
	char command[AST_MAX_ARGS] = "";
	struct ast_cli_entry *ce = NULL;
	int found = 0;
	char *group, *tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "cli check permissions";
		e->usage =
			"Usage: cli check permissions {<username>|@<groupname>|<username>@<groupname>} [<command>]\n"
			"       Check permissions config for a user@group or list the allowed commands for the specified user.\n"
			"       The username or the groupname may be omitted.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos >= 4) {
			return ast_cli_generator(a->line + strlen("cli check permissions") + strlen(a->argv[3]) + 1, a->word, a->n);
		}
		return NULL;
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	tmp = ast_strdupa(a->argv[3]);
	group = strchr(tmp, '@');
	if (group) {
		gr = getgrnam(&group[1]);
		if (!gr) {
			ast_cli(a->fd, "Unknown group '%s'\n", &group[1]);
			return CLI_FAILURE;
		}
		group[0] = '\0';
		gid = gr->gr_gid;
	}

	if (!group && ast_strlen_zero(tmp)) {
		ast_cli(a->fd, "You didn't supply a username\n");
	} else if (!ast_strlen_zero(tmp) && !(pw = getpwnam(tmp))) {
		ast_cli(a->fd, "Unknown user '%s'\n", tmp);
		return CLI_FAILURE;
	} else if (pw) {
		uid = pw->pw_uid;
	}

	if (a->argc == 4) {
		while ((ce = cli_next(ce))) {
			/* Hide commands that start with '_' */
			if (ce->_full_cmd[0] == '_') {
				continue;
			}
			if (cli_has_permissions(uid, gid, ce->_full_cmd)) {
				ast_cli(a->fd, "%30.30s %s\n", ce->_full_cmd, S_OR(ce->summary, "<no description available>"));
				found++;
			}
		}
		if (!found) {
			ast_cli(a->fd, "You are not allowed to run any command on Asterisk\n");
		}
	} else {
		ast_join(command, sizeof(command), a->argv + 4);
		ast_cli(a->fd, "%s '%s%s%s' is %s to run command: '%s'\n", uid >= 0 ? "User" : "Group", tmp,
			group && uid >= 0 ? "@" : "",
			group ? &group[1] : "",
			cli_has_permissions(uid, gid, command) ? "allowed" : "not allowed", command);
	}

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

struct channel_set_debug_args {
	int fd;
	int is_off;
};

static int channel_set_debug(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *chan = obj;
	struct channel_set_debug_args *args = data;

	ast_channel_lock(chan);

	if (!(chan->fin & DEBUGCHAN_FLAG) || !(chan->fout & DEBUGCHAN_FLAG)) {
		if (args->is_off) {
			chan->fin &= ~DEBUGCHAN_FLAG;
			chan->fout &= ~DEBUGCHAN_FLAG;
		} else {
			chan->fin |= DEBUGCHAN_FLAG;
			chan->fout |= DEBUGCHAN_FLAG;
		}
		ast_cli(args->fd, "Debugging %s on channel %s\n", args->is_off ? "disabled" : "enabled",
				chan->name);
	}

	ast_channel_unlock(chan);

	return 0;
}

static char *handle_core_set_debug_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c = NULL;
	struct channel_set_debug_args args = {
		.fd = a->fd,
	};

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

	if (cmd == (CLI_HANDLER + 1000)) {
		/* called from handle_nodebugchan_deprecated */
		args.is_off = 1;
	} else if (a->argc == e->args + 2) {
		/* 'core set debug channel {all|chan_id}' */
		if (!strcasecmp(a->argv[e->args + 1], "off"))
			args.is_off = 1;
		else
			return CLI_SHOWUSAGE;
	} else if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp("all", a->argv[e->args])) {
		if (args.is_off) {
			global_fin &= ~DEBUGCHAN_FLAG;
			global_fout &= ~DEBUGCHAN_FLAG;
		} else {
			global_fin |= DEBUGCHAN_FLAG;
			global_fout |= DEBUGCHAN_FLAG;
		}
		ast_channel_callback(channel_set_debug, NULL, &args, OBJ_NODATA | OBJ_MULTIPLE);
	} else {
		if ((c = ast_channel_get_by_name(a->argv[e->args]))) {
			channel_set_debug(c, NULL, &args, 0);
			ast_channel_unref(c);
		} else {
			ast_cli(a->fd, "No such channel %s\n", a->argv[e->args]);
		}
	}

	ast_cli(a->fd, "Debugging on new channels is %s\n", args.is_off ? "disabled" : "enabled");

	return CLI_SUCCESS;
}

static char *handle_nodebugchan_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "no debug channel";
		return NULL;
	case CLI_HANDLER:
		/* exit out of switch statement */
		break;
	default:
		return NULL;
	}

	if (a->argc != e->args + 1)
		return CLI_SHOWUSAGE;

	/* add a 'magic' value to the CLI_HANDLER command so that
	 * handle_core_set_debug_channel() will act as if 'off'
	 * had been specified as part of the command
	 */
	res = handle_core_set_debug_channel(e, CLI_HANDLER + 1000, a);

	return res;
}
		
static char *handle_showchan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c=NULL;
	struct timeval now;
	struct ast_str *out = ast_str_thread_get(&ast_str_thread_global_buf, 16);
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
	
	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	now = ast_tvnow();

	if (!(c = ast_channel_get_by_name(a->argv[3]))) {
		ast_cli(a->fd, "%s is not a known channel\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_channel_lock(c);

	if (c->cdr) {
		elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
		snprintf(cdrtime, sizeof(cdrtime), "%dh%dm%ds", hour, min, sec);
	} else {
		strcpy(cdrtime, "N/A");
	}

	ast_cli(a->fd, 
		" -- General --\n"
		"           Name: %s\n"
		"           Type: %s\n"
		"       UniqueID: %s\n"
		"       LinkedID: %s\n"
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
		c->name, c->tech->type, c->uniqueid, c->linkedid,
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
	
	if (pbx_builtin_serialize_variables(c, &out)) {
		ast_cli(a->fd,"      Variables:\n%s\n", ast_str_buffer(out));
	}

	if (c->cdr && ast_cdr_serialize_variables(c->cdr, &out, '=', '\n', 1)) {
		ast_cli(a->fd,"  CDR Variables:\n%s\n", ast_str_buffer(out));
	}

#ifdef CHANNEL_TRACE
	trace_enabled = ast_channel_trace_is_enabled(c);
	ast_cli(a->fd, "  Context Trace: %s\n", trace_enabled ? "Enabled" : "Disabled");
	if (trace_enabled && ast_channel_trace_serialize(c, &out))
		ast_cli(a->fd, "          Trace:\n%s\n", ast_str_buffer(out));
#endif

	ast_channel_unlock(c);
	c = ast_channel_unref(c);

	return CLI_SUCCESS;
}

/*
 * helper function to generate CLI matches from a fixed set of values.
 * A NULL word is acceptable.
 */
char *ast_cli_complete(const char *word, const char * const choices[], int state)
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
	char notfound = '\0';
	char *ret = &notfound; /* so NULL can break the loop */
	struct ast_channel_iterator *iter;

	if (pos != rpos) {
		return NULL;
	}

	if (!(iter = ast_channel_iterator_by_name_new(0, word, strlen(word)))) {
		return NULL;
	}

	while (ret == &notfound && (c = ast_channel_iterator_next(iter))) {
		if (++which > state) {
			ast_channel_lock(c);
			ret = ast_strdup(c->name);
			ast_channel_unlock(c);
		}
		ast_channel_unref(c);
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
		gi = AST_LIST_NEXT(gi, group_list);
	}
	
	ast_app_group_list_unlock();
	
	if (havepattern)
		regfree(&regexbuf);

	ast_cli(a->fd, "%d active channel%s\n", numchans, ESS(numchans));
	return CLI_SUCCESS;
#undef FORMAT_STRING
}

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

	AST_CLI_DEFINE(handle_core_set_debug_channel, "Enable/disable debugging on a channel"),

	AST_CLI_DEFINE(handle_verbose, "Set level of debug/verbose chattiness"),

	AST_CLI_DEFINE(group_show_channels, "Display active channels with group(s)"),

	AST_CLI_DEFINE(handle_help, "Display help list, or specific help on a command"),

	AST_CLI_DEFINE(handle_logger_mute, "Toggle logging output to a console"),

	AST_CLI_DEFINE(handle_modlist, "List modules and info"),

	AST_CLI_DEFINE(handle_load, "Load a module by name"),

	AST_CLI_DEFINE(handle_reload, "Reload configuration"),

	AST_CLI_DEFINE(handle_unload, "Unload a module by name"),

	AST_CLI_DEFINE(handle_showuptime, "Show uptime information"),

	AST_CLI_DEFINE(handle_softhangup, "Request a hangup on a given channel"),

	AST_CLI_DEFINE(handle_cli_reload_permissions, "Reload CLI permissions config"),

	AST_CLI_DEFINE(handle_cli_show_permissions, "Show CLI permissions"),

	AST_CLI_DEFINE(handle_cli_check_permissions, "Try a permissions config for a user"),
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

/*! \brief cleanup (free) cli_perms linkedlist. */
static void destroy_user_perms(void)
{
	struct cli_perm *perm;
	struct usergroup_cli_perm *user_perm;

	AST_RWLIST_WRLOCK(&cli_perms);
	while ((user_perm = AST_LIST_REMOVE_HEAD(&cli_perms, list))) {
		while ((perm = AST_LIST_REMOVE_HEAD(user_perm->perms, list))) {
			ast_free(perm->command);
			ast_free(perm);
		}
		ast_free(user_perm);
	}
	AST_RWLIST_UNLOCK(&cli_perms);
}

int ast_cli_perms_init(int reload)
{
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_config *cfg;
	char *cat = NULL;
	struct ast_variable *v;
	struct usergroup_cli_perm *user_group, *cp_entry;
	struct cli_perm *perm = NULL;
	struct passwd *pw;
	struct group *gr;

	if (ast_mutex_trylock(&permsconfiglock)) {
		ast_log(LOG_NOTICE, "You must wait until last 'cli reload permissions' command finish\n");
		return 1;
	}

	cfg = ast_config_load2(perms_config, "" /* core, can't reload */, config_flags);
	if (!cfg) {
		ast_mutex_unlock(&permsconfiglock);
		return 1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_mutex_unlock(&permsconfiglock);
		return 0;
	}

	/* free current structures. */
	destroy_user_perms();

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			/* General options */
			for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "default_perm")) {
					cli_default_perm = (!strcasecmp(v->value, "permit")) ? 1: 0;
				}
			}
			continue;
		}

		/* users or groups */
		gr = NULL, pw = NULL;
		if (cat[0] == '@') {
			/* This is a group */
			gr = getgrnam(&cat[1]);
			if (!gr) {
				ast_log (LOG_WARNING, "Unknown group '%s'\n", &cat[1]);
				continue;
			}
		} else {
			/* This is a user */
			pw = getpwnam(cat);
			if (!pw) {
				ast_log (LOG_WARNING, "Unknown user '%s'\n", cat);
				continue;
			}
		}
		user_group = NULL;
		/* Check for duplicates */
		AST_RWLIST_WRLOCK(&cli_perms);
		AST_LIST_TRAVERSE(&cli_perms, cp_entry, list) {
			if ((pw && cp_entry->uid == pw->pw_uid) || (gr && cp_entry->gid == gr->gr_gid)) {
				/* if it is duplicated, just added this new settings, to 
				the current list. */
				user_group = cp_entry;
				break;
			}
		}
		AST_RWLIST_UNLOCK(&cli_perms);

		if (!user_group) {
			/* alloc space for the new user config. */
			user_group = ast_calloc(1, sizeof(*user_group));
			if (!user_group) {
				continue;
			}
			user_group->uid = (pw ? pw->pw_uid : -1);
			user_group->gid = (gr ? gr->gr_gid : -1);
			user_group->perms = ast_calloc(1, sizeof(*user_group->perms));
			if (!user_group->perms) {
				ast_free(user_group);
				continue;
			}
		}
		for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
			if (ast_strlen_zero(v->value)) {
				/* we need to check this condition cause it could break security. */
				ast_log(LOG_WARNING, "Empty permit/deny option in user '%s'\n", cat);
				continue;
			}
			if (!strcasecmp(v->name, "permit")) {
				perm = ast_calloc(1, sizeof(*perm));
				if (perm) {
					perm->permit = 1;
					perm->command = ast_strdup(v->value);
				}
			} else if (!strcasecmp(v->name, "deny")) {
				perm = ast_calloc(1, sizeof(*perm));
				if (perm) {
					perm->permit = 0;
					perm->command = ast_strdup(v->value);
				}
			} else {
				/* up to now, only 'permit' and 'deny' are possible values. */
				ast_log(LOG_WARNING, "Unknown '%s' option\n", v->name);
				continue;
			}
			if (perm) {
				/* Added the permission to the user's list. */
				AST_LIST_INSERT_TAIL(user_group->perms, perm, list);
				perm = NULL;
			}
		}
		AST_RWLIST_WRLOCK(&cli_perms);
		AST_RWLIST_INSERT_TAIL(&cli_perms, user_group, list);
		AST_RWLIST_UNLOCK(&cli_perms);
	}

	ast_config_destroy(cfg);
	ast_mutex_unlock(&permsconfiglock);
	return 0;
}

/*! \brief initialize the _full_cmd string in * each of the builtins. */
void ast_builtins_init(void)
{
	ast_cli_register_multiple(cli_cli, ARRAY_LEN(cli_cli));
}

/*!
 * match a word in the CLI entry.
 * returns -1 on mismatch, 0 on match of an optional word,
 * 1 on match of a full word.
 *
 * The pattern can be
 *   any_word           match for equal
 *   [foo|bar|baz]      optionally, one of these words
 *   {foo|bar|baz}      exactly, one of these words
 *   %                  any word
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
 * \internal
 * \brief locate a cli command in the 'helpers' list (which must be locked).
 *     The search compares word by word taking care of regexps in e->cmda
 *     This function will return NULL when nothing is matched, or the ast_cli_entry that matched.
 * \param cmds
 * \param match_type has 3 possible values:
 *      0       returns if the search key is equal or longer than the entry.
 *		            note that trailing optional arguments are skipped.
 *      -1      true if the mismatch is on the last word XXX not true!
 *      1       true only on complete, exact match.
 *
 */
static struct ast_cli_entry *find_cli(const char * const cmds[], int match_type)
{
	int matchlen = -1;	/* length of longest match so far */
	struct ast_cli_entry *cand = NULL, *e=NULL;

	while ( (e = cli_next(e)) ) {
		/* word-by word regexp comparison */
		const char * const *src = cmds;
		const char * const *dst = e->cmda;
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

static char *find_best(const char *argv[])
{
	static char cmdline[80];
	int x;
	/* See how close we get, then print the candidate */
	const char *myargv[AST_MAX_CMD_LEN] = { NULL, };

	AST_RWLIST_RDLOCK(&helpers);
	for (x = 0; argv[x]; x++) {
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
			char *cmda = (char *) e->cmda;
			memset(cmda, '\0', sizeof(e->cmda));
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

	memset(&a, '\0', sizeof(a));
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
		ast_log(LOG_WARNING, "Command '%s' already registered (or something close enough)\n", S_OR(e->_full_cmd, e->command));
		goto done;
	}
	if (set_full_cmd(e))
		goto done;

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
static char *help1(int fd, const char * const match[], int locked)
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
		e->command = "core show help";
		e->usage =
			"Usage: core show help [topic]\n"
			"       When called with a topic as an argument, displays usage\n"
			"       information on the given command. If called without a\n"
			"       topic, it provides a list of commands.\n";
		return NULL;

	} else if (cmd == CLI_GENERATE) {
		/* skip first 14 or 15 chars, "core show help " */
		int l = strlen(a->line);

		if (l > 15) {
			l = 15;
		}
		/* XXX watch out, should stop to the non-generator parts */
		return __ast_cli_generator(a->line + l, a->word, a->n, 0);
	}
	if (a->argc == e->args) {
		return help1(a->fd, NULL, 0);
	}

	AST_RWLIST_RDLOCK(&helpers);
	my_e = find_cli(a->argv + 3, 1);	/* try exact match first */
	if (!my_e) {
		res = help1(a->fd, a->argv + 3, 1 /* locked */);
		AST_RWLIST_UNLOCK(&helpers);
		return res;
	}
	if (my_e->usage)
		ast_cli(a->fd, "%s", my_e->usage);
	else {
		ast_join(fullcmd, sizeof(fullcmd), a->argv + 3);
		ast_cli(a->fd, "No help text available for '%s'.\n", fullcmd);
	}
	AST_RWLIST_UNLOCK(&helpers);
	return res;
}

static char *parse_args(const char *s, int *argc, const char *argv[], int max, int *trailingwhitespace)
{
	char *duplicate, *cur;
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
	if (!(duplicate = ast_strdup(s)))
		return NULL;

	cur = duplicate;
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
	return duplicate;
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
static int more_words (const char * const *dst)
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
	const char *argv[AST_MAX_ARGS];
	struct ast_cli_entry *e = NULL;
	int x = 0, argindex, matchlen;
	int matchnum=0;
	char *ret = NULL;
	char matchstr[80] = "";
	int tws = 0;
	/* Split the argument into an array of words */
	char *duplicate = parse_args(text, &x, argv, ARRAY_LEN(argv), &tws);

	if (!duplicate)	/* malloc error */
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
					.n = state - matchnum,
					.argv = argv,
					.argc = x};
				ret = e->handler(e, CLI_GENERATE, &a);
			}
			if (ret)
				break;
		}
	}
	if (lock)
		AST_RWLIST_UNLOCK(&helpers);
	ast_free(duplicate);
	return ret;
}

char *ast_cli_generator(const char *text, const char *word, int state)
{
	return __ast_cli_generator(text, word, state, 1);
}

int ast_cli_command_full(int uid, int gid, int fd, const char *s)
{
	const char *args[AST_MAX_ARGS + 1];
	struct ast_cli_entry *e;
	int x;
	char *duplicate = parse_args(s, &x, args + 1, AST_MAX_ARGS, NULL);
	char tmp[AST_MAX_ARGS + 1];
	char *retval = NULL;
	struct ast_cli_args a = {
		.fd = fd, .argc = x, .argv = args+1 };

	if (duplicate == NULL)
		return -1;

	if (x < 1)	/* We need at least one entry, otherwise ignore */
		goto done;

	AST_RWLIST_RDLOCK(&helpers);
	e = find_cli(args + 1, 0);
	if (e)
		ast_atomic_fetchadd_int(&e->inuse, 1);
	AST_RWLIST_UNLOCK(&helpers);
	if (e == NULL) {
		ast_cli(fd, "No such command '%s' (type 'core show help %s' for other possible commands)\n", s, find_best(args + 1));
		goto done;
	}

	ast_join(tmp, sizeof(tmp), args + 1);
	/* Check if the user has rights to run this command. */
	if (!cli_has_permissions(uid, gid, tmp)) {
		ast_cli(fd, "You don't have permissions to run '%s' command\n", tmp);
		ast_free(duplicate);
		return 0;
	}

	/*
	 * Within the handler, argv[-1] contains a pointer to the ast_cli_entry.
	 * Remember that the array returned by parse_args is NULL-terminated.
	 */
	args[0] = (char *)e;

	retval = e->handler(e, CLI_HANDLER, &a);

	if (retval == CLI_SHOWUSAGE) {
		ast_cli(fd, "%s", S_OR(e->usage, "Invalid usage, but no usage information available.\n"));
	} else {
		if (retval == CLI_FAILURE)
			ast_cli(fd, "Command '%s' failed.\n", s);
	}
	ast_atomic_fetchadd_int(&e->inuse, -1);
done:
	ast_free(duplicate);
	return 0;
}

int ast_cli_command_multiple_full(int uid, int gid, int fd, size_t size, const char *s)
{
	char cmd[512];
	int x, y = 0, count = 0;

	for (x = 0; x < size; x++) {
		cmd[y] = s[x];
		y++;
		if (s[x] == '\0') {
			ast_cli_command_full(uid, gid, fd, cmd);
			y = 0;
			count++;
		}
	}
	return count;
}
