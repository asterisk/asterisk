/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief App to send MF digits
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/indications.h"

/*** DOCUMENTATION
	<application name="SendMF" language="en_US">
		<synopsis>
			Sends arbitrary MF digits
		</synopsis>
		<syntax>
			<parameter name="digits" required="true">
				<para>List of digits 0-9,*#ABC to send; also f or F for a flash-hook
				if the channel supports flash-hook, and w or W for a wink if the channel
				supports wink.</para>
				<para>Key pulse and start digits are not included automatically.
				* is used for KP, # for ST, A for STP, B for ST2P, and C for ST3P.</para>
			</parameter>
			<parameter name="timeout_ms" required="false">
				<para>Amount of time to wait in ms between tones. (defaults to 50ms).</para>
			</parameter>
			<parameter name="duration_ms" required="false">
				<para>Duration of each numeric digit (defaults to 55ms).</para>
			</parameter>
			<parameter name="duration_ms_kp" required="false">
				<para>Duration of KP digits (defaults to 120ms).</para>
			</parameter>
			<parameter name="duration_ms_st" required="false">
				<para>Duration of ST, STP, ST2P, and ST3P digits (defaults to 65ms).</para>
			</parameter>
			<parameter name="channel" required="false">
				<para>Channel where digits will be played</para>
			</parameter>
		</syntax>
		<description>
			<para>It will send all digits or terminate if it encounters an error.</para>
		</description>
		<see-also>
			<ref type="application">SendDTMF</ref>
		</see-also>
	</application>
	<manager name="PlayMF" language="en_US">
		<synopsis>
			Play MF signal on a specific channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to send digit to.</para>
			</parameter>
			<parameter name="Digit" required="true">
				<para>The MF digit to play.</para>
			</parameter>
			<parameter name="Duration" required="false">
				<para>The duration, in milliseconds, of the digit to be played.</para>
			</parameter>
		</syntax>
		<description>
			<para>Plays an MF digit on the specified channel.</para>
		</description>
	</manager>
 ***/

static const char sendmf_name[] = "SendMF";

#define DEFAULT_EMULATE_MF_DURATION 35
#define MF_BETWEEN_MS 50
#define MF_DURATION 55
#define MF_KP_DURATION 120
#define MF_ST_DURATION 65

static int senddigit_mf_begin(struct ast_channel *chan, char digit)
{
	static const char * const mf_tones[] = {
		"1300+1500", /* 0 */
		"700+900",   /* 1 */
		"700+1100",  /* 2 */
		"900+1100",  /* 3 */
		"700+1300",  /* 4 */
		"900+1300",  /* 5 */
		"1100+1300", /* 6 */
		"700+1500",  /* 7 */
		"900+1500",  /* 8 */
		"1100+1500", /* 9 */
		"1100+1700", /* * (KP) */
		"1500+1700", /* # (ST) */
		"900+1700",  /* A (STP) */
		"1300+1700", /* B (ST2P) */
		"700+1700"   /* C (ST3P) */
	};

	if (digit >= '0' && digit <='9') {
		ast_playtones_start(chan, 0, mf_tones[digit-'0'], 0);
	} else if (digit == '*') {
		ast_playtones_start(chan, 0, mf_tones[10], 0);
	} else if (digit == '#') {
		ast_playtones_start(chan, 0, mf_tones[11], 0);
	} else if (digit == 'A') {
		ast_playtones_start(chan, 0, mf_tones[12], 0);
	} else if (digit == 'B') {
		ast_playtones_start(chan, 0, mf_tones[13], 0);
	} else if (digit == 'C') {
		ast_playtones_start(chan, 0, mf_tones[14], 0);
	} else {
		/* not handled */
		ast_log(LOG_WARNING, "Unable to generate MF tone '%c' for '%s'\n", digit, ast_channel_name(chan));
	}

	return 0;
}

static int senddigit_mf_end(struct ast_channel *chan)
{
	if (ast_channel_generator(chan)) {
		ast_playtones_stop(chan);
		return 0;
	}
	return -1;
}

static int mysleep(struct ast_channel *chan, int ms, int is_external)
{
	return is_external ? usleep(ms * 1000) : ast_safe_sleep(chan, ms);
}

static int senddigit_mf(struct ast_channel *chan, char digit, unsigned int duration,
	unsigned int durationkp, unsigned int durationst, int is_external)
{
	if (duration < DEFAULT_EMULATE_MF_DURATION) {
		duration = DEFAULT_EMULATE_MF_DURATION;
	}
	if (ast_channel_tech(chan)->send_digit_begin) {
		if (digit == '*') {
			duration = durationkp;
		} else if (digit == '#' || digit == 'A' || digit == 'B' || digit == 'C') {
			duration = durationst;
		}
		senddigit_mf_begin(chan, digit);
		mysleep(chan, duration, is_external);
	}
	return senddigit_mf_end(chan);
}

static int mf_stream(struct ast_channel *chan, const char *digits, int between, unsigned int duration,
	unsigned int durationkp, unsigned int durationst, int is_external)
{
	const char *ptr;
	int res;
	struct ast_silence_generator *silgen = NULL;

	if (!between) {
		between = 100;
	}

	/* Need a quiet time before sending digits. */
	if (ast_opt_transmit_silence) {
		silgen = ast_channel_start_silence_generator(chan);
	}
	res = mysleep(chan, 100, is_external);
	if (res) {
		goto mf_stream_cleanup;
	}

	for (ptr = digits; *ptr; ptr++) {
		if (strchr("0123456789*#ABCwWfF", *ptr)) {
			if (*ptr == 'f' || *ptr == 'F') {
				/* ignore return values if not supported by channel */
				ast_indicate(chan, AST_CONTROL_FLASH);
			} else if (*ptr == 'w' || *ptr == 'W') {
				/* ignore return values if not supported by channel */
				ast_indicate(chan, AST_CONTROL_WINK);
			} else {
				/* Character represents valid MF */
				senddigit_mf(chan, *ptr, duration, durationkp, durationst, is_external);
			}
			/* pause between digits */
			/* The DSP code in Asterisk does not currently properly receive repeated tones
				if no audio is sent in the middle. Simply sending audio (even 0 Hz)
				works around this limitation and guarantees the correct behavior.
				*/
			ast_playtones_start(chan, 0, "0", 0);
			res = mysleep(chan, between, is_external);
			senddigit_mf_end(chan);
			if (res) {
				break;
			}
		} else {
			ast_log(LOG_WARNING, "Illegal MF character '%c' in string. (0-9*#ABCwWfF allowed)\n", *ptr);
		}
	}

mf_stream_cleanup:
	if (silgen) {
		ast_channel_stop_silence_generator(chan, silgen);
	}

	return res;
}

static int sendmf_exec(struct ast_channel *chan, const char *vdata)
{
	int res;
	char *data;
	int dinterval = 0, duration = 0, durationkp = 0, durationst = 0;
	struct ast_channel *chan_found = NULL;
	struct ast_channel *chan_dest = chan;
	struct ast_channel *chan_autoservice = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(digits);
		AST_APP_ARG(dinterval);
		AST_APP_ARG(duration);
		AST_APP_ARG(durationkp);
		AST_APP_ARG(durationst);
		AST_APP_ARG(channel);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "SendMF requires an argument\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.digits)) {
		ast_log(LOG_WARNING, "The digits argument is required (0-9,*#ABC,wf)\n");
		return 0;
	}
	if (!ast_strlen_zero(args.dinterval)) {
		ast_app_parse_timelen(args.dinterval, &dinterval, TIMELEN_MILLISECONDS);
	}
	if (!ast_strlen_zero(args.duration)) {
		ast_app_parse_timelen(args.duration, &duration, TIMELEN_MILLISECONDS);
	}
	if (!ast_strlen_zero(args.durationkp)) {
		ast_app_parse_timelen(args.durationkp, &durationkp, TIMELEN_MILLISECONDS);
	}
	if (!ast_strlen_zero(args.durationst)) {
		ast_app_parse_timelen(args.durationst, &durationst, TIMELEN_MILLISECONDS);
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
	if (chan_autoservice && ast_autoservice_start(chan_autoservice)) {
		ast_channel_cleanup(chan_found);
		return -1;
	}
	res = mf_stream(chan_dest, args.digits, dinterval <= 0 ? MF_BETWEEN_MS : dinterval,
		duration <= 0 ? MF_DURATION : duration, durationkp <= 0 ? MF_KP_DURATION : durationkp,
		durationst <= 0 ? MF_ST_DURATION : durationst, 0);
	if (chan_autoservice && ast_autoservice_stop(chan_autoservice)) {
		res = -1;
	}
	ast_channel_cleanup(chan_found);

	return chan_autoservice ? 0 : res;
}

static int manager_play_mf(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *digit = astman_get_header(m, "Digit");
	const char *duration = astman_get_header(m, "Duration");
	struct ast_channel *chan;
	unsigned int duration_ms = MF_DURATION;

	if (!(chan = ast_channel_get_by_name(channel))) {
		astman_send_error(s, m, "Channel not found");
		return 0;
	}

	if (ast_strlen_zero(digit)) {
		astman_send_error(s, m, "No digit specified");
		chan = ast_channel_unref(chan);
		return 0;
	}

	/* Override default duration with KP or ST-specific default durations */
	if (!strcmp(digit, "*"))
		duration_ms = MF_KP_DURATION;

	if (!strcmp(digit, "#") || !strcmp(digit, "A") || !strcmp(digit, "B") || !strcmp(digit, "C"))
		duration_ms = MF_ST_DURATION;

	if (!ast_strlen_zero(duration) && (sscanf(duration, "%30u", &duration_ms) != 1)) {
		astman_send_error(s, m, "Could not convert Duration parameter");
		chan = ast_channel_unref(chan);
		return 0;
	}

	senddigit_mf(chan, *digit, duration_ms, duration_ms, duration_ms, 1);
	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "MF successfully queued");

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(sendmf_name);
	res |= ast_manager_unregister("PlayMF");

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_manager_register_xml("PlayMF", EVENT_FLAG_CALL, manager_play_mf);
	res |= ast_register_application_xml(sendmf_name, sendmf_exec);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Send MF digits Application");
