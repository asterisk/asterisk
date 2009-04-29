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

/*** DOCUMENTATION
	<function name="CALLERID" language="en_US">
		<synopsis>
			Gets or sets Caller*ID data on the channel.
		</synopsis>
		<syntax>
			<parameter name="datatype" required="true">
				<para>The allowable datatypes are:</para>
				<enumlist>
					<enum name="all" />
					<enum name="num" />
					<enum name="ANI" />
					<enum name="DNID" />
					<enum name="RDNIS" />
					<enum name="pres" />
					<enum name="ton" />
				</enumlist>
			</parameter>
			<parameter name="CID">
				<para>Optional Caller*ID</para>
			</parameter>
		</syntax>
		<description>
			<para>Gets or sets Caller*ID data on the channel. Uses channel callerid by default or optional
			callerid, if specified.</para>
		</description>
	</function>
	<function name="CALLERPRES" language="en_US">
		<synopsis>
			Gets or sets Caller*ID presentation on the channel.
		</synopsis>
		<syntax />
		<description>
			<para>Gets or sets Caller*ID presentation on the channel. The following values
			are valid:</para>
			<enumlist>
				<enum name="allowed_not_screened">
					<para>Presentation Allowed, Not Screened.</para>
				</enum>
				<enum name="allowed_passed_screen">
					<para>Presentation Allowed, Passed Screen.</para>
				</enum>
				<enum name="allowed_failed_screen">
					<para>Presentation Allowed, Failed Screen.</para>
				</enum>
				<enum name="allowed">
					<para>Presentation Allowed, Network Number.</para>
				</enum>
				<enum name="prohib_not_screened">
					<para>Presentation Prohibited, Not Screened.</para>
				</enum>
				<enum name="prohib_passed_screen">
					<para>Presentation Prohibited, Passed Screen.</para>
				</enum>
				<enum name="prohib_failed_screen">
					<para>Presentation Prohibited, Failed Screen.</para>
				</enum>
				<enum name="prohib">
					<para>Presentation Prohibited, Network Number.</para>
				</enum>
				<enum name="unavailable">
					<para>Number Unavailable.</para>
				</enum>
			</enumlist>
		</description>
	</function>
 ***/

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
	char *opt = data;

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan)
		return -1;

	if (strchr(opt, ',')) {
		char name[80], num[80];

		data = strsep(&opt, ",");
		ast_callerid_split(opt, name, sizeof(name), num, sizeof(num));

		if (!strncasecmp("all", data, 3)) {
			snprintf(buf, len, "\"%s\" <%s>", name, num);
		} else if (!strncasecmp("name", data, 4)) {
			ast_copy_string(buf, name, len);
		} else if (!strncasecmp("num", data, 3)) {
			/* also matches "number" */
			ast_copy_string(buf, num, len);
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}
	} else {
		ast_channel_lock(chan);

		if (!strncasecmp("all", data, 3)) {
			snprintf(buf, len, "\"%s\" <%s>",
				S_OR(chan->cid.cid_name, ""),
				S_OR(chan->cid.cid_num, ""));
		} else if (!strncasecmp("name", data, 4)) {
			if (chan->cid.cid_name) {
				ast_copy_string(buf, chan->cid.cid_name, len);
			}
		} else if (!strncasecmp("num", data, 3)) {
			/* also matches "number" */
			if (chan->cid.cid_num) {
				ast_copy_string(buf, chan->cid.cid_num, len);
			}
		} else if (!strncasecmp("ani", data, 3)) {
			if (!strncasecmp(data + 3, "2", 1)) {
				snprintf(buf, len, "%d", chan->cid.cid_ani2);
			} else if (chan->cid.cid_ani) {
				ast_copy_string(buf, chan->cid.cid_ani, len);
			}
		} else if (!strncasecmp("dnid", data, 4)) {
			if (chan->cid.cid_dnid) {
				ast_copy_string(buf, chan->cid.cid_dnid, len);
			}
		} else if (!strncasecmp("rdnis", data, 5)) {
			if (chan->cid.cid_rdnis) {
				ast_copy_string(buf, chan->cid.cid_rdnis, len);
			}
		} else if (!strncasecmp("pres", data, 4)) {
			ast_copy_string(buf, ast_named_caller_presentation(chan->cid.cid_pres), len);
		} else if (!strncasecmp("ton", data, 3)) {
			snprintf(buf, len, "%d", chan->cid.cid_ton);
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}

		ast_channel_unlock(chan);
	}

	return 0;
}

static int callerid_write(struct ast_channel *chan, const char *cmd, char *data,
			  const char *value)
{
	if (!value || !chan)
		return -1;

	value = ast_skip_blanks(value);

	if (!strncasecmp("all", data, 3)) {
		char name[256];
		char num[256];

		ast_callerid_split(value, name, sizeof(name), num, sizeof(num));
		ast_set_callerid(chan, num, name, num);
		if (chan->cdr) {
			ast_cdr_setcid(chan->cdr, chan);
		}
	} else if (!strncasecmp("name", data, 4)) {
		ast_set_callerid(chan, NULL, value, NULL);
		if (chan->cdr) {
			ast_cdr_setcid(chan->cdr, chan);
		}
	} else if (!strncasecmp("num", data, 3)) {
		/* also matches "number" */
		ast_set_callerid(chan, value, NULL, NULL);
		if (chan->cdr) {
			ast_cdr_setcid(chan->cdr, chan);
		}
	} else if (!strncasecmp("ani", data, 3)) {
		if (!strncasecmp(data + 3, "2", 1)) {
			chan->cid.cid_ani2 = atoi(value);
		} else {
			ast_set_callerid(chan, NULL, NULL, value);
		}
		if (chan->cdr) {
			ast_cdr_setcid(chan->cdr, chan);
		}
	} else if (!strncasecmp("dnid", data, 4)) {
		ast_channel_lock(chan);
		if (chan->cid.cid_dnid) {
			ast_free(chan->cid.cid_dnid);
		}
		chan->cid.cid_dnid = ast_strdup(value);
		if (chan->cdr) {
			ast_cdr_setcid(chan->cdr, chan);
		}
		ast_channel_unlock(chan);
	} else if (!strncasecmp("rdnis", data, 5)) {
		ast_channel_lock(chan);
		if (chan->cid.cid_rdnis) {
			ast_free(chan->cid.cid_rdnis);
		}
		chan->cid.cid_rdnis = ast_strdup(value);
		if (chan->cdr) {
			ast_cdr_setcid(chan->cdr, chan);
		}
		ast_channel_unlock(chan);
	} else if (!strncasecmp("pres", data, 4)) {
		int i;
		char *val;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if ((val[0] >= '0') && (val[0] <= '9')) {
			i = atoi(val);
		} else {
			i = ast_parse_caller_presentation(val);
		}

		if (i < 0) {
			ast_log(LOG_ERROR, "Unknown calling number presentation '%s', value unchanged\n", val);
		} else {
			chan->cid.cid_pres = i;
		}
	} else if (!strncasecmp("ton", data, 3)) {
		chan->cid.cid_ton = atoi(value);
	} else {
		ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
	}

	return 0;
}

static struct ast_custom_function callerid_function = {
	.name = "CALLERID",
	.read = callerid_read,
	.read_max = 256,
	.write = callerid_write,
};

static struct ast_custom_function callerpres_function = {
	.name = "CALLERPRES",
	.read = callerpres_read,
	.read_max = 50,
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
