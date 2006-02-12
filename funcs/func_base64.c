/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Digium, Inc.
 * Copyright (C) 2005, Claude Patry
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
 * \brief Use the base64 as functions
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int base64_encode(struct ast_channel *chan, char *cmd, char *data,
			 char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: BASE64_ENCODE(<data>) - missing argument!\n");
		return -1;
	}

	ast_base64encode(buf, (unsigned char *) data, strlen(data), len);

	return 0;
}

static int base64_decode(struct ast_channel *chan, char *cmd, char *data,
			 char *buf, size_t len)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: BASE64_DECODE(<base_64 string>) - missing argument!\n");
		return -1;
	}

	ast_base64decode((unsigned char *) buf, data, len);

	return 0;
}

static struct ast_custom_function base64_encode_function = {
	.name = "BASE64_ENCODE",
	.synopsis = "Encode a string in base64",
	.desc = "Returns the base64 string\n",
	.syntax = "BASE64_ENCODE(<string>)",
	.read = base64_encode,
};

static struct ast_custom_function base64_decode_function = {
	.name = "BASE64_DECODE",
	.synopsis = "Decode a base64 string",
	.desc = "Returns the plain text string\n",
	.syntax = "BASE64_DECODE(<base64_string>)",
	.read = base64_decode,
};

static char *tdesc = "base64 encode/decode dialplan functions";

int unload_module(void)
{
	return ast_custom_function_unregister(&base64_encode_function) |
		ast_custom_function_unregister(&base64_decode_function);
}

int load_module(void)
{
	return ast_custom_function_register(&base64_encode_function) |
		ast_custom_function_register(&base64_decode_function);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
