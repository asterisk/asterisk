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

/*! \li \ref cli.c uses the configuration file \ref cli_permissions.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cli_permissions.conf cli_permissions.conf
 * \verbinclude cli_permissions.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

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
#include <editline/readline.h>

#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/lock.h"
#include "asterisk/threadstorage.h"
#include "asterisk/translate.h"
#include "asterisk/bridge.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"

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
 * \brief map a debug or verbose level to a module name
 */
struct module_level {
	unsigned int level;
	AST_RWLIST_ENTRY(module_level) entry;
	char module[0];
};

AST_RWLIST_HEAD(module_level_list, module_level);

/*! list of module names and their debug levels */
static struct module_level_list debug_modules = AST_RWLIST_HEAD_INIT_VALUE;

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

unsigned int ast_debug_get_by_module(const char *module)
{
	struct module_level *ml;
	unsigned int res = 0;

	AST_RWLIST_RDLOCK(&debug_modules);
	AST_LIST_TRAVERSE(&debug_modules, ml, entry) {
		if (!strcasecmp(ml->module, module)) {
			res = ml->level;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&debug_modules);

	return res;
}

unsigned int ast_verbose_get_by_module(const char *module)
{
	return 0;
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

	ast_std_free(d);

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
	ast_cli(a->fd, "Loaded %s\n", a->argv[e->args]);
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
		enum ast_module_reload_result res = ast_module_reload(a->argv[x]);
		switch (res) {
		case AST_MODULE_RELOAD_NOT_FOUND:
			ast_cli(a->fd, "No such module '%s'\n", a->argv[x]);
			break;
		case AST_MODULE_RELOAD_NOT_IMPLEMENTED:
			ast_cli(a->fd, "The module '%s' does not support reloads\n", a->argv[x]);
			break;
		case AST_MODULE_RELOAD_QUEUED:
			ast_cli(a->fd, "Asterisk cannot reload a module yet; request queued\n");
			break;
		case AST_MODULE_RELOAD_ERROR:
			ast_cli(a->fd, "The module '%s' reported a reload failure\n", a->argv[x]);
			break;
		case AST_MODULE_RELOAD_IN_PROGRESS:
			ast_cli(a->fd, "A module reload request is already in progress; please be patient\n");
			break;
		case AST_MODULE_RELOAD_UNINITIALIZED:
			ast_cli(a->fd, "The module '%s' was not properly initialized. Before reloading"
					" the module, you must run \"module load %s\" and fix whatever is"
					" preventing the module from being initialized.\n", a->argv[x], a->argv[x]);
			break;
		case AST_MODULE_RELOAD_SUCCESS:
			ast_cli(a->fd, "Module '%s' reloaded successfully.\n", a->argv[x]);
			break;
		}
	}
	return CLI_SUCCESS;
}

static char *handle_core_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "core reload";
		e->usage =
			"Usage: core reload\n"
			"       Execute a global reload.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_module_reload(NULL);

	return CLI_SUCCESS;
}

/*!
 * \brief Find the module level setting
 *
 * \param module Module name to look for.
 * \param mll List to search.
 *
 * \retval level struct found on success.
 * \retval NULL not found.
 */
static struct module_level *find_module_level(const char *module, struct module_level_list *mll)
{
	struct module_level *ml;

	AST_LIST_TRAVERSE(mll, ml, entry) {
		if (!strcasecmp(ml->module, module))
			return ml;
	}

	return NULL;
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

static void status_debug_verbose(struct ast_cli_args *a, const char *what, int old_val, int cur_val)
{
	char was_buf[30];
	const char *was;

	if (old_val) {
		snprintf(was_buf, sizeof(was_buf), "%d", old_val);
		was = was_buf;
	} else {
		was = "OFF";
	}

	if (old_val == cur_val) {
		ast_cli(a->fd, "%s is still %s.\n", what, was);
	} else {
		char now_buf[30];
		const char *now;

		if (cur_val) {
			snprintf(now_buf, sizeof(now_buf), "%d", cur_val);
			now = now_buf;
		} else {
			now = "OFF";
		}

		ast_cli(a->fd, "%s was %s and is now %s.\n", what, was, now);
	}
}

static char *handle_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval;
	int newlevel;
	int atleast = 0;
	const char *argv3 = a->argv ? S_OR(a->argv[3], "") : "";
	struct module_level *ml;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core set debug";
		e->usage =
#if !defined(LOW_MEMORY)
			"Usage: core set debug [atleast] <level> [module]\n"
#else
			"Usage: core set debug [atleast] <level>\n"
#endif
			"       core set debug off\n"
			"\n"
#if !defined(LOW_MEMORY)
			"       Sets level of debug messages to be displayed or\n"
			"       sets a module name to display debug messages from.\n"
#else
			"       Sets level of debug messages to be displayed.\n"
#endif
			"       0 or off means no messages should be displayed.\n"
			"       Equivalent to -d[d[...]] on startup\n";
		return NULL;

	case CLI_GENERATE:
		if (!strcasecmp(argv3, "atleast")) {
			atleast = 1;
		}
		if (a->pos == 3 || (a->pos == 4 && atleast)) {
			const char *pos = a->pos == 3 ? argv3 : S_OR(a->argv[4], "");
			int numbermatch = (ast_strlen_zero(pos) || strchr("123456789", pos[0])) ? 0 : 21;

			if (a->n < 21 && numbermatch == 0) {
				return complete_number(pos, 0, 0x7fffffff, a->n);
			} else if (pos[0] == '0') {
				if (a->n == 0) {
					return ast_strdup("0");
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
#if !defined(LOW_MEMORY)
		} else if ((a->pos == 4 && !atleast && strcasecmp(argv3, "off") && strcasecmp(argv3, "channel"))
			|| (a->pos == 5 && atleast)) {
			const char *pos = S_OR(a->argv[a->pos], "");

			return ast_complete_source_filename(pos, a->n);
#endif
		}
		return NULL;
	}
	/* all the above return, so we proceed with the handler.
	 * we are guaranteed to be called with argc >= e->args;
	 */

	if (a->argc <= e->args) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == e->args + 1 && !strcasecmp(a->argv[e->args], "off")) {
		newlevel = 0;
	} else {
		if (!strcasecmp(a->argv[e->args], "atleast")) {
			atleast = 1;
		}
		if (a->argc != e->args + atleast + 1 && a->argc != e->args + atleast + 2) {
			return CLI_SHOWUSAGE;
		}
		if (sscanf(a->argv[e->args + atleast], "%30d", &newlevel) != 1) {
			return CLI_SHOWUSAGE;
		}

		if (a->argc == e->args + atleast + 2) {
			/* We have specified a module name. */
			char *mod = ast_strdupa(a->argv[e->args + atleast + 1]);
			int mod_len = strlen(mod);

			if (3 < mod_len && !strcasecmp(mod + mod_len - 3, ".so")) {
				mod[mod_len - 3] = '\0';
			}

			AST_RWLIST_WRLOCK(&debug_modules);

			ml = find_module_level(mod, &debug_modules);
			if (!newlevel) {
				if (!ml) {
					/* Specified off for a nonexistent entry. */
					AST_RWLIST_UNLOCK(&debug_modules);
					ast_cli(a->fd, "Core debug is still 0 for '%s'.\n", mod);
					return CLI_SUCCESS;
				}
				AST_RWLIST_REMOVE(&debug_modules, ml, entry);
				if (AST_RWLIST_EMPTY(&debug_modules)) {
					ast_clear_flag(&ast_options, AST_OPT_FLAG_DEBUG_MODULE);
				}
				AST_RWLIST_UNLOCK(&debug_modules);
				ast_cli(a->fd, "Core debug was %u and has been set to 0 for '%s'.\n",
					ml->level, mod);
				ast_free(ml);
				return CLI_SUCCESS;
			}

			if (ml) {
				if ((atleast && newlevel < ml->level) || ml->level == newlevel) {
					ast_cli(a->fd, "Core debug is still %u for '%s'.\n", ml->level, mod);
					AST_RWLIST_UNLOCK(&debug_modules);
					return CLI_SUCCESS;
				}
				oldval = ml->level;
				ml->level = newlevel;
			} else {
				ml = ast_calloc(1, sizeof(*ml) + strlen(mod) + 1);
				if (!ml) {
					AST_RWLIST_UNLOCK(&debug_modules);
					return CLI_FAILURE;
				}
				oldval = ml->level;
				ml->level = newlevel;
				strcpy(ml->module, mod);
				AST_RWLIST_INSERT_TAIL(&debug_modules, ml, entry);
			}
			ast_set_flag(&ast_options, AST_OPT_FLAG_DEBUG_MODULE);

			ast_cli(a->fd, "Core debug was %d and has been set to %u for '%s'.\n",
				oldval, ml->level, ml->module);

			AST_RWLIST_UNLOCK(&debug_modules);

			return CLI_SUCCESS;
		}
	}

	/* Update global debug level */
	if (!newlevel) {
		/* Specified level was 0 or off. */
		AST_RWLIST_WRLOCK(&debug_modules);
		while ((ml = AST_RWLIST_REMOVE_HEAD(&debug_modules, entry))) {
			ast_free(ml);
		}
		ast_clear_flag(&ast_options, AST_OPT_FLAG_DEBUG_MODULE);
		AST_RWLIST_UNLOCK(&debug_modules);
	}
	oldval = option_debug;
	if (!atleast || newlevel > option_debug) {
		option_debug = newlevel;
	}

	/* Report debug level status */
	status_debug_verbose(a, "Core debug", oldval, option_debug);

	return CLI_SUCCESS;
}

static char *handle_verbose(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int oldval;
	int newlevel;
	int atleast = 0;
	int silent = 0;
	const char *argv3 = a->argv ? S_OR(a->argv[3], "") : "";

	switch (cmd) {
	case CLI_INIT:
		e->command = "core set verbose";
		e->usage =
			"Usage: core set verbose [atleast] <level> [silent]\n"
			"       core set verbose off\n"
			"\n"
			"       Sets level of verbose messages to be displayed.\n"
			"       0 or off means no verbose messages should be displayed.\n"
			"       The silent option means the command does not report what\n"
			"       happened to the verbose level.\n"
			"       Equivalent to -v[v[...]] on startup\n";
		return NULL;

	case CLI_GENERATE:
		if (!strcasecmp(argv3, "atleast")) {
			atleast = 1;
		}
		if (a->pos == 3 || (a->pos == 4 && atleast)) {
			const char *pos = a->pos == 3 ? argv3 : S_OR(a->argv[4], "");
			int numbermatch = (ast_strlen_zero(pos) || strchr("123456789", pos[0])) ? 0 : 21;

			if (a->n < 21 && numbermatch == 0) {
				return complete_number(pos, 0, 0x7fffffff, a->n);
			} else if (pos[0] == '0') {
				if (a->n == 0) {
					return ast_strdup("0");
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
		} else if ((a->pos == 4 && !atleast && strcasecmp(argv3, "off"))
			|| (a->pos == 5 && atleast)) {
			const char *pos = S_OR(a->argv[a->pos], "");

			if (a->n == 0 && !strncasecmp(pos, "silent", strlen(pos))) {
				return ast_strdup("silent");
			}
		}
		return NULL;
	}
	/* all the above return, so we proceed with the handler.
	 * we are guaranteed to be called with argc >= e->args;
	 */

	if (a->argc <= e->args) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == e->args + 1 && !strcasecmp(a->argv[e->args], "off")) {
		newlevel = 0;
	} else {
		if (!strcasecmp(a->argv[e->args], "atleast")) {
			atleast = 1;
		}
		if (a->argc == e->args + atleast + 2
			&& !strcasecmp(a->argv[e->args + atleast + 1], "silent")) {
			silent = 1;
		}
		if (a->argc != e->args + atleast + silent + 1) {
			return CLI_SHOWUSAGE;
		}
		if (sscanf(a->argv[e->args + atleast], "%30d", &newlevel) != 1) {
			return CLI_SHOWUSAGE;
		}
	}

	/* Update verbose level */
	oldval = ast_verb_console_get();
	if (!atleast || newlevel > oldval) {
		ast_verb_console_set(newlevel);
	} else {
		newlevel = oldval;
	}

	if (silent) {
		/* Be silent after setting the level. */
		return CLI_SUCCESS;
	}

	/* Report verbose level status */
	status_debug_verbose(a, "Console verbose", oldval, newlevel);

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
		ast_cli(a->fd, "Unloaded %s\n", a->argv[x]);
	}

	return CLI_SUCCESS;
}

#define MODLIST_FORMAT  "%-30s %-40.40s %-10d %-11s %13s\n"
#define MODLIST_FORMAT2 "%-30s %-40.40s %-10s %-11s %13s\n"

AST_MUTEX_DEFINE_STATIC(climodentrylock);
static int climodentryfd = -1;

static int modlist_modentry(const char *module, const char *description,
		int usecnt, const char *status, const char *like,
		enum ast_module_support_level support_level)
{
	/* Comparing the like with the module */
	if (strcasestr(module, like) ) {
		ast_cli(climodentryfd, MODLIST_FORMAT, module, description, usecnt,
				status, ast_module_support_level_to_string(support_level));
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
	ast_cli(a->fd, MODLIST_FORMAT2, "Module", "Description", "Use Count", "Status", "Support Level");
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

	if (ast_option_maxcalls) {
		ast_cli(a->fd, "%d of %d max active call%s (%5.2f%% of capacity)\n",
		   ast_active_calls(), ast_option_maxcalls, ESS(ast_active_calls()),
		   ((double)ast_active_calls() / (double)ast_option_maxcalls) * 100.0);
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

	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);
	struct ao2_iterator it_chans;
	struct stasis_message *msg;
	int numchans = 0, concise = 0, verbose = 0, count = 0;

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


	if (!(channels = stasis_cache_dump(ast_channel_cache_by_name(), ast_channel_snapshot_type()))) {
		ast_cli(a->fd, "Failed to retrieve cached channels\n");
		return CLI_SUCCESS;
	}

	if (!count) {
		if (!concise && !verbose)
			ast_cli(a->fd, FORMAT_STRING2, "Channel", "Location", "State", "Application(Data)");
		else if (verbose)
			ast_cli(a->fd, VERBOSE_FORMAT_STRING2, "Channel", "Context", "Extension", "Priority", "State", "Application", "Data",
				"CallerID", "Duration", "Accountcode", "PeerAccount", "BridgeID");
	}

	it_chans = ao2_iterator_init(channels, 0);
	for (; (msg = ao2_iterator_next(&it_chans)); ao2_ref(msg, -1)) {
		struct ast_channel_snapshot *cs = stasis_message_data(msg);
		char durbuf[10] = "-";

		if (!count) {
			if ((concise || verbose)  && !ast_tvzero(cs->creationtime)) {
				int duration = (int)(ast_tvdiff_ms(ast_tvnow(), cs->creationtime) / 1000);
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
				ast_cli(a->fd, CONCISE_FORMAT_STRING, cs->name, cs->context, cs->exten, cs->priority, ast_state2str(cs->state),
					S_OR(cs->appl, "(None)"),
					cs->data,
					cs->caller_number,
					cs->accountcode,
					cs->peeraccount,
					cs->amaflags,
					durbuf,
					cs->bridgeid,
					cs->uniqueid);
			} else if (verbose) {
				ast_cli(a->fd, VERBOSE_FORMAT_STRING, cs->name, cs->context, cs->exten, cs->priority, ast_state2str(cs->state),
					S_OR(cs->appl, "(None)"),
					S_OR(cs->data, "(Empty)"),
					cs->caller_number,
					durbuf,
					cs->accountcode,
					cs->peeraccount,
					cs->bridgeid);
			} else {
				char locbuf[40] = "(None)";
				char appdata[40] = "(None)";

				if (!cs->context && !cs->exten)
					snprintf(locbuf, sizeof(locbuf), "%s@%s:%d", cs->exten, cs->context, cs->priority);
				if (cs->appl)
					snprintf(appdata, sizeof(appdata), "%s(%s)", cs->appl, S_OR(cs->data, ""));
				ast_cli(a->fd, FORMAT_STRING, cs->name, locbuf, ast_state2str(cs->state), appdata);
			}
		}
	}
	ao2_iterator_destroy(&it_chans);

	if (!concise) {
		numchans = ast_active_channels();
		ast_cli(a->fd, "%d active channel%s\n", numchans, ESS(numchans));
		if (ast_option_maxcalls)
			ast_cli(a->fd, "%d of %d max active call%s (%5.2f%% of capacity)\n",
				ast_active_calls(), ast_option_maxcalls, ESS(ast_active_calls()),
				((double)ast_active_calls() / (double)ast_option_maxcalls) * 100.0);
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
			"Usage: channel request hangup <channel>|<all>\n"
			"       Request that a channel be hung up. The hangup takes effect\n"
			"       the next time the driver reads or writes from the channel.\n"
			"       If 'all' is specified instead of a channel name, all channels\n"
			"       will see the hangup request.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, e->args);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "all")) {
		struct ast_channel_iterator *iter = NULL;
		if (!(iter = ast_channel_iterator_all_new())) {
			return CLI_FAILURE;
		}
		for (; iter && (c = ast_channel_iterator_next(iter)); ast_channel_unref(c)) {
			ast_channel_lock(c);
			ast_cli(a->fd, "Requested Hangup on channel '%s'\n", ast_channel_name(c));
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			ast_channel_unlock(c);
		}
		ast_channel_iterator_destroy(iter);
	} else if ((c = ast_channel_get_by_name(a->argv[3]))) {
		ast_channel_lock(c);
		ast_cli(a->fd, "Requested Hangup on channel '%s'\n", ast_channel_name(c));
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

	if (!(ast_channel_fin(chan) & DEBUGCHAN_FLAG) || !(ast_channel_fout(chan) & DEBUGCHAN_FLAG)) {
		if (args->is_off) {
			ast_channel_fin_set(chan, ast_channel_fin(chan) & ~DEBUGCHAN_FLAG);
			ast_channel_fout_set(chan, ast_channel_fout(chan) & ~DEBUGCHAN_FLAG);
		} else {
			ast_channel_fin_set(chan, ast_channel_fin(chan) | DEBUGCHAN_FLAG);
			ast_channel_fout_set(chan, ast_channel_fout(chan) | DEBUGCHAN_FLAG);
		}
		ast_cli(args->fd, "Debugging %s on channel %s\n", args->is_off ? "disabled" : "enabled",
				ast_channel_name(chan));
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
	struct ast_channel *chan;
	struct timeval now;
	char cdrtime[256];
	struct ast_str *obuf;/*!< Buffer for CDR variables. */
	struct ast_str *output;/*!< Accumulation buffer for all output. */
	long elapsed_seconds=0;
	int hour=0, min=0, sec=0;
	struct ast_var_t *var;
	struct ast_str *write_transpath = ast_str_alloca(256);
	struct ast_str *read_transpath = ast_str_alloca(256);
	struct ast_str *codec_buf = ast_str_alloca(64);
	struct ast_bridge *bridge;
	struct ast_callid *callid;
	char callid_buf[32];

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

	obuf = ast_str_thread_get(&ast_str_thread_global_buf, 16);
	if (!obuf) {
		return CLI_FAILURE;
	}

	output = ast_str_create(8192);
	if (!output) {
		return CLI_FAILURE;
	}

	chan = ast_channel_get_by_name(a->argv[3]);
	if (!chan) {
		ast_cli(a->fd, "%s is not a known channel\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	now = ast_tvnow();
	ast_channel_lock(chan);

	if (!ast_tvzero(ast_channel_creationtime(chan))) {
		elapsed_seconds = now.tv_sec - ast_channel_creationtime(chan).tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
		snprintf(cdrtime, sizeof(cdrtime), "%dh%dm%ds", hour, min, sec);
	} else {
		strcpy(cdrtime, "N/A");
	}

	ast_translate_path_to_str(ast_channel_writetrans(chan), &write_transpath);
	ast_translate_path_to_str(ast_channel_readtrans(chan), &read_transpath);

	bridge = ast_channel_get_bridge(chan);
	callid_buf[0] = '\0';
	callid = ast_channel_callid(chan);
	if (callid) {
		ast_callid_strnprint(callid_buf, sizeof(callid_buf), callid);
		ast_callid_unref(callid);
	}

	ast_str_append(&output, 0,
		" -- General --\n"
		"           Name: %s\n"
		"           Type: %s\n"
		"       UniqueID: %s\n"
		"       LinkedID: %s\n"
		"      Caller ID: %s\n"
		" Caller ID Name: %s\n"
		"Connected Line ID: %s\n"
		"Connected Line ID Name: %s\n"
		"Eff. Connected Line ID: %s\n"
		"Eff. Connected Line ID Name: %s\n"
		"    DNID Digits: %s\n"
		"       Language: %s\n"
		"          State: %s (%u)\n"
		"  NativeFormats: %s\n"
		"    WriteFormat: %s\n"
		"     ReadFormat: %s\n"
		" WriteTranscode: %s %s\n"
		"  ReadTranscode: %s %s\n"
		" Time to Hangup: %ld\n"
		"   Elapsed Time: %s\n"
		"      Bridge ID: %s\n"
		" --   PBX   --\n"
		"        Context: %s\n"
		"      Extension: %s\n"
		"       Priority: %d\n"
		"     Call Group: %llu\n"
		"   Pickup Group: %llu\n"
		"    Application: %s\n"
		"           Data: %s\n"
		" Call Identifer: %s\n",
		ast_channel_name(chan),
		ast_channel_tech(chan)->type,
		ast_channel_uniqueid(chan),
		ast_channel_linkedid(chan),
		S_COR(ast_channel_caller(chan)->id.number.valid,
		      ast_channel_caller(chan)->id.number.str, "(N/A)"),
		S_COR(ast_channel_caller(chan)->id.name.valid,
		      ast_channel_caller(chan)->id.name.str, "(N/A)"),
		S_COR(ast_channel_connected(chan)->id.number.valid,
		      ast_channel_connected(chan)->id.number.str, "(N/A)"),
		S_COR(ast_channel_connected(chan)->id.name.valid,
		      ast_channel_connected(chan)->id.name.str, "(N/A)"),
		S_COR(ast_channel_connected_effective_id(chan).number.valid,
		      ast_channel_connected_effective_id(chan).number.str, "(N/A)"),
		S_COR(ast_channel_connected_effective_id(chan).name.valid,
		      ast_channel_connected_effective_id(chan).name.str, "(N/A)"),
		S_OR(ast_channel_dialed(chan)->number.str, "(N/A)"),
		ast_channel_language(chan),
		ast_state2str(ast_channel_state(chan)),
		ast_channel_state(chan),
		ast_format_cap_get_names(ast_channel_nativeformats(chan), &codec_buf),
		ast_format_get_name(ast_channel_writeformat(chan)),
		ast_format_get_name(ast_channel_readformat(chan)),
		ast_str_strlen(write_transpath) ? "Yes" : "No",
		ast_str_buffer(write_transpath),
		ast_str_strlen(read_transpath) ? "Yes" : "No",
		ast_str_buffer(read_transpath),
		ast_channel_whentohangup(chan)->tv_sec,
		cdrtime,
		bridge ? bridge->uniqueid : "(Not bridged)",
		ast_channel_context(chan),
		ast_channel_exten(chan),
		ast_channel_priority(chan),
		ast_channel_callgroup(chan),
		ast_channel_pickupgroup(chan),
		S_OR(ast_channel_appl(chan), "(N/A)"),
		S_OR(ast_channel_data(chan), "(Empty)"),
		S_OR(callid_buf, "(None)")
		);
	ast_str_append(&output, 0, "      Variables:\n");

	AST_LIST_TRAVERSE(ast_channel_varshead(chan), var, entries) {
		ast_str_append(&output, 0, "%s=%s\n", ast_var_name(var), ast_var_value(var));
	}

	if (!(ast_channel_tech(chan)->properties & AST_CHAN_TP_INTERNAL)
		&& ast_cdr_serialize_variables(ast_channel_name(chan), &obuf, '=', '\n')) {
		ast_str_append(&output, 0, "  CDR Variables:\n%s\n", ast_str_buffer(obuf));
	}

	ast_channel_unlock(chan);

	ast_cli(a->fd, "%s", ast_str_buffer(output));
	ast_free(output);

	ao2_cleanup(bridge);
	ast_channel_unref(chan);

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
	int wordlen = strlen(word), which = 0;
	RAII_VAR(struct ao2_container *, cached_channels, NULL, ao2_cleanup);
	char *ret = NULL;
	struct ao2_iterator iter;
	struct stasis_message *msg;

	if (pos != rpos) {
		return NULL;
	}

	if (!(cached_channels = stasis_cache_dump(ast_channel_cache(), ast_channel_snapshot_type()))) {
		return NULL;
	}

	iter = ao2_iterator_init(cached_channels, 0);
	for (; (msg = ao2_iterator_next(&iter)); ao2_ref(msg, -1)) {
		struct ast_channel_snapshot *snapshot = stasis_message_data(msg);

		if (!strncasecmp(word, snapshot->name, wordlen) && (++which > state)) {
			ret = ast_strdup(snapshot->name);
			ao2_ref(msg, -1);
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	return ret;
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
			ast_cli(a->fd, FORMAT_STRING, ast_channel_name(gi->chan), gi->group, (ast_strlen_zero(gi->category) ? "(default)" : gi->category));
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

static char *handle_cli_wait_fullybooted(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "core waitfullybooted";
		e->usage =
			"Usage: core waitfullybooted\n"
			"	Wait until Asterisk has fully booted.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	while (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		usleep(100);
	}

	ast_cli(a->fd, "Asterisk has fully booted.\n");

	return CLI_SUCCESS;
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

	AST_CLI_DEFINE(handle_debug, "Set level of debug chattiness"),
	AST_CLI_DEFINE(handle_verbose, "Set level of verbose chattiness"),

	AST_CLI_DEFINE(group_show_channels, "Display active channels with group(s)"),

	AST_CLI_DEFINE(handle_help, "Display help list, or specific help on a command"),

	AST_CLI_DEFINE(handle_logger_mute, "Toggle logging output to a console"),

	AST_CLI_DEFINE(handle_modlist, "List modules and info"),

	AST_CLI_DEFINE(handle_load, "Load a module by name"),

	AST_CLI_DEFINE(handle_reload, "Reload configuration for a module"),

	AST_CLI_DEFINE(handle_core_reload, "Global reload"),

	AST_CLI_DEFINE(handle_unload, "Unload a module by name"),

	AST_CLI_DEFINE(handle_showuptime, "Show uptime information"),

	AST_CLI_DEFINE(handle_softhangup, "Request a hangup on a given channel"),

	AST_CLI_DEFINE(handle_cli_reload_permissions, "Reload CLI permissions config"),

	AST_CLI_DEFINE(handle_cli_show_permissions, "Show CLI permissions"),

	AST_CLI_DEFINE(handle_cli_check_permissions, "Try a permissions config for a user"),

	AST_CLI_DEFINE(handle_cli_wait_fullybooted, "Wait for Asterisk to be fully booted"),
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

static void cli_shutdown(void)
{
	ast_cli_unregister_multiple(cli_cli, ARRAY_LEN(cli_cli));
}

/*! \brief initialize the _full_cmd string in * each of the builtins. */
void ast_builtins_init(void)
{
	ast_cli_register_multiple(cli_cli, ARRAY_LEN(cli_cli));
	ast_register_atexit(cli_shutdown);
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
	l = strlen(cmd);
	/* wildcard match - will extend in the future */
	if (l > 0 && cli_word[0] == '%') {
		return 1;	/* wildcard */
	}

	/* Start a search for the command entered against the cli word in question */
	pos = strcasestr(cli_word, cmd);
	while (pos) {

		/*
		 *Check if the word matched with is surrounded by reserved characters on both sides
		 * and isn't at the beginning of the cli_word since that would make it check in a location we shouldn't know about.
		 * If it is surrounded by reserved chars and isn't at the beginning, it's a match.
		 */
		if (pos != cli_word && strchr(cli_rsvd, pos[-1]) && strchr(cli_rsvd, pos[l])) {
			return 1;	/* valid match */
		}

		/* Ok, that one didn't match, strcasestr to the next appearance of the command and start over.*/
		pos = strcasestr(pos + 1, cmd);
	}
	/* If no matches were found over the course of the while loop, we hit the end of the string. It's a mismatch. */
	return -1;
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
 *			    note that trailing optional arguments are skipped.
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

static int cli_is_registered(struct ast_cli_entry *e)
{
	struct ast_cli_entry *cur = NULL;

	while ((cur = cli_next(cur))) {
		if (cur == e) {
			return 1;
		}
	}
	return 0;
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

	AST_RWLIST_WRLOCK(&helpers);

	if (cli_is_registered(e)) {
		ast_log(LOG_WARNING, "Command '%s' already registered (the same ast_cli_entry)\n",
			S_OR(e->_full_cmd, e->command));
		ret = 0;  /* report success */
		goto done;
	}

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

	if (find_cli(e->cmda, 1)) {
		ast_log(LOG_WARNING, "Command '%s' already registered (or something close enough)\n",
			S_OR(e->_full_cmd, e->command));
		goto done;
	}
	if (set_full_cmd(e)) {
		ast_log(LOG_WARNING, "Error registering CLI Command '%s'\n",
			S_OR(e->_full_cmd, e->command));
		goto done;
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
	if (ret) {
		ast_free(e->command);
		e->command = NULL;
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
		ast_cli(fd, "%-30s -- %s\n", e->_full_cmd,
			S_OR(e->summary, "<no description available>"));
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

	/* Remove leading spaces from the command */
	while (isspace(*s)) {
		cur++;
		s++;
	}

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

static void destroy_match_list(char **match_list, int matches)
{
	if (match_list) {
		int idx;

		for (idx = 1; idx < matches; ++idx) {
			ast_free(match_list[idx]);
		}
		ast_free(match_list);
	}
}

char **ast_cli_completion_matches(const char *text, const char *word)
{
	char **match_list = NULL, *retstr, *prevstr;
	char **new_list;
	size_t match_list_len, max_equal, which, i;
	int matches = 0;

	/* leave entry 0 free for the longest common substring */
	match_list_len = 1;
	while ((retstr = ast_cli_generator(text, word, matches)) != NULL) {
		if (matches + 1 >= match_list_len) {
			match_list_len <<= 1;
			new_list = ast_realloc(match_list, match_list_len * sizeof(*match_list));
			if (!new_list) {
				destroy_match_list(match_list, matches);
				return NULL;
			}
			match_list = new_list;
		}
		match_list[++matches] = retstr;
	}

	if (!match_list) {
		return match_list; /* NULL */
	}

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

	retstr = ast_malloc(max_equal + 1);
	if (!retstr) {
		destroy_match_list(match_list, matches);
		return NULL;
	}
	ast_copy_string(retstr, match_list[1], max_equal + 1);
	match_list[0] = retstr;

	/* ensure that the array is NULL terminated */
	if (matches + 1 >= match_list_len) {
		new_list = ast_realloc(match_list, (match_list_len + 1) * sizeof(*match_list));
		if (!new_list) {
			ast_free(retstr);
			destroy_match_list(match_list, matches);
			return NULL;
		}
		match_list = new_list;
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
