/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Function to lookup the callerid number, and see if it is blacklisted
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup functions
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/astdb.h"

static int blacklist_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char blacklist[1];
	int bl = 0;

	if (chan->cid.cid_num) {
		if (!ast_db_get("blacklist", chan->cid.cid_num, blacklist, sizeof (blacklist)))
			bl = 1;
	}
	if (chan->cid.cid_name) {
		if (!ast_db_get("blacklist", chan->cid.cid_name, blacklist, sizeof (blacklist)))
			bl = 1;
	}

	snprintf(buf, len, "%d", bl);
	return 0;
}

static struct ast_custom_function blacklist_function = {
	.name = "BLACKLIST",
	.synopsis = "Check if the callerid is on the blacklist",
	.desc = "Uses astdb to check if the Caller*ID is in family 'blacklist'.  Returns 1 or 0.\n",
	.syntax = "BLACKLIST()",
	.read = blacklist_read,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&blacklist_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&blacklist_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Look up Caller*ID name/number from blacklist database");
