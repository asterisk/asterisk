/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com> 
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
 * \brief Manually controlled blinky lights
 *
 * \author Russell Bryant <russell@digium.com> 
 *
 * \ingroup functions
 *
 * \note Props go out to Ahrimanes in \#asterisk for requesting this at 4:30 AM
 *       when I couldn't sleep.  :)
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"
#include "asterisk/cli.h"
#include "asterisk/astdb.h"
#include "asterisk/app.h"

static const char astdb_family[] = "CustomDevstate";

static int devstate_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	ast_copy_string(buf, ast_devstate_str(ast_device_state(data)), len);

	return 0;
}

static int devstate_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	size_t len = strlen("Custom:");
	enum ast_device_state state_val;

	if (strncasecmp(data, "Custom:", len)) {
		ast_log(LOG_WARNING, "The DEVICE_STATE function can only be used to set 'Custom:' device state!\n");
		return -1;
	}
	data += len;
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "DEVICE_STATE function called with no custom device name!\n");
		return -1;
	}

	state_val = ast_devstate_val(value);

	if (state_val == AST_DEVICE_UNKNOWN) {
		ast_log(LOG_ERROR, "DEVICE_STATE function given invalid state value '%s'\n", value);
		return -1;
	}

	ast_db_put(astdb_family, data, value);

	ast_devstate_changed(state_val, "Custom:%s", data);

	return 0;
}

enum {
	HINT_OPT_NAME = (1 << 0),
};

AST_APP_OPTIONS(hint_options, BEGIN_OPTIONS
	AST_APP_OPTION('n', HINT_OPT_NAME),
END_OPTIONS );

static int hint_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *exten, *context;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(exten);
		AST_APP_ARG(options);
	);
	struct ast_flags opts = { 0, };
	int res;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "The HINT function requires an extension\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.exten)) {
		ast_log(LOG_WARNING, "The HINT function requires an extension\n");
		return -1;
	}

	context = exten = args.exten;
	strsep(&context, "@");
	if (ast_strlen_zero(context))
		context = "default";

	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(hint_options, &opts, NULL, args.options);

	if (ast_test_flag(&opts, HINT_OPT_NAME))
		res = ast_get_hint(NULL, 0, buf, len, chan, context, exten);
	else
		res = ast_get_hint(buf, len, NULL, 0, chan, context, exten);

	return !res; /* ast_get_hint returns non-zero on success */
}

static enum ast_device_state custom_devstate_callback(const char *data)
{
	char buf[256] = "";

	ast_db_get(astdb_family, data, buf, sizeof(buf));

	return ast_devstate_val(buf);
}

static char *handle_cli_funcdevstate_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_db_entry *db_entry, *db_tree;

	switch (cmd) {
	case CLI_INIT:
		e->command = "funcdevstate list";
		e->usage =
			"Usage: funcdevstate list\n"
			"       List all custom device states that have been set by using\n"
			"       the DEVICE_STATE dialplan function.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\n"
	        "---------------------------------------------------------------------\n"
	        "--- Custom Device States --------------------------------------------\n"
	        "---------------------------------------------------------------------\n"
	        "---\n");

	db_entry = db_tree = ast_db_gettree(astdb_family, NULL);
	for (; db_entry; db_entry = db_entry->next) {
		const char *dev_name = strrchr(db_entry->key, '/') + 1;
		if (dev_name <= (const char *) 1)
			continue;
		ast_cli(a->fd, "--- Name: 'Custom:%s'  State: '%s'\n"
		               "---\n", dev_name, db_entry->data);
	}
	ast_db_freetree(db_tree);
	db_tree = NULL;

	ast_cli(a->fd,
	        "---------------------------------------------------------------------\n"
	        "---------------------------------------------------------------------\n"
	        "\n");

	return CLI_SUCCESS;
}

static char *handle_cli_devstate_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_db_entry *db_entry, *db_tree;

	switch (cmd) {
	case CLI_INIT:
		e->command = "devstate list";
		e->usage =
			"Usage: devstate list\n"
			"       List all custom device states that have been set by using\n"
			"       the DEVICE_STATE dialplan function.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\n"
	        "---------------------------------------------------------------------\n"
	        "--- Custom Device States --------------------------------------------\n"
	        "---------------------------------------------------------------------\n"
	        "---\n");

	db_entry = db_tree = ast_db_gettree(astdb_family, NULL);
	for (; db_entry; db_entry = db_entry->next) {
		const char *dev_name = strrchr(db_entry->key, '/') + 1;
		if (dev_name <= (const char *) 1)
			continue;
		ast_cli(a->fd, "--- Name: 'Custom:%s'  State: '%s'\n"
		               "---\n", dev_name, db_entry->data);
	}
	ast_db_freetree(db_tree);
	db_tree = NULL;

	ast_cli(a->fd,
	        "---------------------------------------------------------------------\n"
	        "---------------------------------------------------------------------\n"
	        "\n");

	return CLI_SUCCESS;
}

static char *handle_cli_devstate_change(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
    size_t len;
	const char *dev, *state;
	enum ast_device_state state_val;

	switch (cmd) {
	case CLI_INIT:
		e->command = "devstate change";
		e->usage =
			"Usage: devstate change <device> <state>\n"
			"       Change a custom device to a new state.\n"
			"       The possible values for the state are:\n"
			"UNKNOWN | NOT_INUSE | INUSE | BUSY | INVALID | UNAVAILABLE | RINGING\n"
			"RINGINUSE | ONHOLD\n",
			"\n"
			"Examples:\n"
			"       devstate change Custom:mystate1 INUSE\n"
			"       devstate change Custom:mystate1 NOT_INUSE\n"
			"       \n";
		return NULL;
	case CLI_GENERATE:
	{
		static char * const cmds[] = { "UNKNOWN", "NOT_INUSE", "INUSE", "BUSY",
			"UNAVAILALBE", "RINGING", "RINGINUSE", "ONHOLD", NULL };

		if (a->pos == e->args + 1)
			return ast_cli_complete(a->word, cmds, a->n);

		return NULL;
	}
	}

	if (a->argc != e->args + 2)
		return CLI_SHOWUSAGE;

	len = strlen("Custom:");
	dev = a->argv[e->args];
	state = a->argv[e->args + 1];

	if (strncasecmp(dev, "Custom:", len)) {
		ast_cli(a->fd, "The devstate command can only be used to set 'Custom:' device state!\n");
		return CLI_FAILURE;
	}

	dev += len;
	if (ast_strlen_zero(dev))
		return CLI_SHOWUSAGE;

	state_val = ast_devstate_val(state);

	if (state_val == AST_DEVICE_UNKNOWN)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "Changing %s to %s\n", dev, state);

	ast_db_put(astdb_family, dev, state);

	ast_devstate_changed(state_val, "Custom:%s", dev);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_funcdevstate_list_deprecated = AST_CLI_DEFINE(handle_cli_funcdevstate_list, "List currently known custom device states");
static struct ast_cli_entry cli_funcdevstate[] = {
	AST_CLI_DEFINE(handle_cli_devstate_list, "List currently known custom device states", .deprecate_cmd = &cli_funcdevstate_list_deprecated),
	AST_CLI_DEFINE(handle_cli_devstate_change, "Change a custom device state"),
};

static struct ast_custom_function devstate_function = {
	.name = "DEVICE_STATE",
	.synopsis = "Get or Set a device state",
	.syntax = "DEVICE_STATE(device)",
	.desc =
	"  The DEVICE_STATE function can be used to retrieve the device state from any\n"
	"device state provider.  For example:\n"
	"   NoOp(SIP/mypeer has state ${DEVICE_STATE(SIP/mypeer)})\n"
	"   NoOp(Conference number 1234 has state ${DEVICE_STATE(MeetMe:1234)})\n"
	"\n"
	"  The DEVICE_STATE function can also be used to set custom device state from\n"
	"the dialplan.  The \"Custom:\" prefix must be used.  For example:\n"
	"  Set(DEVICE_STATE(Custom:lamp1)=BUSY)\n"
	"  Set(DEVICE_STATE(Custom:lamp2)=NOT_INUSE)\n"
	"You can subscribe to the status of a custom device state using a hint in\n"
	"the dialplan:\n"
	"  exten => 1234,hint,Custom:lamp1\n"
	"\n"
	"  The possible values for both uses of this function are:\n"
	"UNKNOWN | NOT_INUSE | INUSE | BUSY | INVALID | UNAVAILABLE | RINGING\n"
	"RINGINUSE | ONHOLD\n",
	.read = devstate_read,
	.write = devstate_write,
};

static struct ast_custom_function hint_function = {
	.name = "HINT",
	.synopsis = "Get the devices set for a dialplan hint",
	.syntax = "HINT(extension[@context][|options])",
	.desc =
	"  The HINT function can be used to retrieve the list of devices that are\n"
	"mapped to a dialplan hint.  For example:\n"
	"   NoOp(Hint for Extension 1234 is ${HINT(1234)})\n"
	"Options:\n"
	"   'n' - Retrieve name on the hint instead of list of devices\n"
	"",
	.read = hint_read,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&devstate_function);
	res |= ast_custom_function_unregister(&hint_function);
	res |= ast_devstate_prov_del("Custom");
	res |= ast_cli_unregister_multiple(cli_funcdevstate, ARRAY_LEN(cli_funcdevstate));

	return res;
}

static int load_module(void)
{
	int res = 0;
	struct ast_db_entry *db_entry, *db_tree;

	/* Populate the device state cache on the system with all of the currently
	 * known custom device states. */
	db_entry = db_tree = ast_db_gettree(astdb_family, NULL);
	for (; db_entry; db_entry = db_entry->next) {
		const char *dev_name = strrchr(db_entry->key, '/') + 1;
		if (dev_name <= (const char *) 1)
			continue;
		ast_devstate_changed(ast_devstate_val(db_entry->data),
			"Custom:%s\n", dev_name);
	}
	ast_db_freetree(db_tree);
	db_tree = NULL;

	res |= ast_custom_function_register(&devstate_function);
	res |= ast_custom_function_register(&hint_function);
	res |= ast_devstate_prov_add("Custom", custom_devstate_callback);
	res |= ast_cli_register_multiple(cli_funcdevstate, ARRAY_LEN(cli_funcdevstate));

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Gets or sets a device state in the dialplan");
