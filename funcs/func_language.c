/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Language related dialplan functions
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/stringfields.h"

static int depwarning = 0;

static int language_read(struct ast_channel *chan, char *cmd, char *data,
			 char *buf, size_t len)
{
	if (!depwarning) {
		depwarning = 1;
		ast_log(LOG_WARNING,
				"LANGUAGE() is deprecated; use CHANNEL(language) instead.\n");
	}

	ast_copy_string(buf, chan ? chan->language : "", len);

	return 0;
}

static int language_write(struct ast_channel *chan, char *cmd, char *data,
			  const char *value)
{
	if (!depwarning) {
		depwarning = 1;
		ast_log(LOG_WARNING,
				"LANGUAGE() is deprecated; use CHANNEL(language) instead.\n");
	}

	if (chan && value)
		ast_string_field_set(chan, language, value);

	return 0;
}

static struct ast_custom_function language_function = {
	.name = "LANGUAGE",
	.synopsis = "Gets or sets the channel's language.",
	.syntax = "LANGUAGE()",
	.desc = "Deprecated. Use CHANNEL(language) instead.\n",
	.read = language_read,
	.write = language_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&language_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&language_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel language dialplan function");
