/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * WaitForSilence Application by David C. Troy <dave@popvox.com>
 * Version 1.11 2006-06-29
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
 * \brief Wait for Silence
 *   - Waits for up to 'x' milliseconds of silence, 'y' times \n
 *
 * \author David C. Troy <dave@popvox.com>
 *
 * \brief Wait For Noise
 * The same as Wait For Silence but listenes noise on the chennel that is above \n
 * the pre-configured silence threshold from dsp.conf
 *
 * \author Philipp Skadorov <skadorov@yahoo.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/dsp.h"
#include "asterisk/module.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="WaitForSilence" language="en_US">
		<synopsis>
			Waits for a specified amount of silence.
		</synopsis>
		<syntax>
			<parameter name="silencerequired">
				<para>If not specified, defaults to <literal>1000</literal> milliseconds.</para>
			</parameter>
			<parameter name="iterations">
				<para>If not specified, defaults to <literal>1</literal>.</para>
			</parameter>
			<parameter name="timeout">
				<para>Is specified only to avoid an infinite loop in cases where silence is never achieved.</para>
			</parameter>
		</syntax>
		<description>
			<para>Waits for up to <replaceable>silencerequired</replaceable> milliseconds of silence,
			<replaceable>iterations</replaceable> times. An optional <replaceable>timeout</replaceable>
			specified the number of seconds to return after, even if we do not receive the specified amount of silence.
			Use <replaceable>timeout</replaceable> with caution, as it may defeat the purpose of this application, which
			is to wait indefinitely until silence is detected on the line. This is particularly useful for reverse-911-type
			call broadcast applications where you need to wait for an answering machine to complete its spiel before
			playing a message.</para>
			<para>Typically you will want to include two or more calls to WaitForSilence when dealing with an answering
			machine; first waiting for the spiel to finish, then waiting for the beep, etc.</para>
			<example title="Wait for half a second of silence, twice">
			same => n,WaitForSilence(500,2)
			</example>
			<example title="Wait for one second of silence, once">
			same => n,WaitForSilence(1000)
			</example>
			<example title="Wait for 300 ms of silence, 3 times, and returns after 10 seconds, even if no silence detected">
			same => n,WaitForSilence(300,3,10)
			</example>
			<para>Sets the channel variable <variable>WAITSTATUS</variable> to one of these values:</para>
			<variablelist>
				<variable name="WAITSTATUS">
					<value name="SILENCE">
						if exited with silence detected.
					</value>
					<value name="TIMEOUT">
						if exited without silence detected after timeout.
					</value>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">WaitForNoise</ref>
		</see-also>
	</application>
	<application name="WaitForNoise" language="en_US">
		<synopsis>
			Waits for a specified amount of noise.
		</synopsis>
		<syntax>
			<parameter name="noiserequired">
				<para>If not specified, defaults to <literal>1000</literal> milliseconds.</para>
			</parameter>
			<parameter name="iterations">
				<para>If not specified, defaults to <literal>1</literal>.</para>
			</parameter>
			<parameter name="timeout">
				<para>Is specified only to avoid an infinite loop in cases where silence is never achieved.</para>
			</parameter>
		</syntax>
		<description>
			<para>Waits for up to <replaceable>noiserequired</replaceable> milliseconds of noise,
			<replaceable>iterations</replaceable> times. An optional <replaceable>timeout</replaceable>
			specified the number of seconds to return after, even if we do not receive the specified amount of noise.
			Use <replaceable>timeout</replaceable> with caution, as it may defeat the purpose of this application, which
			is to wait indefinitely until noise is detected on the line.</para>
		</description>
		<see-also>
			<ref type="application">WaitForSilence</ref>
		</see-also>
	</application>
 ***/

static char *app_silence = "WaitForSilence";
static char *app_noise = "WaitForNoise";

struct wait_type {
	const char *name;
	const char *status;
	int stop_on_frame_timeout;
	int (*func)(struct ast_dsp *, struct ast_frame *, int *);
};

static const struct wait_type wait_for_silence = {
	.name = "silence",
	.status = "SILENCE",
	.stop_on_frame_timeout = 1,
	.func = ast_dsp_silence,
};

static const struct wait_type wait_for_noise = {
	.name = "noise",
	.status = "NOISE",
	.stop_on_frame_timeout = 0,
	.func = ast_dsp_noise,
};

static int do_waiting(struct ast_channel *chan, int timereqd, time_t waitstart, int timeout, const struct wait_type *wait_for)
{
	RAII_VAR(struct ast_format *, rfmt, NULL, ao2_cleanup);
	int res;
	struct ast_dsp *sildet;

	rfmt = ao2_bump(ast_channel_readformat(chan));
	if ((res = ast_set_read_format(chan, ast_format_slin)) < 0) {
		ast_log(LOG_WARNING, "Unable to set channel to linear mode, giving up\n");
		return -1;
	}

	/* Create the silence detector */
	if (!(sildet = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to create silence detector\n");
		return -1;
	}
	ast_dsp_set_threshold(sildet, ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE));

	for (;;) {
		int dsptime = 0;

		res = ast_waitfor(chan, timereqd);

		/* Must have gotten a hangup; let's exit */
		if (res < 0) {
			pbx_builtin_setvar_helper(chan, "WAITSTATUS", "HANGUP");
			break;
		}

		/* We waited and got no frame; sounds like digital silence or a muted digital channel */
		if (res == 0) {
			if (wait_for->stop_on_frame_timeout) {
				dsptime = timereqd;
			}
		} else {
			/* Looks like we did get a frame, so let's check it out */
			struct ast_frame *f = ast_read(chan);
			if (!f) {
				pbx_builtin_setvar_helper(chan, "WAITSTATUS", "HANGUP");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				wait_for->func(sildet, f, &dsptime);
			}
			ast_frfree(f);
		}

		ast_debug(1, "Got %dms of %s < %dms required\n", dsptime, wait_for->name, timereqd);

		if (dsptime >= timereqd) {
			ast_verb(3, "Exiting with %dms of %s >= %dms required\n", dsptime, wait_for->name, timereqd);
			pbx_builtin_setvar_helper(chan, "WAITSTATUS", wait_for->status);
			ast_debug(1, "WAITSTATUS was set to %s\n", wait_for->status);
			res = 1;
			break;
		}

		if (timeout && difftime(time(NULL), waitstart) >= timeout) {
			pbx_builtin_setvar_helper(chan, "WAITSTATUS", "TIMEOUT");
			ast_debug(1, "WAITSTATUS was set to TIMEOUT\n");
			res = 0;
			break;
		}
	}

	if (rfmt && ast_set_read_format(chan, rfmt)) {
		ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_format_get_name(rfmt), ast_channel_name(chan));
	}

	ast_dsp_free(sildet);
	return res;
}

static int waitfor_exec(struct ast_channel *chan, const char *data, const struct wait_type *wait_for)
{
	int res = 1;
	int timereqd = 1000;
	int timeout = 0;
	int iterations = 1, i;
	time_t waitstart;
	char *parse;
	struct ast_silence_generator *silgen = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timereqd);
		AST_APP_ARG(iterations);
		AST_APP_ARG(timeout);
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.timereqd)) {
		if (sscanf(args.timereqd, "%30d", &timereqd) != 1 || timereqd < 0) {
			ast_log(LOG_ERROR, "Argument '%srequired' must be an integer greater than or equal to zero.\n",
					wait_for->name);
			return -1;
		}
	}

	if (!ast_strlen_zero(args.iterations)) {
		if (sscanf(args.iterations, "%30d", &iterations) != 1 || iterations < 1) {
			ast_log(LOG_ERROR, "Argument 'iterations' must be an integer greater than 0.\n");
			return -1;
		}
	}

	if (!ast_strlen_zero(args.timeout)) {
		if (sscanf(args.timeout, "%30d", &timeout) != 1 || timeout < 0) {
			ast_log(LOG_ERROR, "Argument 'timeout' must be an integer greater than or equal to zero.\n");
			return -1;
		}
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan); /* Answer the channel */
	}

	ast_verb(3, "Waiting %d time(s) for %dms of %s with %ds timeout\n",
			 iterations, timereqd, wait_for->name, timeout);

	if (ast_opt_transmit_silence) {
		silgen = ast_channel_start_silence_generator(chan);
	}

	time(&waitstart);
	for (i = 0; i < iterations && res == 1; i++) {
		res = do_waiting(chan, timereqd, waitstart, timeout, wait_for);
	}

	if (silgen) {
		ast_channel_stop_silence_generator(chan, silgen);
	}

	return res > 0 ? 0 : res;
}

static int waitforsilence_exec(struct ast_channel *chan, const char *data)
{
	return waitfor_exec(chan, data, &wait_for_silence);
}

static int waitfornoise_exec(struct ast_channel *chan, const char *data)
{
	return waitfor_exec(chan, data, &wait_for_noise);
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app_silence);
	res |= ast_unregister_application(app_noise);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app_silence, waitforsilence_exec);
	res |= ast_register_application_xml(app_noise, waitfornoise_exec);
	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Wait For Silence/Noise");
