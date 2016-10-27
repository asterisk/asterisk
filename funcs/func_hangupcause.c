/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2012, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief Functions related to retreiving per-channel hangupcause information
 *
 * \author Kinsey Moore <kmoore@digium.com>
 * \ingroup functions
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="HANGUPCAUSE" language="en_US">
		<synopsis>
			Gets per-channel hangupcause information from the channel.
		</synopsis>
		<syntax>
			<parameter name="channel" required="true">
				<para>The name of the channel for which to retrieve cause information.</para>
			</parameter>
			<parameter name="type" required="true">
				<para>Parameter describing which type of information is requested. Types are:</para>
				<enumlist>
					<enum name="tech"><para>Technology-specific cause information</para></enum>
					<enum name="ast"><para>Translated Asterisk cause code</para></enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Gets technology-specific or translated Asterisk cause code information
			from the channel for the specified channel that resulted from a dial.</para>
		</description>
		<see-also>
			<ref type="function">HANGUPCAUSE_KEYS</ref>
			<ref type="application">HangupCauseClear</ref>
		</see-also>
	</function>
	<function name="HANGUPCAUSE_KEYS" language="en_US">
		<synopsis>
			Gets the list of channels for which hangup causes are available.
		</synopsis>
		<description>
			<para>Returns a comma-separated list of channel names to be used with the HANGUPCAUSE function.</para>
		</description>
		<see-also>
			<ref type="function">HANGUPCAUSE</ref>
			<ref type="application">HangupCauseClear</ref>
		</see-also>
	</function>
	<application name="HangupCauseClear" language="en_US">
		<synopsis>
			Clears hangup cause information from the channel that is available through HANGUPCAUSE.
		</synopsis>
		<description>
			<para>Clears all channel-specific hangup cause information from the channel.
			This is never done automatically (i.e. for new Dial()s).</para>
		</description>
		<see-also>
			<ref type="function">HANGUPCAUSE</ref>
			<ref type="function">HANGUPCAUSE_KEYS</ref>
		</see-also>
	</application>
 ***/

/*!
 * \internal
 * \brief Read values from the hangupcause ao2 container.
 *
 * \param chan Asterisk channel to read
 * \param cmd Not used
 * \param data HANGUPCAUSE function argument string
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int hangupcause_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parms;
	struct ast_control_pvt_cause_code *cause_code;
	int res = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);	/*!< Channel name */
		AST_APP_ARG(type);	/*!< Type of information requested (ast or tech) */
		);

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan) {
		return -1;
	}

	parms = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parms);
	if (args.argc != 2) {
		/* Must have two arguments. */
		ast_log(LOG_WARNING, "The HANGUPCAUSE function must have 2 parameters, not %u\n", args.argc);
		return -1;
	}

	ast_channel_lock(chan);
	cause_code = ast_channel_dialed_causes_find(chan, args.channel);
	ast_channel_unlock(chan);

	if (!cause_code) {
		ast_log(LOG_WARNING, "Unable to find information for channel %s\n", args.channel);
		return -1;
	}

	if (!strcmp(args.type, "ast")) {
		ast_copy_string(buf, ast_cause2str(cause_code->ast_cause), len);
	} else if (!strcmp(args.type, "tech")) {
		ast_copy_string(buf, cause_code->code, len);
	} else {
		ast_log(LOG_WARNING, "Information type not recognized (%s)\n", args.type);
		res = -1;
	}

	ao2_ref(cause_code, -1);

	return res;
}

/*!
 * \internal
 * \brief Read keys from the hangupcause ao2 container.
 *
 * \param chan Asterisk channel to read
 * \param cmd Not used
 * \param data HANGUPCAUSE_KEYS function argument string
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int hangupcause_keys_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_str *chanlist;

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan) {
		return -1;
	}

	ast_channel_lock(chan);
	chanlist = ast_channel_dialed_causes_channels(chan);
	ast_channel_unlock(chan);

	if (chanlist && ast_str_strlen(chanlist)) {
		ast_copy_string(buf, ast_str_buffer(chanlist), len);
	}

	ast_free(chanlist);
	return 0;
}

/*!
 * \internal
 * \brief Remove all keys from the hangupcause ao2 container.
 *
 * \param chan Asterisk channel to read
 * \param data Not used
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int hangupcause_clear_exec(struct ast_channel *chan, const char *data) {
	ast_channel_lock(chan);
	ast_channel_dialed_causes_clear(chan);
	ast_channel_unlock(chan);
	return 0;
}

static struct ast_custom_function hangupcause_function = {
	.name = "HANGUPCAUSE",
	.read = hangupcause_read,
};

static struct ast_custom_function hangupcause_keys_function = {
	.name = "HANGUPCAUSE_KEYS",
	.read = hangupcause_keys_read,
};

static const char app[] = "HangupCauseClear";

/*!
 * \internal
 * \brief Unload the function module
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&hangupcause_function);
	res |= ast_custom_function_unregister(&hangupcause_keys_function);
	res |= ast_unregister_application(app);
	return res;
}

/*!
 * \internal
 * \brief Load and initialize the function module.
 *
 * \retval AST_MODULE_LOAD_SUCCESS on success.
 * \retval AST_MODULE_LOAD_DECLINE on error.
 */
static int load_module(void)
{
	int res;

	res = ast_custom_function_register(&hangupcause_function);
	res |= ast_custom_function_register(&hangupcause_keys_function);
	res |= ast_register_application_xml(app, hangupcause_clear_exec);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

/* Do not wrap the following line. */
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "HANGUPCAUSE related functions and applications");
