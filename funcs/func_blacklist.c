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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/astdb.h"

/*** DOCUMENTATION
	<function name="BLACKLIST" language="en_US">
		<synopsis>
			Check if the callerid is on the blacklist.
		</synopsis>
		<syntax />
		<description>
			<para>Uses astdb to check if the Caller*ID is in family <literal>blacklist</literal>.
			Returns <literal>1</literal> or <literal>0</literal>.</para>
		</description>
		<see-also>
			<ref type="function">DB</ref>
		</see-also>
	</function>

***/

static int blacklist_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char blacklist[1];
	int bl = 0;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (ast_channel_caller(chan)->id.number.valid && ast_channel_caller(chan)->id.number.str) {
		if (!ast_db_get("blacklist", ast_channel_caller(chan)->id.number.str, blacklist, sizeof (blacklist)))
			bl = 1;
	}
	if (ast_channel_caller(chan)->id.name.valid && ast_channel_caller(chan)->id.name.str) {
		if (!ast_db_get("blacklist", ast_channel_caller(chan)->id.name.str, blacklist, sizeof (blacklist)))
			bl = 1;
	}

	snprintf(buf, len, "%d", bl);
	return 0;
}

static int blacklist_read2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **str, ssize_t len)
{
	/* 2 bytes is a single integer, plus terminating null */
	if (ast_str_size(*str) - ast_str_strlen(*str) < 2) {
		if (len > ast_str_size(*str) || len == 0) {
			ast_str_make_space(str, len ? len : ast_str_strlen(*str) + 2);
		}
	}
	if (ast_str_size(*str) - ast_str_strlen(*str) >= 2) {
		int res = blacklist_read(chan, cmd, data, ast_str_buffer(*str) + ast_str_strlen(*str), 2);
		ast_str_update(*str);
		return res;
	}
	return -1;
}

static struct ast_custom_function blacklist_function = {
	.name = "BLACKLIST",
	.read = blacklist_read,
	.read2 = blacklist_read2,
};

static int load_module(void)
{
	return ast_custom_function_register(&blacklist_function);
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Look up Caller*ID name/number from blacklist database");
