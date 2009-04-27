/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Redirecting data dialplan function
 * \ingroup functions
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

/* ------------------------------------------------------------------- */


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
 * Do not document the REDIRECTING(pres) datatype.
 * It has turned out that the from-pres and to-pres values must be kept
 * separate.  They represent two different parties and there is a case when
 * they are active at the same time.  The plain pres option will simply
 * live on as a historical relic.
 */
/*** DOCUMENTATION
	<function name="REDIRECTING" language="en_US">
		<synopsis>
			Gets or sets Redirecting data on the channel.
		</synopsis>
		<syntax>
			<parameter name="datatype" required="true">
				<para>The allowable datatypes are:</para>
				<enumlist>
					<enum name = "from-all" />
					<enum name = "from-num" />
					<enum name = "from-name" />
					<enum name = "from-ton" />
					<enum name = "from-pres" />
					<enum name = "to-all" />
					<enum name = "to-num" />
					<enum name = "to-name" />
					<enum name = "to-ton" />
					<enum name = "to-pres" />
					<enum name = "reason" />
					<enum name = "count" />
				</enumlist>
			</parameter>
			<parameter name="i">
				<para>If set, this will prevent the channel from sending out protocol
				messages because of the value being set</para>
			</parameter>
		</syntax>
		<description>
			<para>Gets or sets Redirecting data on the channel. The allowable values
			for the <replaceable>reason</replaceable> field are the following:</para>
			<enumlist>
				<enum name = "unknown"><para>Unknown</para></enum>
				<enum name = "cfb"><para>Call Forwarding Busy</para></enum>
				<enum name = "cfnr"><para>Call Forwarding No Reply</para></enum>
				<enum name = "unavailable"><para>Callee is Unavailable</para></enum>
				<enum name = "time_of_day"><para>Time of Day</para></enum>
				<enum name = "dnd"><para>Do Not Disturb</para></enum>
				<enum name = "deflection"><para>Call Deflection</para></enum>
				<enum name = "follow_me"><para>Follow Me</para></enum>
				<enum name = "out_of_order"><para>Called DTE Out-Of-Order</para></enum>
				<enum name = "away"><para>Callee is Away</para></enum>
				<enum name = "cf_dte"><para>Call Forwarding By The Called DTE</para></enum>
				<enum name = "cfu"><para>Call Forwarding Unconditional</para></enum>
			</enumlist>
		</description>
	</function>
 ***/

enum ID_FIELD_STATUS {
	ID_FIELD_VALID,
	ID_FIELD_INVALID,
	ID_FIELD_UNKNOWN
};




/* ******************************************************************* */
/*!
 * \internal
 * \brief Read values from the party id struct.
 *
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 * \param data Remaining function datatype string
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS redirecting_id_read(char *buf, size_t len, char *data, const struct ast_party_id *id)
{
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (!strncasecmp("all", data, 3)) {
		snprintf(buf, len, "\"%s\" <%s>",
			 S_OR(id->name, ""),
			 S_OR(id->number, ""));
	} else if (!strncasecmp("name", data, 4)) {
		if (id->name) {
			ast_copy_string(buf, id->name, len);
		}
	} else if (!strncasecmp("num", data, 3)) {
		if (id->number) {
			ast_copy_string(buf, id->number, len);
		}
	} else if (!strncasecmp("ton", data, 3)) {
		snprintf(buf, len, "%d", id->number_type);
	} else if (!strncasecmp("pres", data, 4)) {
		ast_copy_string(buf, ast_named_caller_presentation(id->number_presentation), len);
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}




/* ******************************************************************* */
/*!
 * \internal
 * \brief Read values from the redirecting information struct.
 *
 * \param chan Asterisk channel to read
 * \param cmd Not used
 * \param data Redirecting function datatype string
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int redirecting_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan)
		return -1;

	ast_channel_lock(chan);

	if (!strncasecmp("from-", data, 5)) {
		struct ast_party_id from_id;

		from_id = chan->redirecting.from;
		from_id.number = chan->cid.cid_rdnis;
		switch (redirecting_id_read(buf, len, data + 5, &from_id)) {
		case ID_FIELD_VALID:
		case ID_FIELD_INVALID:
			break;

		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
	} else if (!strncasecmp("to-", data, 3)) {
		switch (redirecting_id_read(buf, len, data + 3, &chan->redirecting.to)) {
		case ID_FIELD_VALID:
		case ID_FIELD_INVALID:
			break;

		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
	} else if (!strncasecmp("pres", data, 4)) {
		ast_copy_string(buf, ast_named_caller_presentation(chan->redirecting.from.number_presentation), len);
	} else if (!strncasecmp("reason", data, 6)) {
		ast_copy_string(buf, ast_redirecting_reason_name(chan->redirecting.reason), len);
	} else if (!strncasecmp("count", data, 5)) {
		snprintf(buf, len, "%d", chan->redirecting.count);
	} else {
		ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
	}

	ast_channel_unlock(chan);

	return 0;
}




/* ******************************************************************* */
/*!
 * \internal
 * \brief Write new values to the party id struct
 *
 * \param id Party ID struct to write values
 * \param data Remaining function datatype string
 * \param value Value to assign to the party id.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_INVALID on error with field value.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS redirecting_id_write(struct ast_party_id *id, char *data, const char *value)
{
	char *val;
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (!strncasecmp("all", data, 3)) {
		char name[256];
		char num[256];

		ast_callerid_split(value, name, sizeof(name), num, sizeof(num));
		if (!(id->name = ast_strdup(name))) {
			return ID_FIELD_INVALID;
		}
		if (!(id->number = ast_strdup(num))) {
			return ID_FIELD_INVALID;
		}
	} else if (!strncasecmp("name", data, 4)) {
		id->name = ast_strdup(value);
		ast_trim_blanks(id->name);
	} else if (!strncasecmp("num", data, 3)) {
		id->number = ast_strdup(value);
		ast_trim_blanks(id->number);
	} else if (!strncasecmp("ton", data, 3)) {
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			id->number_type = atoi(val);
		} else {
			ast_log(LOG_ERROR, "Unknown redirecting type of number '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
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
			ast_log(LOG_ERROR, "Unknown redirecting number presentation '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
		} else {
			id->number_presentation = pres;
		}
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}




/* ******************************************************************* */
/*!
 * \internal
 * \brief Write new values to the redirecting information struct.
 *
 * \param chan Asterisk channel to update
 * \param cmd Not used
 * \param data Redirecting function datatype string
 * \param value Value to assign to the redirecting information struct.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int redirecting_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_party_redirecting redirecting;
	char *val;
	char *option;
	void (*set_it)(struct ast_channel *chan, const struct ast_party_redirecting *redirecting);

	if (!value || !chan) {
		return -1;
	}

	/* Determine if the update indication inhibit option is present */
	option = strchr(data, ',');
	if (option) {
		option = ast_skip_blanks(option + 1);
		switch (*option) {
		case 'i':
			set_it = ast_channel_set_redirecting;
			break;

		default:
			ast_log(LOG_ERROR, "Unknown redirecting option '%s'.\n", option);
			return 0;
		}
	}
	else {
		set_it = ast_channel_update_redirecting;
	}

	ast_channel_lock(chan);
	ast_party_redirecting_set_init(&redirecting, &chan->redirecting);
	ast_channel_unlock(chan);

	value = ast_skip_blanks(value);

	if (!strncasecmp("from-", data, 5)) {
		switch (redirecting_id_write(&redirecting.from, data + 5, value)) {
		case ID_FIELD_VALID:
			set_it(chan, &redirecting);
			ast_party_redirecting_free(&redirecting);
			break;

		case ID_FIELD_INVALID:
			break;

		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
	} else if (!strncasecmp("to-", data, 3)) {
		switch (redirecting_id_write(&redirecting.to, data + 3, value)) {
		case ID_FIELD_VALID:
			set_it(chan, &redirecting);
			ast_party_redirecting_free(&redirecting);
			break;

		case ID_FIELD_INVALID:
			break;

		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
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
			ast_log(LOG_ERROR, "Unknown redirecting number presentation '%s', value unchanged\n", val);
		} else {
			redirecting.from.number_presentation = pres;
			redirecting.to.number_presentation = pres;
			set_it(chan, &redirecting);
		}
	} else if (!strncasecmp("reason", data, 6)) {
		int reason;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			reason = atoi(val);
		} else {
			reason = ast_redirecting_reason_parse(val);
		}

		if (reason < 0) {
			ast_log(LOG_ERROR, "Unknown redirecting reason '%s', value unchanged\n", val);
		} else {
			redirecting.reason = reason;
			set_it(chan, &redirecting);
		}
	} else if (!strncasecmp("count", data, 5)) {
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			redirecting.count = atoi(val);
			set_it(chan, &redirecting);
		} else {
			ast_log(LOG_ERROR, "Unknown redirecting count '%s', value unchanged\n", val);
		}
	} else {
		ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
	}

	return 0;
}




static struct ast_custom_function redirecting_function = {
	.name = "REDIRECTING",
	.read = redirecting_read,
	.write = redirecting_write,
};




/* ******************************************************************* */
/*!
 * \internal
 * \brief Unload the function module
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int unload_module(void)
{
	return ast_custom_function_unregister(&redirecting_function);
}




/* ******************************************************************* */
/*!
 * \internal
 * \brief Load and initialize the function module.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int load_module(void)
{
	return ast_custom_function_register(&redirecting_function)
		? AST_MODULE_LOAD_DECLINE
		: AST_MODULE_LOAD_SUCCESS;
}




AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Redirecting data dialplan function");


/* ------------------------------------------------------------------- */
/* end func_redirecting.c */
