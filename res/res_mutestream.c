/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Olle E. Johansson
 *
 * Olle E. Johansson <oej@edvina.net>
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
 * \brief MUTESTREAM audiohooks
 *
 * \author Olle E. Johansson <oej@edvina.net>
 *
 *  \ingroup functions
 *
 * \note This module only handles audio streams today, but can easily be appended to also
 * zero out text streams if there's an application for it.
 * When we know and understands what happens if we zero out video, we can do that too.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"
#include "asterisk/manager.h"

/*** DOCUMENTATION
	<function name="MUTEAUDIO" language="en_US">
		<synopsis>
			Muting audio streams in the channel
		</synopsis>
		<syntax>
			<parameter name="direction" required="true">
				<para>Must be one of </para>
				<enumlist>
					<enum name="in">
						<para>Inbound stream (to the PBX)</para>
					</enum>
					<enum name="out">
						<para>Outbound stream (from the PBX)</para>
					</enum>
					<enum name="all">
						<para>Both streams</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>The MUTEAUDIO function can be used to mute inbound (to the PBX) or outbound audio in a call.
			</para>
			<para>Examples:
			</para>
			<para>
			MUTEAUDIO(in)=on
			</para>
			<para>
			MUTEAUDIO(in)=off
			</para>
		</description>
	</function>
	<manager name="MuteAudio" language="en_US">
		<synopsis>
			Mute an audio stream.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>The channel you want to mute.</para>
			</parameter>
			<parameter name="Direction" required="true">
				<enumlist>
					<enum name="in">
						<para>Set muting on inbound audio stream. (to the PBX)</para>
					</enum>
					<enum name="out">
						<para>Set muting on outbound audio stream. (from the PBX)</para>
					</enum>
					<enum name="all">
						<para>Set muting on inbound and outbound audio streams.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="State" required="true">
				<enumlist>
					<enum name="on">
						<para>Turn muting on.</para>
					</enum>
					<enum name="off">
						<para>Turn muting off.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Mute an incoming or outgoing audio stream on a channel.</para>
		</description>
	</manager>
 ***/


static int mute_channel(struct ast_channel *chan, const char *direction, int mute)
{
	unsigned int mute_direction = 0;
	enum ast_frame_type frametype = AST_FRAME_VOICE;
	int ret = 0;

	if (!strcmp(direction, "in")) {
		mute_direction = AST_MUTE_DIRECTION_READ;
	} else if (!strcmp(direction, "out")) {
		mute_direction = AST_MUTE_DIRECTION_WRITE;
	} else if (!strcmp(direction, "all")) {
		mute_direction = AST_MUTE_DIRECTION_READ | AST_MUTE_DIRECTION_WRITE;
	} else {
		return -1;
	}

	ast_channel_lock(chan);

	if (mute) {
		ret = ast_channel_suppress(chan, mute_direction, frametype);
	} else {
		ret = ast_channel_unsuppress(chan, mute_direction, frametype);
	}

	ast_channel_unlock(chan);

	return ret;
}

/*! \brief Mute dialplan function */
static int func_mute_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	return mute_channel(chan, data, ast_true(value));
}

/* Function for debugging - might be useful */
static struct ast_custom_function mute_function = {
	.name = "MUTEAUDIO",
	.write = func_mute_write,
};

static int manager_mutestream(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m,"ActionID");
	const char *state = astman_get_header(m,"State");
	const char *direction = astman_get_header(m,"Direction");
	char id_text[256];
	struct ast_channel *c = NULL;

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (ast_strlen_zero(state)) {
		astman_send_error(s, m, "State not specified");
		return 0;
	}
	if (ast_strlen_zero(direction)) {
		astman_send_error(s, m, "Direction not specified");
		return 0;
	}
	/* Ok, we have everything */

	c = ast_channel_get_by_name(channel);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	if (mute_channel(c, direction, ast_true(state))) {
		astman_send_error(s, m, "Failed to mute/unmute stream");
		ast_channel_unref(c);
		return 0;
	}

	ast_channel_unref(c);

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}
	astman_append(s, "Response: Success\r\n"
		"%s"
		"\r\n", id_text);
	return 0;
}


static int load_module(void)
{
	int res;

	res = ast_custom_function_register(&mute_function);
	res |= ast_manager_register_xml("MuteAudio", EVENT_FLAG_SYSTEM, manager_mutestream);

	return (res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS);
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Mute audio stream resources");
