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
 * \brief App to send DTMF digits
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<application name="SendDTMF" language="en_US">
		<synopsis>
			Sends arbitrary DTMF digits
		</synopsis>
		<syntax>
			<parameter name="digits" required="true">
				<para>List of digits 0-9,*#,a-d,A-D to send also w for a half second pause,
				W for a one second pause, and f or F for a flash-hook if the channel supports
				flash-hook.</para>
			</parameter>
			<parameter name="timeout_ms" required="false">
				<para>Amount of time to wait in ms between tones. (defaults to .25s)</para>
			</parameter>
			<parameter name="duration_ms" required="false">
				<para>Duration of each digit</para>
			</parameter>
			<parameter name="channel" required="false">
				<para>Channel where digits will be played</para>
			</parameter>
		</syntax>
		<description>
			<para>It will send all digits or terminate if it encounters an error.</para>
		</description>
		<see-also>
			<ref type="application">Read</ref>
		</see-also>
	</application>
	<manager name="PlayDTMF" language="en_US">
		<synopsis>
			Play DTMF signal on a specific channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to send digit to.</para>
			</parameter>
			<parameter name="Digit" required="true">
				<para>The DTMF digit to play.</para>
			</parameter>
			<parameter name="Duration" required="false">
				<para>The duration, in milliseconds, of the digit to be played.</para>
			</parameter>
			<parameter name="Receive" required="false">
				<para>Emulate receiving DTMF on this channel instead of sending it out.</para>
			</parameter>
		</syntax>
		<description>
			<para>Plays a dtmf digit on the specified channel.</para>
		</description>
	</manager>
 ***/

static const char senddtmf_name[] = "SendDTMF";

static int senddtmf_exec(struct ast_channel *chan, const char *vdata)
{
	int res;
	char *data;
	int dinterval = 0, duration = 0;
	struct ast_channel *chan_found = NULL;
	struct ast_channel *chan_dest = chan;
	struct ast_channel *chan_autoservice = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(digits);
		AST_APP_ARG(dinterval);
		AST_APP_ARG(duration);
		AST_APP_ARG(channel);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "SendDTMF requires an argument\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.digits)) {
		ast_log(LOG_WARNING, "The digits argument is required (0-9,*#,a-d,A-D,wfF)\n");
		return 0;
	}
	if (!ast_strlen_zero(args.dinterval)) {
		ast_app_parse_timelen(args.dinterval, &dinterval, TIMELEN_MILLISECONDS);
	}
	if (!ast_strlen_zero(args.duration)) {
		ast_app_parse_timelen(args.duration, &duration, TIMELEN_MILLISECONDS);
	}
	if (!ast_strlen_zero(args.channel)) {
		chan_found = ast_channel_get_by_name(args.channel);
		if (!chan_found) {
			ast_log(LOG_WARNING, "No such channel: %s\n", args.channel);
			return 0;
		}
		chan_dest = chan_found;
		if (chan_found != chan) {
			chan_autoservice = chan;
		}
	}
	res = ast_dtmf_stream(chan_dest, chan_autoservice, args.digits,
		dinterval <= 0 ? 250 : dinterval, duration);
	if (chan_found) {
		ast_channel_unref(chan_found);
	}

	return chan_autoservice ? 0 : res;
}

static int manager_play_dtmf(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *digit = astman_get_header(m, "Digit");
	const char *duration = astman_get_header(m, "Duration");
	const char *receive_s = astman_get_header(m, "Receive");
	struct ast_channel *chan;
	unsigned int duration_ms = 0;

	if (!(chan = ast_channel_get_by_name(channel))) {
		astman_send_error(s, m, "Channel not found");
		return 0;
	}

	if (ast_strlen_zero(digit)) {
		astman_send_error(s, m, "No digit specified");
		chan = ast_channel_unref(chan);
		return 0;
	}

	if (!ast_strlen_zero(duration) && (sscanf(duration, "%30u", &duration_ms) != 1)) {
		astman_send_error(s, m, "Could not convert Duration parameter");
		chan = ast_channel_unref(chan);
		return 0;
	}

	if (ast_true(receive_s)) {
		struct ast_frame f = { AST_FRAME_DTMF, };
		f.len = duration_ms;
		f.subclass.integer = *digit;
		ast_queue_frame(chan, &f);
	} else {
		ast_senddigit_external(chan, *digit, duration_ms);
	}

	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "DTMF successfully queued");

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(senddtmf_name);
	res |= ast_manager_unregister("PlayDTMF");

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_manager_register_xml("PlayDTMF", EVENT_FLAG_CALL, manager_play_dtmf);
	res |= ast_register_application_xml(senddtmf_name, senddtmf_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Send DTMF digits Application");
