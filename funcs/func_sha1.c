/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 * Copyright (C) 2006, Claude Patry
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
 * \brief SHA1 digest related dialplan functions
 * 
 * \author Claude Patry <cpatry@gmail.com>
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

static int sha1(struct ast_channel *chan, char *cmd, char *data,
		char *buf, size_t len)
{
	*buf = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: SHA1(<data>) - missing argument!\n");
		return -1;
	}

	if (len >= 41)
		ast_sha1_hash(buf, data);
	else {
		ast_log(LOG_ERROR,
				"Insufficient space to produce SHA1 hash result (%d < 41)\n",
				len);
	}

	return 0;
}

static struct ast_custom_function sha1_function = {
	.name = "SHA1",
	.synopsis = "Computes a SHA1 digest",
	.syntax = "SHA1(<data>)",
	.read = sha1,
	.desc = "Generate a SHA1 digest via the SHA1 algorythm.\n"
		" Example:  Set(sha1hash=${SHA1(junky)})\n"
		" Sets the asterisk variable sha1hash to the string '60fa5675b9303eb62f99a9cd47f9f5837d18f9a0'\n"
		" which is known as his hash\n",
};

static char *tdesc = "SHA-1 computation dialplan function";

int unload_module(void)
{
	return ast_custom_function_unregister(&sha1_function);
}

int load_module(void)
{
	return ast_custom_function_register(&sha1_function);
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
