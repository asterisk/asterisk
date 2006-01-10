/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Digium, Inc.
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

/* ASTERISK_FILE_VERSION(__FILE__, "Revision: 7221 ") */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static char *builtin_function_base64_encode(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	int res = 0;

	if (ast_strlen_zero(data) ) {
		ast_log(LOG_WARNING, "Syntax: BASE64_ENCODE(<data>) - missing argument!\n");
		return NULL;
	}

	ast_log(LOG_DEBUG, "data=%s\n",data);
	res = ast_base64encode(buf, data, strlen(data), len);
	ast_log(LOG_DEBUG, "res=%d\n", res);
	return buf;
}

static char *builtin_function_base64_decode(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	if (ast_strlen_zero(data) ) {
		ast_log(LOG_WARNING, "Syntax: BASE64_DECODE(<base_64 string>) - missing argument!\n");
		return NULL;
	}

	ast_log(LOG_DEBUG, "data=%s\n", data);
	ast_base64decode(buf, data, len);
	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function base64_encode_function = {
	.name = "BASE64_ENCODE",
	.synopsis = "Encode a string in base64",
	.desc = "Returns the base64 string\n",
	.syntax = "BASE64_ENCODE(<string>)",
	.read = builtin_function_base64_encode,
};

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function base64_decode_function = {
	.name = "BASE64_DECODE",
	.synopsis = "Decode a base64 string",
	.desc = "Returns the plain text string\n",
	.syntax = "BASE64_DECODE(<base64_string>)",
	.read = builtin_function_base64_decode,
};
