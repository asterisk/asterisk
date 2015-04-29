/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2015, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 * Luigi Rizzo <rizzo@icir.org>
 * Corey Farrell <git@cfware.com>
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
 * \brief Module Loader CLI
 * \author Mark Spencer <markster@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Luigi Rizzo <rizzo@icir.org>
 * \author Corey Farrell <git@cfware.com>
 * - See ModMngMnt
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"

#include "asterisk/cli.h"
#include "asterisk/module.h"

#include "module_private.h"

static char *handle_modlist(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define MODLIST_FORMAT  "%-30s %-40.40s %-10d %-11s %13s\n"
#define MODLIST_FORMAT2 "%-30s %-40.40s %-10s %-11s %13s\n"
	const char *like;
	int i;
	int c = 0;
	struct ast_module *module;

	switch (cmd) {
	case CLI_INIT:
		e->command = "module show [like]";
		e->usage =
			"Usage: module show [like keyword]\n"
			"       Shows Asterisk modules currently in use, and usage statistics.\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos == e->args) {
			return ast_module_complete(a->line, a->word, a->pos, a->n, a->pos, AST_MODULE_COMPLETE_LOADED);
		}
		return NULL;
	}

	/* all the above return, so we proceed with the handler.
	 * we are guaranteed to have argc >= e->args
	 */
	if (a->argc == e->args - 1) {
		like = "";
	} else if (a->argc == e->args + 1 && !strcasecmp(a->argv[e->args-1], "like")) {
		like = a->argv[e->args];
	} else {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, MODLIST_FORMAT2, "Module", "Description", "Use Count", "Status", "Support Level");
	AST_VECTOR_RW_RDLOCK(&modules_loaded);
	for (i = 0; i < AST_VECTOR_SIZE(&modules_loaded); i++) {
		const char *name;

		module = AST_VECTOR_GET(&modules_loaded, i);
		name = ast_module_name(module);

		if (strcasestr(name, like)) {
			ast_cli(a->fd, MODLIST_FORMAT,
				name,
				ast_module_description(module),
				ast_module_instance_refs(module),
				ast_module_is_running(module) ? "Running" : "Not Running",
				ast_module_support_level_to_string(module)
			);
			c++;
		}
	}
	AST_VECTOR_RW_UNLOCK(&modules_loaded);

	ast_cli(a->fd,"%d modules loaded\n", c);

	return CLI_SUCCESS;
#undef MODLIST_FORMAT
#undef MODLIST_FORMAT2
}

static char *handle_load(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_module *module;

	/* "module load <mod>" */
	switch (cmd) {
	case CLI_INIT:
		e->command = "module load";
		e->usage =
			"Usage: module load <module name>\n"
			"       Loads the specified module into Asterisk.\n";
		return NULL;

	case CLI_GENERATE:
		if (a->pos != e->args) {
			return NULL;
		}
		return ast_module_complete(a->line, a->word, a->pos, a->n, a->pos, AST_MODULE_COMPLETE_CANLOAD);
	}

	if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	module = ast_module_find(a->argv[e->args]);
	if (!module) {
		ast_cli(a->fd, "Module %s not found\n", a->argv[e->args]);
		return CLI_FAILURE;
	}

	if (ast_module_load(module)) {
		ast_cli(a->fd, "Unable to load module %s\n", a->argv[e->args]);
		ao2_ref(module, -1);
		return CLI_FAILURE;
	}
	ao2_ref(module, -1);

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
		return ast_module_complete(a->line, a->word, a->pos, a->n, a->pos, AST_MODULE_COMPLETE_RELOADABLE);
	}

	if (a->argc == e->args) {
		ast_module_reload(NULL);
		return CLI_SUCCESS;
	}

	for (x = e->args; x < a->argc; x++) {
		enum ast_module_reload_result res = AST_MODULE_RELOAD_NOT_FOUND;
		struct ast_module *module = ast_module_find(a->argv[x]);

		if (module) {
			res = ast_module_reload(module);
		}

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

static char *handle_unload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	/* "module unload mod_1 [mod_2 .. mod_N]" */
	int x;
	int force = 0;
	const char *s;

	switch (cmd) {
	case CLI_INIT:
		e->command = "module unload";
		e->usage =
			"Usage: module unload [-f] <module_1> [<module_2> ... ]\n"
			"       Unloads the specified module from Asterisk. The -f\n"
			"       option attempts to hangup all channels and stop users\n"
			"       of the module.\n"
			"       module to be unloaded even if the module says it cannot, \n"
			"       which almost always will cause a crash.\n";
		return NULL;

	case CLI_GENERATE:
		/* BUGBUG: use AST_MODULE_COMPLETE_LOADED for '-f' */
		return ast_module_complete(a->line, a->word, a->pos, a->n, a->pos, AST_MODULE_COMPLETE_ADMINLOADED);
	}

	if (a->argc < e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	/* first argument */
	x = e->args;
	s = a->argv[x];

	if (s[0] == '-') {
		if (s[1] == 'f') {
			force = 1;
		} else {
			return CLI_SHOWUSAGE;
		}
		if (a->argc < e->args + 2) {
			/* need at least one module name */
			return CLI_SHOWUSAGE;
		}
		x++;	/* skip this argument */
	}

	for (; x < a->argc; x++) {
		struct ast_module *module = ast_module_find(a->argv[x]);

		if (!module) {
			ast_cli(a->fd, "Module %s not found\n", a->argv[x]);
			return CLI_FAILURE;
		}

		ast_module_unload(module, force);
		ast_cli(a->fd, "Unload requested for %s\n", a->argv[x]);
		ao2_ref(module, -1);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry module_cli[] = {
	AST_CLI_DEFINE(handle_modlist,     "List modules and info"),
	AST_CLI_DEFINE(handle_load,        "Load a module by name"),
	AST_CLI_DEFINE(handle_reload,      "Reload configuration for a module"),
	AST_CLI_DEFINE(handle_core_reload, "Global reload"),
	AST_CLI_DEFINE(handle_unload,      "Unload a module by name"),
};

static void module_cli_cleanup(void)
{
	ast_cli_unregister_multiple(module_cli, ARRAY_LEN(module_cli));
}

/* BUGBUG: add to header and run at startup. */
int module_cli_init(void)
{
	ast_cli_register_multiple(module_cli, ARRAY_LEN(module_cli));
	ast_register_cleanup(module_cli_cleanup);

	return 0;
}
