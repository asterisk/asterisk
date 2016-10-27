/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, CFWare, LLC
 *
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
 * \brief PBX switch routines.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/pbx.h"
#include "pbx_private.h"

static AST_RWLIST_HEAD_STATIC(switches, ast_switch);

struct ast_switch *pbx_findswitch(const char *sw)
{
	struct ast_switch *asw;

	AST_RWLIST_RDLOCK(&switches);
	AST_RWLIST_TRAVERSE(&switches, asw, list) {
		if (!strcasecmp(asw->name, sw))
			break;
	}
	AST_RWLIST_UNLOCK(&switches);

	return asw;
}

/*
 * Append to the list. We don't have a tail pointer because we need
 * to scan the list anyways to check for duplicates during insertion.
 */
int ast_register_switch(struct ast_switch *sw)
{
	struct ast_switch *tmp;

	AST_RWLIST_WRLOCK(&switches);
	AST_RWLIST_TRAVERSE(&switches, tmp, list) {
		if (!strcasecmp(tmp->name, sw->name)) {
			AST_RWLIST_UNLOCK(&switches);
			ast_log(LOG_WARNING, "Switch '%s' already found\n", sw->name);
			return -1;
		}
	}
	AST_RWLIST_INSERT_TAIL(&switches, sw, list);
	AST_RWLIST_UNLOCK(&switches);

	return 0;
}

void ast_unregister_switch(struct ast_switch *sw)
{
	AST_RWLIST_WRLOCK(&switches);
	AST_RWLIST_REMOVE(&switches, sw, list);
	AST_RWLIST_UNLOCK(&switches);
}

/*! \brief  handle_show_switches: CLI support for listing registered dial plan switches */
static char *handle_show_switches(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_switch *sw;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show switches";
		e->usage =
			"Usage: core show switches\n"
			"       List registered switches\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_RWLIST_RDLOCK(&switches);

	if (AST_RWLIST_EMPTY(&switches)) {
		AST_RWLIST_UNLOCK(&switches);
		ast_cli(a->fd, "There are no registered alternative switches\n");
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "\n    -= Registered Asterisk Alternative Switches =-\n");
	AST_RWLIST_TRAVERSE(&switches, sw, list)
		ast_cli(a->fd, "%s: %s\n", sw->name, sw->description);

	AST_RWLIST_UNLOCK(&switches);

	return CLI_SUCCESS;
}

static struct ast_cli_entry sw_cli[] = {
	AST_CLI_DEFINE(handle_show_switches, "Show alternative switches"),
};

static void unload_pbx_switch(void)
{
	ast_cli_unregister_multiple(sw_cli, ARRAY_LEN(sw_cli));
}

int load_pbx_switch(void)
{
	ast_cli_register_multiple(sw_cli, ARRAY_LEN(sw_cli));
	ast_register_cleanup(unload_pbx_switch);

	return 0;
}
