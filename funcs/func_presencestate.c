/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011-2012, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com> 
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
 * \brief Custom presence provider
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/presencestate.h"
#include "asterisk/cli.h"
#include "asterisk/astdb.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="PRESENCE_STATE" language="en_US">
		<synopsis>
			Get or Set a presence state.
		</synopsis>
		<syntax>
			<parameter name="provider" required="true">
			  <para>The provider of the presence, such as <literal>CustomPresence</literal></para>
			</parameter>
			<parameter name="field" required="true">
			  <para>Which field of the presence state information is wanted.</para>
			  <optionlist>
				<option name="value">
				  <para>The current presence, such as <literal>away</literal></para>
				</option>
				<option name="subtype">
				  <para>Further information about the current presence</para>
				</option>
			    <option name="message">
				  <para>A custom message that may indicate further details about the presence</para>
				</option>
			  </optionlist>
			</parameter>
			<parameter name="options" required="false">
			  <optionlist>
			    <option name="e">
				  <para>Base-64 encode the data.</para>
				</option>
			  </optionlist>
			</parameter>
		</syntax>
		<description>
			<para>The PRESENCE_STATE function can be used to retrieve the presence from any
			presence provider. For example:</para>
			<para>NoOp(SIP/mypeer has presence ${PRESENCE_STATE(SIP/mypeer,value)})</para>
			<para>NoOp(Conference number 1234 has presence message ${PRESENCE_STATE(MeetMe:1234,message)})</para>
			<para>The PRESENCE_STATE function can also be used to set custom presence state from
			the dialplan.  The <literal>CustomPresence:</literal> prefix must be used. For example:</para>
			<para>Set(PRESENCE_STATE(CustomPresence:lamp1)=away,temporary,Out to lunch)</para>
			<para>Set(PRESENCE_STATE(CustomPresence:lamp2)=dnd,,Trying to get work done)</para>
			<para>You can subscribe to the status of a custom presence state using a hint in
			the dialplan:</para>
			<para>exten => 1234,hint,CustomPresence:lamp1</para>
			<para>The possible values for both uses of this function are:</para>
			<para>not_set | unavailable | available | away | xa | chat | dnd</para>
		</description>
	</function>
 ***/


static const char astdb_family[] = "CustomPresence";

static int presence_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int state;
	char *message = NULL;
	char *subtype = NULL;
	char *parse;
	int base64encode = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(provider);
		AST_APP_ARG(field);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "PRESENCE_STATE reading requires an argument \n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.provider) || ast_strlen_zero(args.field)) {
		ast_log(LOG_WARNING, "PRESENCE_STATE reading requires both presence provider and presence field arguments. \n");
		return -1;
	}

	state = ast_presence_state(args.provider, &subtype, &message);
	if (state < 0) {
		ast_log(LOG_WARNING, "PRESENCE_STATE unknown \n");
		return -1;
	}

	if (!(ast_strlen_zero(args.options)) && (strchr(args.options, 'e'))) {
		base64encode = 1;
	}

	if (!ast_strlen_zero(subtype) && !strcasecmp(args.field, "subtype")) {
		if (base64encode) {
			ast_base64encode(buf, (unsigned char *) subtype, strlen(subtype), len);
		} else {
			ast_copy_string(buf, subtype, len);
		}
	} else if (!ast_strlen_zero(message) && !strcasecmp(args.field, "message")) {
		if (base64encode) {
			ast_base64encode(buf, (unsigned char *) message, strlen(message), len);
		} else {
			ast_copy_string(buf, message, len);
		}

	} else if (!strcasecmp(args.field, "value")) {
		ast_copy_string(buf, ast_presence_state2str(state), len);
	}

	ast_free(message);
	ast_free(subtype);

	return 0;
}

static int parse_data(char *data, int *state, char **subtype, char **message, char **options)
{
	char *state_str;

	/* data syntax is state,subtype,message,options */
	*subtype = "";
	*message = "";
	*options = "";

	state_str = strsep(&data, ",");
	if (ast_strlen_zero(state_str)) {
		return -1; /* state is required */
	}

	*state = ast_presence_state_val(state_str);

	/* not a valid state */
	if (*state < 0) {
		ast_log(LOG_WARNING, "Unknown presence state value %s\n", state_str);
		return -1;
	}

	if (!(*subtype = strsep(&data,","))) {
		*subtype = "";
		return 0;
	}

	if (!(*message = strsep(&data, ","))) {
		*message = "";
		return 0;
	}

	if (!(*options = strsep(&data, ","))) {
		*options = "";
		return 0;
	}

	if (!ast_strlen_zero(*options) && !(strchr(*options, 'e'))) {
		ast_log(LOG_NOTICE, "Invalid options  '%s'\n", *options);
		return -1;
	}

	return 0;
}

static int presence_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	size_t len = strlen("CustomPresence:");
	char *tmp = data;
	char *args = ast_strdupa(value);
	int state;
	char *options, *message, *subtype;

	if (strncasecmp(data, "CustomPresence:", len)) {
		ast_log(LOG_WARNING, "The PRESENCE_STATE function can only set CustomPresence: presence providers.\n");
		return -1;
	}
	data += len;
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "PRESENCE_STATE function called with no custom device name!\n");
		return -1;
	}

	if (parse_data(args, &state, &subtype, &message, &options)) {
		ast_log(LOG_WARNING, "Invalid arguments to PRESENCE_STATE\n");
		return -1;
	}

	ast_db_put(astdb_family, data, value);

	ast_presence_state_changed(tmp);

	return 0;
}

static enum ast_presence_state custom_presence_callback(const char *data, char **subtype, char **message)
{
	char buf[1301] = "";
	int state;
	char *_options;
	char *_message;
	char *_subtype;

	ast_db_get(astdb_family, data, buf, sizeof(buf));

	if (parse_data(buf, &state, &_subtype, &_message, &_options)) {
		return -1;
	}

	if ((strchr(_options, 'e'))) {
		char tmp[1301];
		if (ast_strlen_zero(_subtype)) {
			*subtype = NULL;
		} else {
			memset(tmp, 0, sizeof(tmp));
			ast_base64decode((unsigned char *) tmp, _subtype, sizeof(tmp) - 1);
			*subtype = ast_strdup(tmp);
		}

		if (ast_strlen_zero(_message)) {
			*message = NULL;
		} else {
			memset(tmp, 0, sizeof(tmp));
			ast_base64decode((unsigned char *) tmp, _message, sizeof(tmp) - 1);
			*message = ast_strdup(tmp);
		}
	} else {
		*subtype = ast_strlen_zero(_subtype) ? NULL : ast_strdup(_subtype);
		*message = ast_strlen_zero(_message) ? NULL : ast_strdup(_message);
	}
	return state;
}

static struct ast_custom_function presence_function = {
	.name = "PRESENCE_STATE",
	.read = presence_read,
	.write = presence_write,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&presence_function);
	res |= ast_presence_state_prov_del("CustomPresence");
	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&presence_function);
	res |= ast_presence_state_prov_add("CustomPresence", custom_presence_callback);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Gets or sets a presence state in the dialplan",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);

