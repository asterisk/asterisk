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
 * \note Props go out to Ahrimanes in #asterisk for requesting this at 4:30 AM
 *       when I couldn't sleep.  :)
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"
#include "asterisk/cli.h"

struct custom_device {
	int state;
	AST_RWLIST_ENTRY(custom_device) entry;
	char name[1];
};

static AST_RWLIST_HEAD_STATIC(custom_devices, custom_device);

static int devstate_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	ast_copy_string(buf, ast_devstate_str(ast_device_state(data)), len);

	return 0;
}

static int devstate_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct custom_device *dev;
	int len = strlen("Custom:");

	if (strncasecmp(data, "Custom:", len)) {
		ast_log(LOG_WARNING, "The DEVSTATE function can only be used to set 'Custom:' device state!\n");
		return -1;
	}
	data += len;
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "DEVSTATE function called with no custom device name!\n");
		return -1;
	}

	AST_RWLIST_WRLOCK(&custom_devices);
	AST_RWLIST_TRAVERSE(&custom_devices, dev, entry) {
		if (!strcasecmp(dev->name, data))
			break;
	}
	if (!dev) {
		if (!(dev = ast_calloc(1, sizeof(*dev) + strlen(data) + 1))) {
			AST_RWLIST_UNLOCK(&custom_devices);
			return -1;
		}
		strcpy(dev->name, data);
		AST_RWLIST_INSERT_HEAD(&custom_devices, dev, entry);
	}
	dev->state = ast_devstate_val(value);
	ast_device_state_changed("Custom:%s", dev->name);
	AST_RWLIST_UNLOCK(&custom_devices);

	return 0;
}

static enum ast_device_state custom_devstate_callback(const char *data)
{
	struct custom_device *dev;
	enum ast_device_state state = AST_DEVICE_UNKNOWN;

	AST_RWLIST_RDLOCK(&custom_devices);
	AST_RWLIST_TRAVERSE(&custom_devices, dev, entry) {
		if (!strcasecmp(dev->name, data)) {
			state = dev->state;	
			break;
		}
	}
	AST_RWLIST_UNLOCK(&custom_devices);

	return state;
}

static char *cli_funcdevstate_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct custom_device *dev;

	switch (cmd) {
	case CLI_INIT:
		e->command = "funcdevstate list";
		e->usage =
			"Usage: funcdevstate list\n"
			"       List all custom device states that have been set by using\n"
			"       the DEVSTATE dialplan function.\n";
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
	AST_RWLIST_RDLOCK(&custom_devices);
	AST_RWLIST_TRAVERSE(&custom_devices, dev, entry) {
		ast_cli(a->fd, "--- Name: 'Custom:%s'  State: '%s'\n"
		               "---\n", dev->name, ast_devstate_str(dev->state));
	}
	AST_RWLIST_UNLOCK(&custom_devices);
	ast_cli(a->fd,
	        "---------------------------------------------------------------------\n"
	        "---------------------------------------------------------------------\n"
	        "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_funcdevstate[] = {
	NEW_CLI(cli_funcdevstate_list, "List currently known custom device states"),
};

static struct ast_custom_function devstate_function = {
	.name = "DEVSTATE",
	.synopsis = "Get or Set a device state",
	.syntax = "DEVSTATE(device)",
	.desc =
	"  The DEVSTATE function can be used to retrieve the device state from any\n"
	"device state provider.  For example:\n"
	"   NoOp(SIP/mypeer has state ${DEVSTATE(SIP/mypeer)})\n"
	"   NoOp(Conference number 1234 has state ${DEVSTATE(MeetMe:1234)})\n"
	"\n"
	"  The DEVSTATE function can also be used to set custom device state from\n"
	"the dialplan.  The \"Custom:\" prefix must be used.  For example:\n"
	"  Set(DEVSTATE(Custom:lamp1)=BUSY)\n"
	"  Set(DEVSTATE(Custom:lamp2)=NOT_INUSE)\n"
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

static int unload_module(void)
{
	struct custom_device *dev;
	int res = 0;

	res |= ast_custom_function_unregister(&devstate_function);
	res |= ast_devstate_prov_del("Custom");
	res |= ast_cli_unregister_multiple(cli_funcdevstate, ARRAY_LEN(cli_funcdevstate));

	AST_RWLIST_WRLOCK(&custom_devices);
	while ((dev = AST_RWLIST_REMOVE_HEAD(&custom_devices, entry)))
		free(dev);
	AST_RWLIST_UNLOCK(&custom_devices);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&devstate_function);
	res |= ast_devstate_prov_add("Custom", custom_devstate_callback);
	res |= ast_cli_register_multiple(cli_funcdevstate, ARRAY_LEN(cli_funcdevstate));

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Device state dialplan functions");
