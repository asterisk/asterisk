/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Gareth Palmer
 *
 * Gareth Palmer <gareth@acsdata.co.nz>
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
 * \brief Connected Line dialplan function
 *
 * \ingroup functions
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

/*
 * Do not document the CONNECTEDLINE(source) datatype.
 * It has turned out to not be needed.  The source value is really                                      .
 * only useful as a possible tracing aid.
 */
/*** DOCUMENTATION
	<function name="CONNECTEDLINE" language="en_US">
		<synopsis>
			Gets or sets Connected Line data on the channel.
		</synopsis>
		<syntax>
			<parameter name="datatype" required="true">
				<para>The allowable datatypes are:</para>
				<enumlist>
					<enum name = "all" />
					<enum name = "num" />
					<enum name = "name" />
					<enum name = "ton" />
					<enum name = "pres" />
				</enumlist>
			</parameter>
			<parameter name="i">
				<para>If set, this will prevent the channel from sending out protocol
				messages because of the value being set</para>
			</parameter>
		</syntax>
		<description>
			<para>Gets or sets Connected Line data on the channel.</para>
		</description>
	</function>
 ***/

static int connectedline_read(struct ast_channel *chan, const char *cmd, char *data,
			      char *buf, size_t len)
{
	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan)
		return -1;

	ast_channel_lock(chan);

	if (!strncasecmp("all", data, 3)) {
		snprintf(buf, len, "\"%s\" <%s>",
			 S_OR(chan->connected.id.name, ""),
			 S_OR(chan->connected.id.number, ""));
	} else if (!strncasecmp("name", data, 4)) {
		if (chan->connected.id.name) {
			ast_copy_string(buf, chan->connected.id.name, len);
		}
	} else if (!strncasecmp("num", data, 3)) {
		if (chan->connected.id.number) {
			ast_copy_string(buf, chan->connected.id.number, len);
		}
	} else if (!strncasecmp("ton", data, 3)) {
		snprintf(buf, len, "%d", chan->connected.id.number_type);
	} else if (!strncasecmp("pres", data, 4)) {
		ast_copy_string(buf, ast_named_caller_presentation(chan->connected.id.number_presentation), len);
	} else if (!strncasecmp("source", data, 6)) {
		ast_copy_string(buf, ast_connected_line_source_name(chan->connected.source), len);
	} else {
		ast_log(LOG_ERROR, "Unknown connectedline data type '%s'.\n", data);
	}

	ast_channel_unlock(chan);

	return 0;
}

static int connectedline_write(struct ast_channel *chan, const char *cmd, char *data,
			       const char *value)
{
	struct ast_party_connected_line connected;
	char *val;
	char *option;
	void (*set_it)(struct ast_channel *chan, const struct ast_party_connected_line *connected);

	if (!value || !chan) {
		return -1;
	}

	/* Determine if the update indication inhibit option is present */
	option = strchr(data, ',');
	if (option) {
		option = ast_skip_blanks(option + 1);
		switch (*option) {
		case 'i':
			set_it = ast_channel_set_connected_line;
			break;

		default:
			ast_log(LOG_ERROR, "Unknown connectedline option '%s'.\n", option);
			return 0;
		}
	}
	else {
		set_it = ast_channel_update_connected_line;
	}

	ast_channel_lock(chan);
	ast_party_connected_line_set_init(&connected, &chan->connected);
	ast_channel_unlock(chan);

	value = ast_skip_blanks(value);

	if (!strncasecmp("all", data, 3)) {
		char name[256];
		char num[256];

		ast_callerid_split(value, name, sizeof(name), num, sizeof(num));
		connected.id.name = name;
		connected.id.number = num;
		set_it(chan, &connected);
	} else if (!strncasecmp("name", data, 4)) {
		connected.id.name = ast_strdupa(value);
		ast_trim_blanks(connected.id.name);
		set_it(chan, &connected);
	} else if (!strncasecmp("num", data, 3)) {
		connected.id.number = ast_strdupa(value);
		ast_trim_blanks(connected.id.number);
		set_it(chan, &connected);
	} else if (!strncasecmp("ton", data, 3)) {
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			connected.id.number_type = atoi(val);
			set_it(chan, &connected);
		} else {
			ast_log(LOG_ERROR, "Unknown connectedline type of number '%s', value unchanged\n", val);
		}
	} else if (!strncasecmp("pres", data, 4)) {
		int pres;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			pres = atoi(val);
		} else {
			pres = ast_parse_caller_presentation(val);
		}

		if (pres < 0) {
			ast_log(LOG_ERROR, "Unknown connectedline number presentation '%s', value unchanged\n", val);
		} else {
			connected.id.number_presentation = pres;
			set_it(chan, &connected);
		}
	} else if (!strncasecmp("source", data, 6)) {
		int source;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			source = atoi(val);
		} else {
			source = ast_connected_line_source_parse(val);
		}

		if (source < 0) {
			ast_log(LOG_ERROR, "Unknown connectedline source '%s', value unchanged\n", val);
		} else {
			connected.source = source;
			set_it(chan, &connected);
		}
	} else {
		ast_log(LOG_ERROR, "Unknown connectedline data type '%s'.\n", data);
	}

	return 0;
}

static struct ast_custom_function connectedline_function = {
	.name = "CONNECTEDLINE",
	.read = connectedline_read,
	.write = connectedline_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&connectedline_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&connectedline_function)
		? AST_MODULE_LOAD_DECLINE
		: AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Connected Line dialplan function");
