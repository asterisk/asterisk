/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2006, Digium, Inc.
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
 * \brief Caller ID related dialplan functions
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/options.h"
#include "asterisk/callerid.h"

static int callerid_read(struct ast_channel *chan, char *cmd, char *data,
			 char *buf, size_t len)
{
	int res = -1;
	char *opt = data;

	if (!chan)
		return -1;

	if (strchr(opt, '|')) {
		char name[80], num[80];

		data = strsep(&opt, "|");
		ast_callerid_split(opt, name, sizeof(name), num, sizeof(num));

		if (!strncasecmp("all", data, 3)) {
			snprintf(buf, len, "\"%s\" <%s>", name, num);
			res = 0;
		} else if (!strncasecmp("name", data, 4)) {
			ast_copy_string(buf, name, len);
			res = 0;
		} else if (!strncasecmp("num", data, 3) ||
			   !strncasecmp("number", data, 6)) {

			ast_copy_string(buf, num, len);
			res = 0;
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}
	} else {
		ast_channel_lock(chan);

		if (!strncasecmp("all", data, 3)) {
			snprintf(buf, len, "\"%s\" <%s>",
				 S_OR(chan->cid.cid_name, ""),
				 S_OR(chan->cid.cid_num, ""));
			res = 0;
		} else if (!strncasecmp("name", data, 4)) {
			if (chan->cid.cid_name) {
				ast_copy_string(buf, chan->cid.cid_name, len);
				res = 0;
			}
		} else if (!strncasecmp("num", data, 3)
				   || !strncasecmp("number", data, 6)) {
			if (chan->cid.cid_num) {
				ast_copy_string(buf, chan->cid.cid_num, len);
				res = 0;
			}
		} else if (!strncasecmp("ani", data, 3)) {
			if (chan->cid.cid_ani) {
				ast_copy_string(buf, chan->cid.cid_ani, len);
				res = 0;
			}
		} else if (!strncasecmp("dnid", data, 4)) {
			if (chan->cid.cid_dnid) {
				ast_copy_string(buf, chan->cid.cid_dnid, len);
				res = 0;
			}
		} else if (!strncasecmp("rdnis", data, 5)) {
			if (chan->cid.cid_rdnis) {
				ast_copy_string(buf, chan->cid.cid_rdnis, len);
				res = 0;
			}
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}

		ast_channel_unlock(chan);
	}

	return res;
}

static int callerid_write(struct ast_channel *chan, char *cmd, char *data,
			  const char *value)
{
	if (!value || !chan)
		return -1;

	if (!strncasecmp("all", data, 3)) {
		char name[256];
		char num[256];

		if (!ast_callerid_split(value, name, sizeof(name), num, sizeof(num)))
			ast_set_callerid(chan, num, name, num);
	} else if (!strncasecmp("name", data, 4)) {
		ast_set_callerid(chan, NULL, value, NULL);
	} else if (!strncasecmp("num", data, 3) ||
		   !strncasecmp("number", data, 6)) {
		ast_set_callerid(chan, value, NULL, NULL);
	} else if (!strncasecmp("ani", data, 3)) {
		ast_set_callerid(chan, NULL, NULL, value);
	} else if (!strncasecmp("dnid", data, 4)) {
		ast_channel_lock(chan);
		if (chan->cid.cid_dnid)
			free(chan->cid.cid_dnid);
		chan->cid.cid_dnid = ast_strdup(value);
		ast_channel_unlock(chan);
	} else if (!strncasecmp("rdnis", data, 5)) {
		ast_channel_lock(chan);
		if (chan->cid.cid_rdnis)
			free(chan->cid.cid_rdnis);
		chan->cid.cid_rdnis = ast_strdup(value);
		ast_channel_unlock(chan);
	} else {
		ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
	}

	return 0;
}

static struct ast_custom_function callerid_function = {
	.name = "CALLERID",
	.synopsis = "Gets or sets Caller*ID data on the channel.",
	.syntax = "CALLERID(datatype[,<optional-CID>])",
	.desc =
		"Gets or sets Caller*ID data on the channel.  The allowable datatypes\n"
		"are \"all\", \"name\", \"num\", \"ANI\", \"DNID\", \"RDNIS\".\n"
		"Uses channel callerid by default or optional callerid, if specified.\n",
	.read = callerid_read,
	.write = callerid_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&callerid_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&callerid_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Caller ID related dialplan function");
