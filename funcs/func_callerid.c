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
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/callerid.h"

static int callerpres_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	ast_copy_string(buf, ast_named_caller_presentation(chan->cid.cid_pres), len);
	return 0;
}

static int callerpres_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int pres = ast_parse_caller_presentation(value);
	if (pres < 0)
		ast_log(LOG_WARNING, "'%s' is not a valid presentation (see 'show function CALLERPRES')\n", value);
	else
		chan->cid.cid_pres = pres;
	return 0;
}

static int callerid_read(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, size_t len)
{
	int res = -1;
	char *opt = data;

	if (!chan)
		return -1;

	if (strchr(opt, ',')) {
		char name[80], num[80];

		data = strsep(&opt, ",");
		ast_callerid_split(opt, name, sizeof(name), num, sizeof(num));

		if (!strncasecmp("all", data, 3)) {
			snprintf(buf, len, "\"%s\" <%s>", name, num);
			res = 0;
		} else if (!strncasecmp("name", data, 4)) {
			ast_copy_string(buf, name, len);
			res = 0;
		} else if (!strncasecmp("num", data, 3)) {
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
		} else if (!strncasecmp("num", data, 3)) {
			if (chan->cid.cid_num) {
				ast_copy_string(buf, chan->cid.cid_num, len);
				res = 0;
			}
		} else if (!strncasecmp("ani", data, 3)) {
			if (!strncasecmp(data + 3, "2", 1)) {
				snprintf(buf, len, "%d", chan->cid.cid_ani2);
			} else if (chan->cid.cid_ani) {
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
		} else if (!strncasecmp("pres", data, 4)) {
			ast_copy_string(buf, ast_named_caller_presentation(chan->cid.cid_pres), len);
			res = 0;
		} else if (!strncasecmp("ton", data, 3)) {
			snprintf(buf, len, "%d", chan->cid.cid_ton);
			res = 0;
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}

		ast_channel_unlock(chan);
	}

	return res;
}

static int callerid_write(struct ast_channel *chan, const char *cmd, char *data,
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
	} else if (!strncasecmp("num", data, 3)) { 
		ast_set_callerid(chan, value, NULL, NULL);
	} else if (!strncasecmp("ani", data, 3)) {
		if (!strncasecmp(data + 3, "2", 1)) {
			int i = atoi(value);
			chan->cid.cid_ani2 = i;
		} else
			ast_set_callerid(chan, NULL, NULL, value);
	} else if (!strncasecmp("dnid", data, 4)) {
		ast_channel_lock(chan);
		if (chan->cid.cid_dnid)
			ast_free(chan->cid.cid_dnid);
		chan->cid.cid_dnid = ast_strdup(value);
		ast_channel_unlock(chan);
	} else if (!strncasecmp("rdnis", data, 5)) {
		ast_channel_lock(chan);
		if (chan->cid.cid_rdnis)
			ast_free(chan->cid.cid_rdnis);
		chan->cid.cid_rdnis = ast_strdup(value);
		ast_channel_unlock(chan);
	} else if (!strncasecmp("pres", data, 4)) {
		int i;
		char *s, *val;

		/* Strip leading spaces */
		while ((value[0] == '\t') || (value[0] == ' '))
			++value;

		val = ast_strdupa(value);

		/* Strip trailing spaces */
		s = val + strlen(val);
		while ((s != val) && ((s[-1] == '\t') || (s[-1] == ' ')))
			--s;
		*s = '\0';

		if ((val[0] >= '0') && (val[0] <= '9'))
			i = atoi(val);
		else
			i = ast_parse_caller_presentation(val);

		if (i < 0)
			ast_log(LOG_ERROR, "Unknown calling number presentation '%s', value unchanged\n", val);
		else
			chan->cid.cid_pres = i;
	} else if (!strncasecmp("ton", data, 3)) {
		int i = atoi(value);
		chan->cid.cid_ton = i;
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
		"are \"all\", \"name\", \"num\", \"ANI\", \"DNID\", \"RDNIS\", \"pres\",\n"
		"and \"ton\".\n"
		"Uses channel callerid by default or optional callerid, if specified.\n",
	.read = callerid_read,
	.write = callerid_write,
};

static struct ast_custom_function callerpres_function = {
	.name = "CALLERPRES",
	.synopsis = "Gets or sets Caller*ID presentation on the channel.",
	.syntax = "CALLERPRES()",
	.desc =
"Gets or sets Caller*ID presentation on the channel.  The following values\n"
"are valid:\n"
"      allowed_not_screened    : Presentation Allowed, Not Screened\n"
"      allowed_passed_screen   : Presentation Allowed, Passed Screen\n" 
"      allowed_failed_screen   : Presentation Allowed, Failed Screen\n" 
"      allowed                 : Presentation Allowed, Network Number\n"
"      prohib_not_screened     : Presentation Prohibited, Not Screened\n" 
"      prohib_passed_screen    : Presentation Prohibited, Passed Screen\n"
"      prohib_failed_screen    : Presentation Prohibited, Failed Screen\n"
"      prohib                  : Presentation Prohibited, Network Number\n"
"      unavailable             : Number Unavailable\n",
	.read = callerpres_read,
	.write = callerpres_write,
};

static int unload_module(void)
{
	int res = ast_custom_function_unregister(&callerpres_function);
	res |= ast_custom_function_unregister(&callerid_function);
	return res;
}

static int load_module(void)
{
	int res = ast_custom_function_register(&callerpres_function);
	res |= ast_custom_function_register(&callerid_function);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Caller ID related dialplan functions");
