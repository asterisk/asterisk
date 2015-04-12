/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Modified from func_devstate.c by Russell Bryant <russell@digium.com> 
 * Adam Gundy <adam@starsilk.net>

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
 * \brief Get the state of a hinted extension for dialplan control
 *
 * \author Adam Gundy <adam@starsilk.net> 
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/devicestate.h"

/*** DOCUMENTATION
	<function name="EXTENSION_STATE" language="en_US">
		<synopsis>
			Get an extension's state.
		</synopsis>	
		<syntax argsep="@">
			<parameter name="extension" required="true" />
			<parameter name="context">
				<para>If it is not specified defaults to <literal>default</literal>.</para>
			</parameter>
		</syntax>
		<description>
			<para>The EXTENSION_STATE function can be used to retrieve the state from any
			hinted extension. For example:</para>
			<para>NoOp(1234@default has state ${EXTENSION_STATE(1234)})</para>
			<para>NoOp(4567@home has state ${EXTENSION_STATE(4567@home)})</para>
			<para>The possible values returned by this function are:</para>
			<para>UNKNOWN | NOT_INUSE | INUSE | BUSY | INVALID | UNAVAILABLE | RINGING |
			RINGINUSE | HOLDINUSE | ONHOLD</para>
		</description>
	</function>
 ***/

static const char *ast_extstate_str(int state)
{
	const char *res = "UNKNOWN";

	switch (state) {
	case AST_EXTENSION_NOT_INUSE:
		res = "NOT_INUSE";
		break;
	case AST_EXTENSION_INUSE:
		res = "INUSE";
		break;
	case AST_EXTENSION_BUSY:
		res = "BUSY";
		break;
	case AST_EXTENSION_UNAVAILABLE:
		res = "UNAVAILABLE";
		break;
	case AST_EXTENSION_RINGING:
		res = "RINGING";
		break;
	case AST_EXTENSION_INUSE | AST_EXTENSION_RINGING:
		res = "RINGINUSE";
		break;
	case AST_EXTENSION_INUSE | AST_EXTENSION_ONHOLD:
		res = "HOLDINUSE";
		break;
	case AST_EXTENSION_ONHOLD:
		res = "ONHOLD";
		break;
	}

	return res;
}

static int extstate_read(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	char *exten, *context;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "EXTENSION_STATE requires an extension\n");
		return -1;
	}

	context = exten = data;
	strsep(&context, "@");
	if (ast_strlen_zero(context))
		context = "default";

	if (ast_strlen_zero(exten)) {
		ast_log(LOG_WARNING, "EXTENSION_STATE requires an extension\n");
		return -1;
	}

	ast_copy_string(buf, 
		ast_extstate_str(ast_extension_state(chan, context, exten)), len);

	return 0;
}

static struct ast_custom_function extstate_function = {
	.name = "EXTENSION_STATE",
	.read = extstate_read,
	.read_max = 12,
};

static int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&extstate_function);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_custom_function_register(&extstate_function);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Gets an extension's state in the dialplan");
