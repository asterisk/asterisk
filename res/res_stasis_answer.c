/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Stasis application control support.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"

static int app_control_answer(struct stasis_app_control *control,
	struct ast_channel *chan, void *data)
{
	ast_debug(3, "%s: Answering\n",
		stasis_app_control_get_channel_id(control));
	return ast_raw_answer(chan);
}

int stasis_app_control_answer(struct stasis_app_control *control)
{
	int retval;

	ast_debug(3, "%s: Sending answer command\n",
		stasis_app_control_get_channel_id(control));

	retval = stasis_app_send_command(control, app_control_answer, NULL, NULL);

	if (retval != 0) {
		ast_log(LOG_WARNING, "%s: Failed to answer channel\n",
			stasis_app_control_get_channel_id(control));
		return -1;
	}

	return 0;
}

static int load_module(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application answer support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_stasis",
);
