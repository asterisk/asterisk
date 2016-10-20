/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Stasis CLI commands.
 *
 * \author Matt Jordan <mjordan@digium.com>
 */

#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/astobj2.h"

#include "cli.h"
#include "app.h"


static char *ari_show_apps(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *apps;
	struct ao2_iterator it_apps;
	char *app;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show apps";
		e->usage =
			"Usage: ari show apps\n"
			"       Lists all registered applications.\n"
			;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	apps = stasis_app_get_all();
	if (!apps) {
		ast_cli(a->fd, "Unable to retrieve registered applications!\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Application Name         \n");
	ast_cli(a->fd, "=========================\n");
	it_apps = ao2_iterator_init(apps, 0);
	while ((app = ao2_iterator_next(&it_apps))) {
		ast_cli(a->fd, "%-25.25s\n", app);
		ao2_ref(app, -1);
	}

	ao2_iterator_destroy(&it_apps);
	ao2_ref(apps, -1);

	return CLI_SUCCESS;
}

struct app_complete {
	/*! Nth app to search for */
	int state;
	/*! Which app currently on */
	int which;
};

static int complete_ari_app_search(void *obj, void *arg, void *data, int flags)
{
	struct app_complete *search = data;

	if (++search->which > search->state) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_ari_app(struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, apps, stasis_app_get_all(), ao2_cleanup);
	RAII_VAR(char *, app, NULL, ao2_cleanup);

	struct app_complete search = {
		.state = a->n,
	};

	if (!apps) {
		ast_cli(a->fd, "Error getting ARI applications\n");
		return CLI_FAILURE;
	}

	app = ao2_callback_data(apps,
		ast_strlen_zero(a->word) ? 0 : OBJ_PARTIAL_KEY,
		complete_ari_app_search, (char*)a->word, &search);

	return app ? ast_strdup(app) : NULL;
}

static char *complete_ari_show_app(struct ast_cli_args *a)
{
	if (a->pos == 3) {
		return complete_ari_app(a);
	}

	return NULL;
}

static char *ari_show_app(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	void *app;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari show app";
		e->usage =
			"Usage: ari show app <application>\n"
			"       Provide detailed information about a registered application.\n"
			;
		return NULL;
	case CLI_GENERATE:
		return complete_ari_show_app(a);
	default:
		break;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	app = stasis_app_get_by_name(a->argv[3]);
	if (!app) {
		return CLI_FAILURE;
	}

	app_to_cli(app, a);

	ao2_ref(app, -1);

	return CLI_SUCCESS;
}

static char *ari_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	void *app;
	int debug;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ari set debug";
		e->usage =
			"Usage: ari set debug <application> <on|off>\n"
			"       Enable or disable debugging on a specific application.\n"
			;
		return NULL;
	case CLI_GENERATE:
		return complete_ari_show_app(a);
	default:
		break;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	app = stasis_app_get_by_name(a->argv[3]);
	if (!app) {
		return CLI_FAILURE;
	}

	debug = !strcmp(a->argv[4], "on");
	app_set_debug(app, debug);
	ast_cli(a->fd, "Debugging on '%s' %s\n",
		app_name(app),
		debug ? "enabled" : "disabled");

	ao2_ref(app, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_ari[] = {
	AST_CLI_DEFINE(ari_show_apps, "List registered ARI applications"),
	AST_CLI_DEFINE(ari_show_app, "Display details of a registered ARI application"),
	AST_CLI_DEFINE(ari_set_debug, "Enable/disable debugging of an ARI application"),
};


int cli_init(void)
{
	return ast_cli_register_multiple(cli_ari, ARRAY_LEN(cli_ari));
}

void cli_cleanup(void)
{
	ast_cli_unregister_multiple(cli_ari, ARRAY_LEN(cli_ari));
}
