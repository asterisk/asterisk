/*
* Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Maksim Nesterov
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
 * \brief UUID dialplan function
 *
 * \author Maksim Nesterov <braamsdev@gmail.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/uuid.h"

/*** DOCUMENTATION
	<function name="UUID" language="en_US">
		<synopsis>
			Generates an UUID.
		</synopsis>
		<syntax>
		</syntax>
		<description>
			<para>Returns a version 4 (random) Universally Unique Identifier (UUID) as a string.</para>
			<example title="Generate an UUID">
			same => n,Set(uuid=${UUID()})
			</example>
		</description>
	</function>
 ***/

static int uuid(struct ast_channel *chan, const char *cmd, char *data,
				char *buf, size_t len)
{
	ast_uuid_generate_str(buf, AST_UUID_STR_LEN);
	return 0;
}

static struct ast_custom_function uuid_function = {
	.name = "UUID",
	.read = uuid,
	.read_max = AST_UUID_STR_LEN,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&uuid_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&uuid_function);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY,
								  "UUID generation dialplan function");
