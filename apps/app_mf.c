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
 * \brief MF sender and receiver applications
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"
#include "asterisk/conversions.h"

/*** DOCUMENTATION
	<application name="ReceiveMF" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Detects MF digits on a channel and saves them to a variable.
		</synopsis>
		<syntax>
			<parameter name="variable" required="true">
				<para>The input digits will be stored in the given
				<replaceable>variable</replaceable> name.</para>
			</parameter>
			<parameter name="timeout">
				<para>The number of seconds to wait for all digits, if greater
				than <literal>0</literal>. Can be floating point. Default
				is no timeout.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="d">
						<para>Delay audio by a frame to try to extra quelch.</para>
					</option>
					<option name="l">
						<para>Receive digits even if a key pulse (KP) has not yet
						been received. By default, this application will ignore
						all other digits until a KP has been received.</para>
					</option>
					<option name="k">
						<para>Do not return a character for the KP digit.</para>
					</option>
					<option name="m">
						<para>Mute conference.</para>
					</option>
					<option name="n">
						<para>Maximum number of digits, regardless of the sequence.</para>
					</option>
					<option name="o">
						<para>Enable override. Repeated KPs will clear all previous digits.</para>
					</option>
					<option name="q">
						<para>Quelch MF from in-band.</para>
					</option>
					<option name="r">
						<para>"Radio" mode (relaxed MF).</para>
					</option>
					<option name="s">
						<para>Do not return a character for ST digits.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Reads a ST, STP, ST2P, or ST3P-terminated string of MF digits from
			the user in to the given <replaceable>variable</replaceable>.</para>
			<para>This application does not automatically answer the channel and
			should be preceded with <literal>Answer</literal> or
			<literal>Progress</literal> as needed.</para>
			<variablelist>
				<variable name="RECEIVEMFSTATUS">
					<para>This is the status of the read operation.</para>
					<value name="START" />
					<value name="ERROR" />
					<value name="HANGUP" />
					<value name="MAXDIGITS" />
					<value name="TIMEOUT" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Read</ref>
			<ref type="application">SendMF</ref>
			<ref type="application">ReceiveSF</ref>
		</see-also>
	</application>
	<application name="SendMF" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Sends arbitrary MF digits on the current or specified channel.
		</synopsis>
		<syntax>
			<parameter name="digits" required="true">
				<para>List of digits 0-9,*#ABC to send; w for a half-second pause,
				also f or F for a flash-hook if the channel supports flash-hook,
				h or H for 250 ms of 2600 Hz,
				and W for a wink if the channel supports wink.</para>
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
			<ref type="application">ReceiveMF</ref>
			<ref type="application">SendSF</ref>
			<ref type="application">SendDTMF</ref>
		</see-also>
	</application>
	<manager name="PlayMF" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Play MF digit on a specific channel.
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

enum read_option_flags {
	OPT_DELAY = (1 << 0),
	OPT_MUTE = (1 << 1),
	OPT_QUELCH = (1 << 2),
	OPT_RELAXED = (1 << 3),
	OPT_LAX_KP = (1 << 4),
	OPT_PROCESS = (1 << 5),
	OPT_NO_KP = (1 << 6),
	OPT_NO_ST = (1 << 7),
	OPT_KP_OVERRIDE = (1 << 8),
	OPT_MAXDIGITS = (1 << 9),
};

enum {
	OPT_ARG_MAXDIGITS,
	/* Must be the last element */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(read_app_options, {
	AST_APP_OPTION('d', OPT_DELAY),
	AST_APP_OPTION('l', OPT_LAX_KP),
	AST_APP_OPTION('k', OPT_NO_KP),
	AST_APP_OPTION('m', OPT_MUTE),
	AST_APP_OPTION_ARG('n', OPT_MAXDIGITS, OPT_ARG_MAXDIGITS),
	AST_APP_OPTION('o', OPT_KP_OVERRIDE),
	AST_APP_OPTION('p', OPT_PROCESS),
	AST_APP_OPTION('q', OPT_QUELCH),
	AST_APP_OPTION('r', OPT_RELAXED),
	AST_APP_OPTION('s', OPT_NO_ST),
});

static const char *readmf_name = "ReceiveMF";
static const char sendmf_name[] = "SendMF";

#define MF_BETWEEN_MS 50
#define MF_DURATION 55
#define MF_KP_DURATION 120
#define MF_ST_DURATION 65

/*!
 * \brief Detects MF digits on channel using DSP, terminated by ST, STP, ST2P, or ST3P
 *
 * \param chan channel on which to read digits
 * \param buf Buffer in which to store digits
 * \param buflen Size of buffer
 * \param timeout ms to wait for all digits before giving up
 * \param features Any additional DSP features to use
 * \param override Start over if we receive additional KPs
 * \param no_kp Don't include KP in the output
 * \param no_st Don't include start digits in the output
 * \param maxdigits If greater than 0, only read this many digits no matter what
 *
 * \retval 0 if successful
 * \retval -1 if unsuccessful (including hangup).
 */
static int read_mf_digits(struct ast_channel *chan, char *buf, int buflen, int timeout, int features, int laxkp, int override, int no_kp, int no_st, int maxdigits) {
	struct ast_dsp *dsp;
	struct ast_frame *frame = NULL;
	struct timeval start;
	int remaining_time = timeout;
	int digits_read = 0;
	int is_start_digit = 0;
	char *str = buf;
	int res = 0;

	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		pbx_builtin_setvar_helper(chan, "RECEIVEMFSTATUS", "ERROR");
		return -1;
	}
	ast_dsp_set_features(dsp, DSP_FEATURE_DIGIT_DETECT);
	ast_dsp_set_digitmode(dsp, DSP_DIGITMODE_MF | features);

	start = ast_tvnow();
	*str = 0; /* start with empty output buffer */

	/* based on app_read and generic_fax_exec from res_fax */
	while (timeout == 0 || remaining_time > 0) {
		if (timeout > 0) {
			remaining_time = ast_remaining_ms(start, timeout);
			if (remaining_time <= 0) {
				pbx_builtin_setvar_helper(chan, "RECEIVEMFSTATUS", "TIMEOUT");
				break;
			}
		}
		if ((maxdigits && digits_read >= maxdigits) || digits_read >= (buflen - 1)) { /* we don't have room to store any more digits (very unlikely to happen for a legitimate reason) */
			/* This result will probably not be usable, so status should not be START */
			pbx_builtin_setvar_helper(chan, "RECEIVEMFSTATUS", "MAXDIGITS");
			break;
		}
		/* ast_waitfordigit only waits for DTMF frames, we need to do DSP on voice frames */
		if (ast_waitfor(chan, 1000) > 0) {
			frame = ast_read(chan);
			if (!frame) {
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", ast_channel_name(chan));
				pbx_builtin_setvar_helper(chan, "RECEIVEMFSTATUS", "HANGUP");
				break;
			} else if (frame->frametype == AST_FRAME_VOICE) {
				frame = ast_dsp_process(chan, dsp, frame);
				/* AST_FRAME_DTMF is used all over the DSP code for DTMF, MF, fax, etc.
					It's used because we can use the frame to store the digit detected.
					All this means is that we received something we care about. */
				if (frame->frametype == AST_FRAME_DTMF) {
					char result = frame->subclass.integer;
					if (digits_read == 0 && !laxkp && result != '*') {
						ast_debug(1, "Received MF digit, but no KP yet, ignoring: %c\n", result);
						ast_frfree(frame);
						continue;
					}
					ast_debug(1, "Received MF digit: %c\n", result);
					if (result == '*') {
						/* We received an additional KP, start over? */
						if (override && digits_read > 0) {
							ast_debug(1, "Received another KP, starting over\n");
							str = buf;
							*str = 0;
							digits_read = 1; /* we just detected a KP */
						} else {
							digits_read++;
						}
						/* if we were told not to include the KP digit in the output string, then skip it */
						if (no_kp) {
							ast_frfree(frame);
							continue;
						}
					} else {
						digits_read++;
					}
					is_start_digit = (strchr("#", result) || strchr("A", result) || strchr("B", result) || strchr("C", result));
					/* if we were told not to include the ST digit in the output string, then skip it */
					if (!no_st || !is_start_digit) {
						*str++ = result; /* won't write past allotted memory, because of buffer check at top of loop */
						*str = 0;
					}
					/* we received a ST digit (ST, STP, ST2P, or ST3P), so we're done */
					if (is_start_digit) {
						pbx_builtin_setvar_helper(chan, "RECEIVEMFSTATUS", "START");
						ast_frfree(frame);
						break;
					}
					/* only free frame if it was a DSP match. The MF itself should not be muted. */
					ast_frfree(frame);
				}
			}
		} else {
			pbx_builtin_setvar_helper(chan, "RECEIVEMFSTATUS", "HANGUP");
			res = -1;
		}
	}
	ast_dsp_free(dsp);
	ast_debug(3, "channel '%s' - event loop stopped { timeout: %d, remaining_time: %d }\n", ast_channel_name(chan), timeout, remaining_time);
	return res;
}

static int read_mf_exec(struct ast_channel *chan, const char *data)
{
#define BUFFER_SIZE 256
	char tmp[BUFFER_SIZE] = "";
	int to = 0;
	double tosec;
	struct ast_flags flags = {0};
	char *optargs[OPT_ARG_ARRAY_SIZE];
	char *argcopy = NULL;
	int res, features = 0, maxdigits = 0;

	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(variable);
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ReceiveMF requires an argument (variable)\n");
		return -1;
	}

	argcopy = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(arglist, argcopy);

	if (!ast_strlen_zero(arglist.options)) {
		ast_app_parse_options(read_app_options, &flags, optargs, arglist.options);
	}

	if (!ast_strlen_zero(arglist.timeout)) {
		tosec = atof(arglist.timeout);
		if (tosec <= 0) {
			to = 0;
		} else {
			to = tosec * 1000.0;
		}
	}

	if (ast_strlen_zero(arglist.variable)) {
		ast_log(LOG_WARNING, "Invalid! Usage: ReceiveMF(variable[,timeout][,option])\n");
		return -1;
	}
	if (ast_test_flag(&flags, OPT_MAXDIGITS) && !ast_strlen_zero(optargs[OPT_ARG_MAXDIGITS])) {
		maxdigits = atoi(optargs[OPT_ARG_MAXDIGITS]);
		if (maxdigits <= 0) {
			ast_log(LOG_WARNING, "Invalid maximum number of digits, ignoring: '%s'\n", optargs[OPT_ARG_MAXDIGITS]);
			maxdigits = 0;
		}
	}

	if (ast_test_flag(&flags, OPT_DELAY)) {
		features |= DSP_DIGITMODE_MUTEMAX;
	}

	if (ast_test_flag(&flags, OPT_MUTE)) {
		features |= DSP_DIGITMODE_MUTECONF;
	}

	if (!ast_test_flag(&flags, OPT_QUELCH)) {
		features |= DSP_DIGITMODE_NOQUELCH;
	}

	if (ast_test_flag(&flags, OPT_RELAXED)) {
		features |= DSP_DIGITMODE_RELAXDTMF;
	}

	res = read_mf_digits(chan, tmp, BUFFER_SIZE, to, features, (ast_test_flag(&flags, OPT_LAX_KP)),
		(ast_test_flag(&flags, OPT_KP_OVERRIDE)), (ast_test_flag(&flags, OPT_NO_KP)), (ast_test_flag(&flags, OPT_NO_ST)), maxdigits);
	pbx_builtin_setvar_helper(chan, arglist.variable, tmp);
	if (!ast_strlen_zero(tmp)) {
		ast_verb(3, "MF digits received: '%s'\n", tmp);
	} else if (!res) { /* if channel hung up, don't print anything out */
		ast_verb(3, "No MF digits received.\n");
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
	res = ast_mf_stream(chan_dest, chan_autoservice, NULL, args.digits, dinterval <= 0 ? MF_BETWEEN_MS : dinterval,
		duration <= 0 ? MF_DURATION : duration, durationkp <= 0 ? MF_KP_DURATION : durationkp,
		durationst <= 0 ? MF_ST_DURATION : durationst, 0);
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

	ast_mf_stream(chan, NULL, NULL, digit, 0, duration_ms, duration_ms, duration_ms, 1);

	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "MF successfully queued");

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(readmf_name);
	res |= ast_unregister_application(sendmf_name);
	res |= ast_manager_unregister("PlayMF");

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(readmf_name, read_mf_exec);
	res |= ast_register_application_xml(sendmf_name, sendmf_exec);
	res |= ast_manager_register_xml("PlayMF", EVENT_FLAG_CALL, manager_play_mf);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "MF Sender and Receiver Applications");
