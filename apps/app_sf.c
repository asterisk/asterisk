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
 * \brief SF sender and receiver applications
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
	<application name="ReceiveSF" language="en_US">
		<since>
			<version>16.24.0</version>
			<version>18.10.0</version>
			<version>19.2.0</version>
		</since>
		<synopsis>
			Detects SF digits on a channel and saves them to a variable.
		</synopsis>
		<syntax>
			<parameter name="variable" required="true">
				<para>The input digits will be stored in the given
				<replaceable>variable</replaceable> name.</para>
			</parameter>
			<parameter name="digits" required="false">
				<para>Maximum number of digits to read. Default is unlimited.</para>
			</parameter>
			<parameter name="timeout">
				<para>The number of seconds to wait for all digits, if greater
				than <literal>0</literal>. Can be floating point. Default
				is no timeout.</para>
			</parameter>
			<parameter name="frequency">
				<para>The frequency for which to detect pulsed digits.
				Default is 2600 Hz.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="d">
						<para>Delay audio by a frame to try to extra quelch.</para>
					</option>
					<option name="e">
						<para>Allow receiving extra pulses 11 through 16.</para>
					</option>
					<option name="m">
						<para>Mute conference.</para>
					</option>
					<option name="q">
						<para>Quelch SF from in-band.</para>
					</option>
					<option name="r">
						<para>"Radio" mode (relaxed SF).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Reads SF digits from the user in to the given
			<replaceable>variable</replaceable>.</para>
			<para>This application does not automatically answer the channel and
			should be preceded with <literal>Answer</literal> or
			<literal>Progress</literal> as needed.</para>
			<variablelist>
				<variable name="RECEIVESFSTATUS">
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
			<ref type="application">ReceiveMF</ref>
			<ref type="application">SendMF</ref>
			<ref type="application">SendSF</ref>
			<ref type="application">Read</ref>
		</see-also>
	</application>
	<application name="SendSF" language="en_US">
		<since>
			<version>16.24.0</version>
			<version>18.10.0</version>
			<version>19.2.0</version>
		</since>
		<synopsis>
			Sends arbitrary SF digits on the current or specified channel.
		</synopsis>
		<syntax>
			<parameter name="digits" required="true">
				<para>List of digits 0-9 to send; w for a half-second pause,
				also f or F for a flash-hook if the channel supports flash-hook,
				h or H for 250 ms of 2600 Hz, and W for a wink if the channel
				supports wink.</para>
			</parameter>
			<parameter name="frequency" required="false">
				<para>Frequency to use. (defaults to 2600 Hz).</para>
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
			<ref type="application">SendMF</ref>
			<ref type="application">ReceiveMF</ref>
			<ref type="application">ReceiveSF</ref>
		</see-also>
	</application>
 ***/

enum read_option_flags {
	OPT_DELAY = (1 << 0),
	OPT_MUTE = (1 << 1),
	OPT_QUELCH = (1 << 2),
	OPT_RELAXED = (1 << 3),
	OPT_EXTRAPULSES = (1 << 4),
};

AST_APP_OPTIONS(read_app_options, {
	AST_APP_OPTION('d', OPT_DELAY),
	AST_APP_OPTION('e', OPT_EXTRAPULSES),
	AST_APP_OPTION('m', OPT_MUTE),
	AST_APP_OPTION('q', OPT_QUELCH),
	AST_APP_OPTION('r', OPT_RELAXED),
});

static const char *readsf_name = "ReceiveSF";
static const char sendsf_name[] = "SendSF";

/*!
 * \brief Detects SF digits on channel using DSP
 *
 * \param chan channel on which to read digits
 * \param buf Buffer in which to store digits
 * \param buflen Size of buffer
 * \param timeout ms to wait for all digits before giving up
 * \param maxdigits Maximum number of digits
 * \param freq Frequency to use
 * \param features DSP features
 * \param extrapulses Whether to recognize extra pulses
 *
 * \retval 0 if successful
 * \retval -1 if unsuccessful (including hangup).
 */
static int read_sf_digits(struct ast_channel *chan, char *buf, int buflen, int timeout, int maxdigits, int freq, int features, int extrapulses) {
	/* Bell System Technical Journal 39 (Nov. 1960) */
	#define SF_MIN_OFF 25
	#define SF_ON 67
	#define SF_BETWEEN 600
	#define SF_MIN_DETECT 50

	struct ast_dsp *dsp = NULL;
	struct ast_frame *frame = NULL;
	struct timeval start, pulsetimer, digittimer;
	int remaining_time = timeout;
	char *str = buf;
	int hits = 0, digits_read = 0;
	unsigned short int sf_on = 0;
	int res = 0;

	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "ERROR");
		return -1;
	}
	ast_dsp_set_features(dsp, DSP_FEATURE_FREQ_DETECT);
	/* tolerance is 46 to 76% make break at 8 to 12 pps */
	ast_dsp_set_freqmode(dsp, freq, SF_MIN_DETECT, 16, 0);

	start = ast_tvnow();
	*str = 0; /* start with empty output buffer */

	while (timeout == 0 || remaining_time > 0) {
		if (timeout > 0) {
			remaining_time = ast_remaining_ms(start, timeout);
			if (remaining_time <= 0) {
				pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "TIMEOUT");
				break;
			}
		}
		if (digits_read >= (buflen - 1)) { /* we don't have room to store any more digits (very unlikely to happen for a legitimate reason) */
			/* This result will probably not be usable, so status should not be START */
			pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "MAXDIGITS");
			break;
		}
		if (ast_waitfor(chan, 1000) > 0) {
			frame = ast_read(chan);
			if (!frame) {
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", ast_channel_name(chan));
				pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "HANGUP");
				break;
			} else if (frame->frametype == AST_FRAME_VOICE) {
				frame = ast_dsp_process(chan, dsp, frame);
				if (frame->frametype == AST_FRAME_DTMF) {
					char result = frame->subclass.integer;
					if (result == 'q') {
						sf_on = 1;
						pulsetimer = ast_tvnow(); /* reset the pulse timer */
						/* now, we need at least a 33ms pause to register the pulse */
					}
				} else {
					if (sf_on) {
						int timeleft = ast_remaining_ms(pulsetimer, SF_MIN_OFF);
						if (timeleft <= 0) {
							sf_on = 0;
							/* The pulse needs to end no more than 30ms after we detected it */
							if (timeleft > -30) {
								hits++;
								digittimer = ast_tvnow(); /* reset the digit timer */
								ast_debug(5, "Detected SF pulse (pulse #%d)\n", hits);
								ast_dsp_free(dsp);
								if (!(dsp = ast_dsp_new())) {
									ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
									pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "ERROR");
									ast_frfree(frame);
									return -1;
								}
								ast_dsp_set_features(dsp, DSP_FEATURE_FREQ_DETECT);
								ast_dsp_set_freqmode(dsp, freq, SF_MIN_DETECT, 16, 0);
							} else {
								ast_debug(5, "SF noise, ignoring, time elapsed was %d ms\n", timeleft);
							}
						}
					} else if (hits > 0 && ast_remaining_ms(digittimer, SF_BETWEEN) <= 0) {
						/* has the digit finished? */
						ast_debug(2, "Received SF digit: %d\n", hits);
						digits_read++;
						if (hits > 10) {
							if (extrapulses) {
								/* dahdi-base.c translates 11 to * and 12 to # */
								if (hits == 11) {
									hits = '*';
								} else if (hits == 12) {
									hits = '#';
								} else if (hits == 13) {
									hits = 'D';
								} else if (hits == 14) {
									hits = 'C';
								} else if (hits == 15) {
									hits = 'B';
								} else if (hits == 16) {
									hits = 'A';
								} else {
									ast_debug(3, "Got %d SF pulses, is someone playing with the phone?\n", hits);
									hits = 'A';
								}
								*str++ = hits;
							} else {
								ast_debug(2, "Got more than 10 pulses, truncating to 10\n");
								hits = 0; /* 10 dial pulses = digit 0 */
								*str++ = hits + '0';
							}
						} else {
							if (hits == 10) {
								hits = 0; /* 10 dial pulses = digit 0 */
							}
							*str++ = hits + '0';
						}
						*str = 0;
						hits = 0;
						if (maxdigits > 0 && digits_read >= maxdigits) {
							pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "START");
							ast_frfree(frame);
							break;
						}
					}
				}
			}
			ast_frfree(frame);
		} else {
			pbx_builtin_setvar_helper(chan, "RECEIVESFSTATUS", "HANGUP");
			res = -1;
		}
	}
	if (dsp) {
		ast_dsp_free(dsp);
	}
	ast_debug(3, "channel '%s' - event loop stopped { timeout: %d, remaining_time: %d }\n", ast_channel_name(chan), timeout, remaining_time);
	return res;
}

static int read_sf_exec(struct ast_channel *chan, const char *data)
{
#define BUFFER_SIZE 256
	char tmp[BUFFER_SIZE] = "";
	double tosec;
	struct ast_flags flags = {0};
	char *argcopy = NULL;
	int res, features = 0, digits = 0, to = 0, freq = 2600;

	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(variable);
		AST_APP_ARG(digits);
		AST_APP_ARG(timeout);
		AST_APP_ARG(freq);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ReceiveSF requires an argument (variable)\n");
		return -1;
	}

	argcopy = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(arglist, argcopy);

	if (!ast_strlen_zero(arglist.options)) {
		ast_app_parse_options(read_app_options, &flags, NULL, arglist.options);
	}

	if (!ast_strlen_zero(arglist.timeout)) {
		tosec = atof(arglist.timeout);
		if (tosec <= 0) {
			to = 0;
		} else {
			to = tosec * 1000.0;
		}
	}

	if (!ast_strlen_zero(arglist.digits) && (ast_str_to_int(arglist.digits, &digits) || digits <= 0)) {
		ast_log(LOG_WARNING, "Invalid number of digits: %s\n", arglist.digits);
		return -1;
	}

	if (!ast_strlen_zero(arglist.freq) && (ast_str_to_int(arglist.freq, &freq) || freq <= 0)) {
		ast_log(LOG_WARNING, "Invalid freq: %s\n", arglist.freq);
		return -1;
	}

	if (ast_strlen_zero(arglist.variable)) {
		ast_log(LOG_WARNING, "Invalid! Usage: ReceiveSF(variable[,timeout][,option])\n");
		return -1;
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

	res = read_sf_digits(chan, tmp, BUFFER_SIZE, to, digits, freq, features, ast_test_flag(&flags, OPT_EXTRAPULSES));
	pbx_builtin_setvar_helper(chan, arglist.variable, tmp);
	if (!ast_strlen_zero(tmp)) {
		ast_verb(3, "SF digits received: '%s'\n", tmp);
	} else if (!res) { /* if channel hung up, don't print anything out */
		ast_verb(3, "No SF digits received.\n");
	}
	return res;
}

static int sendsf_exec(struct ast_channel *chan, const char *vdata)
{
	int res;
	char *data;
	int frequency = 2600;
	struct ast_channel *chan_found = NULL;
	struct ast_channel *chan_dest = chan;
	struct ast_channel *chan_autoservice = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(digits);
		AST_APP_ARG(frequency);
		AST_APP_ARG(channel);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "SendSF requires an argument\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.digits)) {
		ast_log(LOG_WARNING, "The digits argument is required (0-9,wf)\n");
		return 0;
	}
	if (!ast_strlen_zero(args.frequency) && (ast_str_to_int(args.frequency, &frequency) || frequency < 1)) {
		ast_log(LOG_WARNING, "Invalid duration: %s\n", args.frequency);
		return -1;
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
	res = ast_sf_stream(chan_dest, chan_autoservice, NULL, args.digits, frequency, 0);
	ast_channel_cleanup(chan_found);

	return chan_autoservice ? 0 : res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(readsf_name);
	res |= ast_unregister_application(sendsf_name);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(readsf_name, read_sf_exec);
	res |= ast_register_application_xml(sendsf_name, sendsf_exec);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "SF Sender and Receiver Applications");
