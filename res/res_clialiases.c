/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief CLI Aliases
 *
 * \author\verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 * 
 * This module provides the capability to create aliases to other
 * CLI commands.
 */

/*! \li \ref res_clialiases.c uses the configuration file \ref cli_aliases.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page cli_aliases.conf cli_aliases.conf
 * \verbinclude cli_aliases.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"

/*! Maximum number of buckets for CLI aliases */
#define MAX_ALIAS_BUCKETS 53

/*! Configuration file used for this application */
static const char config_file[] = "cli_aliases.conf";

struct cli_alias {
	struct ast_cli_entry cli_entry; /*!< Actual CLI structure used for this alias */
	char *alias;                    /*!< CLI Alias */
	char *real_cmd;                 /*!< Actual CLI command it is aliased to */
};

static struct ao2_container *cli_aliases;

/*! \brief Hashing function used for aliases */
static int alias_hash_cb(const void *obj, const int flags)
{
	const struct cli_alias *alias = obj;
	return ast_str_hash(alias->cli_entry.command);
}

/*! \brief Comparison function used for aliases */
static int alias_cmp_cb(void *obj, void *arg, int flags)
{
	const struct cli_alias *alias0 = obj, *alias1 = arg;

	return (alias0->cli_entry.command == alias1->cli_entry.command ? CMP_MATCH | CMP_STOP : 0);
}

/*! \brief Callback for unregistering an alias */
static int alias_unregister_cb(void *obj, void *arg, int flags)
{
	struct cli_alias *alias = obj;

	/* Unregister the CLI entry from the core */
	ast_cli_unregister(&alias->cli_entry);

	/* We can determine if this worked or not by looking at the cli_entry itself */
	return !alias->cli_entry.command ? CMP_MATCH : 0;
}

/*! \brief Callback for finding an alias based on name */
static int alias_name_cb(void *obj, void *arg, int flags)
{
	struct cli_alias *alias = obj;
	char *name = arg;

	return !strcmp(alias->alias, name) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Function which passes through an aliased CLI command to the real one */
static char *cli_alias_passthrough(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cli_alias *alias;
	struct cli_alias tmp = {
		.cli_entry.command = e->command,
	};
	char *generator;
	const char *line;

	/* Try to find the alias based on the CLI entry */
	if (!(alias = ao2_find(cli_aliases, &tmp, OBJ_POINTER))) {
		return 0;
	}

	switch (cmd) {
	case CLI_INIT:
		ao2_ref(alias, -1);
		return NULL;
	case CLI_GENERATE:
		line = a->line;
		line += (strlen(alias->alias));
		if (!strncasecmp(alias->alias, alias->real_cmd, strlen(alias->alias))) {
			generator = NULL;
		} else if (!ast_strlen_zero(a->word)) {
			struct ast_str *real_cmd = ast_str_alloca(strlen(alias->real_cmd) + strlen(line) + 1);
			ast_str_append(&real_cmd, 0, "%s%s", alias->real_cmd, line);
			generator = ast_cli_generator(ast_str_buffer(real_cmd), a->word, a->n);
		} else {
			generator = ast_cli_generator(alias->real_cmd, a->word, a->n);
		}
		ao2_ref(alias, -1);
		return generator;
	}

	/* If they gave us extra arguments we need to construct a string to pass in */
	if (a->argc != e->args) {
		struct ast_str *real_cmd = ast_str_alloca(2048);
		int i;

		ast_str_append(&real_cmd, 0, "%s", alias->real_cmd);

		/* Add the additional arguments that have been passed in */
		for (i = e->args + 1; i <= a->argc; i++) {
			ast_str_append(&real_cmd, 0, " %s", a->argv[i - 1]);
		}

		ast_cli_command(a->fd, ast_str_buffer(real_cmd));
	} else {
		ast_cli_command(a->fd, alias->real_cmd);
	}

	ao2_ref(alias, -1);

	return CLI_SUCCESS;
}

/*! \brief CLI Command to display CLI Aliases */
static char *alias_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-50.50s %-50.50s\n"
	struct cli_alias *alias;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "cli show aliases";
		e->usage =
			"Usage: cli show aliases\n"
			"       Displays a list of aliased CLI commands.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, FORMAT, "Alias Command", "Real Command");

	i = ao2_iterator_init(cli_aliases, 0);
	for (; (alias = ao2_iterator_next(&i)); ao2_ref(alias, -1)) {
		ast_cli(a->fd, FORMAT, alias->alias, alias->real_cmd);
	}
	ao2_iterator_destroy(&i);

	return CLI_SUCCESS;
#undef FORMAT
}

/*! \brief CLI commands to interact with things */
static struct ast_cli_entry cli_alias[] = {
	AST_CLI_DEFINE(alias_show, "Show CLI command aliases"),
};

/*! \brief Function called to load or reload the configuration file */
static void load_config(int reload)
{
	struct ast_config *cfg = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct cli_alias *alias;
	struct ast_variable *v, *v1;

	if (!(cfg = ast_config_load(config_file, config_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "res_clialiases configuration file '%s' not found\n", config_file);
		return;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return;
	}

	/* Destroy any existing CLI aliases */
	if (reload) {
		ao2_callback(cli_aliases, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, alias_unregister_cb, NULL);
	}

	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		if (strcmp(v->name, "template")) {
			ast_log(LOG_WARNING, "%s is not a correct option in [%s]\n", v->name, "general");
			continue;
		}
		/* Read in those there CLI aliases */
		for (v1 = ast_variable_browse(cfg, v->value); v1; v1 = v1->next) {
			struct cli_alias *existing = ao2_callback(cli_aliases, 0, alias_name_cb, (char*)v1->name);

			if (existing) {
				ast_log(LOG_WARNING, "Alias '%s' could not be unregistered and has been retained\n",
					existing->alias);
				ao2_ref(existing, -1);
				continue;
			}

			if (!(alias = ao2_alloc((sizeof(*alias) + strlen(v1->name) + strlen(v1->value) + 2), NULL))) {
				continue;
			}
			alias->alias = ((char *) alias) + sizeof(*alias);
			alias->real_cmd = ((char *) alias->alias) + strlen(v1->name) + 1;
			strcpy(alias->alias, v1->name);
			strcpy(alias->real_cmd, v1->value);
			alias->cli_entry.handler = cli_alias_passthrough;
			alias->cli_entry.command = alias->alias;
			alias->cli_entry.usage = "Aliased CLI Command\n";

			if (ast_cli_register(&alias->cli_entry)) {
				ao2_ref(alias, -1);
				continue;
			}
			ao2_link(cli_aliases, alias);
			ast_verb(2, "Aliased CLI command '%s' to '%s'\n", v1->name, v1->value);
			ao2_ref(alias, -1);
		}
	}

	ast_config_destroy(cfg);

	return;
}

/*! \brief Function called to reload the module */
static int reload_module(void)
{
	load_config(1);
	return 0;
}

/*! \brief Function called to unload the module */
static int unload_module(void)
{
	ao2_callback(cli_aliases, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, alias_unregister_cb, NULL);

	if (ao2_container_count(cli_aliases)) {
		ast_log(LOG_ERROR, "Could not unregister all CLI aliases\n");
		return -1;
	}

	ao2_ref(cli_aliases, -1);

	ast_cli_unregister_multiple(cli_alias, ARRAY_LEN(cli_alias));

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (!(cli_aliases = ao2_container_alloc(MAX_ALIAS_BUCKETS, alias_hash_cb, alias_cmp_cb))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	load_config(0);

	ast_cli_register_multiple(cli_alias, ARRAY_LEN(cli_alias));

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "CLI Aliases",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		);
