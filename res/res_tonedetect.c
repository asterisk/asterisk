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
 * \brief Tone detection module
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup resources
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/pbx.h"
#include "asterisk/audiohook.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/conversions.h"

/*** DOCUMENTATION
	<application name="WaitForTone" language="en_US">
		<synopsis>
			Wait for tone
		</synopsis>
		<syntax>
			<parameter name="freq" required="true">
				<para>Frequency of the tone to wait for.</para>
			</parameter>
			<parameter name="duration_ms" required="false">
				<para>Minimum duration of tone, in ms. Default is 500ms.
				Using a minimum duration under 50ms is unlikely to produce
				accurate results.</para>
			</parameter>
			<parameter name="timeout" required="false">
				<para>Maximum amount of time, in seconds, to wait for specified tone.
				Default is forever.</para>
			</parameter>
			<parameter name="times" required="false">
				<para>Number of times the tone should be detected (subject to the
				provided timeout) before returning. Default is 1.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="d">
						<para>Custom decibel threshold to use. Default is 16.</para>
					</option>
					<option name="s">
						<para>Squelch tone.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Waits for a single-frequency tone to be detected before dialplan execution continues.</para>
			<variablelist>
			<variable name="WAITFORTONESTATUS">
				<para>This indicates the result of the wait.</para>
				<value name="SUCCESS"/>
				<value name="ERROR"/>
				<value name="TIMEOUT"/>
				<value name="HANGUP"/>
			</variable>
		</variablelist>
		</description>
		<see-also>
			<ref type="application">PlayTones</ref>
		</see-also>
	</application>
	<function name="TONE_DETECT" language="en_US">
		<synopsis>
			Asynchronously detects a tone
		</synopsis>
		<syntax>
			<parameter name="freq" required="true">
				<para>Frequency of the tone to detect.</para>
			</parameter>
			<parameter name="duration_ms" required="false">
				<para>Minimum duration of tone, in ms. Default is 500ms.
				Using a minimum duration under 50ms is unlikely to produce
				accurate results.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="d">
						<para>Custom decibel threshold to use. Default is 16.</para>
					</option>
					<option name="g">
						<para>Go to the specified context,exten,priority if tone is received on this channel.
						Detection will not end automatically.</para>
					</option>
					<option name="h">
						<para>Go to the specified context,exten,priority if tone is transmitted on this channel.
						Detection will not end automatically.</para>
					</option>
					<option name="n">
						<para>Number of times the tone should be detected (subject to the
						provided timeout) before going to the destination provided in the <literal>g</literal>
						or <literal>h</literal> option. Default is 1.</para>
					</option>
					<option name="r">
						<para>Apply to received frames only. Default is both directions.</para>
					</option>
					<option name="s">
						<para>Squelch tone.</para>
					</option>
					<option name="t">
						<para>Apply to transmitted frames only. Default is both directions.</para>
					</option>
					<option name="x">
						<para>Destroy the detector (stop detection).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>The TONE_DETECT function detects a single-frequency tone and keeps
			track of how many times the tone has been detected.</para>
			<para>When reading this function (instead of writing), supply <literal>tx</literal>
			to get the number of times a tone has been detected in the TX direction and
			<literal>rx</literal> to get the number of times a tone has been detected in the
			RX direction.</para>
			<example title="intercept2600">
			same => n,Set(TONE_DETECT(2600,1000,g(got-2600,s,1))=)
			same => n,Wait(15)
			same => n,NoOp(${TONE_DETECT(rx)})
			</example>
		</description>
	</function>
 ***/

struct detect_information {
	struct ast_dsp *dsp;
	struct ast_audiohook audiohook;
	int freq1;
	int freq2;
	int duration;
	int db;
	char *gototx;
	char *gotorx;
	unsigned short int squelch;
	unsigned short int tx;
	unsigned short int rx;
	int txcount;
	int rxcount;
	int hitsrequired;
};

enum td_opts {
	OPT_TX = (1 << 1),
	OPT_RX = (1 << 2),
	OPT_END_FILTER = (1 << 3),
	OPT_GOTO_RX = (1 << 4),
	OPT_GOTO_TX = (1 << 5),
	OPT_DECIBEL = (1 << 6),
	OPT_SQUELCH = (1 << 7),
	OPT_HITS_REQ = (1 << 8),
};

enum {
	OPT_ARG_DECIBEL,
	OPT_ARG_GOTO_RX,
	OPT_ARG_GOTO_TX,
	OPT_ARG_HITS_REQ,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(td_opts, {
	AST_APP_OPTION_ARG('d', OPT_DECIBEL, OPT_ARG_DECIBEL),
	AST_APP_OPTION_ARG('g', OPT_GOTO_RX, OPT_ARG_GOTO_RX),
	AST_APP_OPTION_ARG('h', OPT_GOTO_TX, OPT_ARG_GOTO_TX),
	AST_APP_OPTION_ARG('n', OPT_HITS_REQ, OPT_ARG_HITS_REQ),
	AST_APP_OPTION('s', OPT_SQUELCH),
	AST_APP_OPTION('t', OPT_TX),
	AST_APP_OPTION('r', OPT_RX),
	AST_APP_OPTION('x', OPT_END_FILTER),
});

static void destroy_callback(void *data)
{
	struct detect_information *di = data;
	ast_dsp_free(di->dsp);
	if (di->gotorx) {
		ast_free(di->gotorx);
	}
	if (di->gototx) {
		ast_free(di->gototx);
	}
	ast_audiohook_lock(&di->audiohook);
	ast_audiohook_detach(&di->audiohook);
	ast_audiohook_unlock(&di->audiohook);
	ast_audiohook_destroy(&di->audiohook);
	ast_free(di);
	return;
}

static const struct ast_datastore_info detect_datastore = {
	.type = "detect",
	.destroy = destroy_callback
};

static int detect_callback(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct detect_information *di = NULL;

	/* If the audiohook is stopping it means the channel is shutting down.... but we let the datastore destroy take care of it */
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return 0;
	}

	/* Grab datastore which contains our gain information */
	if (!(datastore = ast_channel_datastore_find(chan, &detect_datastore, NULL))) {
		return 0;
	}

	di = datastore->data;

	if (!frame || frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	if (!(direction == AST_AUDIOHOOK_DIRECTION_READ ? &di->rx : &di->tx)) {
		return 0;
	}

	/* ast_dsp_process may free the frame and return a new one */
	frame = ast_frdup(frame);
	frame = ast_dsp_process(chan, di->dsp, frame);
	if (frame->frametype == AST_FRAME_DTMF) {
		char result = frame->subclass.integer;
		if (result == 'q') {
			int now;
			if (direction == AST_AUDIOHOOK_DIRECTION_READ) {
				di->rxcount = di->rxcount + 1;
				now = di->rxcount;
			} else {
				di->txcount = di->txcount + 1;
				now = di->txcount;
			}
			ast_debug(1, "TONE_DETECT just got a hit (#%d in this direction, waiting for %d total)\n", now, di->hitsrequired);
			if (now >= di->hitsrequired) {
				if (direction == AST_AUDIOHOOK_DIRECTION_READ && di->gotorx) {
					ast_async_parseable_goto(chan, di->gotorx);
				} else if (di->gototx) {
					ast_async_parseable_goto(chan, di->gototx);
				}
			}
		}
	}
	/* this could be the duplicated frame or a new one, doesn't matter */
	ast_frfree(frame);
	return 0;
}

static int remove_detect(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct detect_information *data;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &detect_datastore, NULL);
	if (!datastore) {
		ast_log(AST_LOG_WARNING, "Cannot remove TONE_DETECT from %s: TONE_DETECT not currently enabled\n",
		        ast_channel_name(chan));
		return -1;
	}
	data = datastore->data;

	if (ast_audiohook_remove(chan, &data->audiohook)) {
		ast_log(AST_LOG_WARNING, "Failed to remove TONE_DETECT audiohook from channel %s\n", ast_channel_name(chan));
		return -1;
	}

	if (ast_channel_datastore_remove(chan, datastore)) {
		ast_log(AST_LOG_WARNING, "Failed to remove TONE_DETECT datastore from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}
	ast_datastore_free(datastore);

	return 0;
}

static int freq_parser(char *freqs, int *freq1, int *freq2) {
	char *f1, *f2, *f3;
	if (ast_strlen_zero(freqs)) {
		ast_log(LOG_ERROR, "No frequency specified\n");
		return -1;
	}
	f3 = ast_strdupa(freqs);
	f1 = strsep(&f3, "+");
	f2 = strsep(&f3, "+");
	if (!ast_strlen_zero(f3)) {
		ast_log(LOG_WARNING, "Only up to 2 frequencies may be specified: %s\n", freqs);
		return -1;
	}
	if (ast_str_to_int(f1, freq1)) {
		ast_log(LOG_WARNING, "Frequency must be an integer: %s\n", f1);
		return -1;
	}
	if (*freq1 < 1) {
		ast_log(LOG_WARNING, "Sorry, positive frequencies only: %d\n", *freq1);
		return -1;
	}
	if (!ast_strlen_zero(f2)) {
		ast_log(LOG_WARNING, "Sorry, currently only 1 frequency is supported\n");
		return -1;
		/* not supported just yet, but possibly will be in the future */
		if (ast_str_to_int(f2, freq2)) {
			ast_log(LOG_WARNING, "Frequency must be an integer: %s\n", f2);
			return -1;
		}
		if (*freq2 < 1) {
			ast_log(LOG_WARNING, "Sorry, positive frequencies only: %d\n", *freq2);
			return -1;
		}
	}
	return 0;
}

static char* goto_parser(struct ast_channel *chan, char *loc) {
	char *exten, *pri, *context, *parse;
	char *dest;
	int size;
	parse = ast_strdupa(loc);
	context = strsep(&parse, ",");
	exten = strsep(&parse, ",");
	pri = strsep(&parse, ",");
	if (!exten) {
		pri = context;
		exten = NULL;
		context = NULL;
	} else if (!pri) {
		pri = exten;
		exten = context;
		context = NULL;
	}
	ast_channel_lock(chan);
	if (ast_strlen_zero(exten)) {
		exten = ast_strdupa(ast_channel_exten(chan));
	}
	if (ast_strlen_zero(context)) {
		context = ast_strdupa(ast_channel_context(chan));
	}
	ast_channel_unlock(chan);

	/* size + 3: for 1 null terminator + 2 commas */
	size = strlen(context) + strlen(exten) + strlen(pri) + 3;
	dest = ast_malloc(size + 1);
	if (!dest) {
		ast_log(LOG_ERROR, "Failed to parse goto: %s,%s,%s\n", context, exten, pri);
		return NULL;
	}
	snprintf(dest, size, "%s,%s,%s", context, exten, pri);
	return dest;
}

static int detect_read(struct ast_channel *chan, const char *cmd, char *data, char *buffer, size_t buflen)
{
	struct ast_datastore *datastore = NULL;
	struct detect_information *di = NULL;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &detect_datastore, NULL))) {
		ast_channel_unlock(chan);
		return -1; /* function not initiated yet, so nothing to read */
	} else {
		ast_channel_unlock(chan);
		di = datastore->data;
	}

	if (strchr(data, 't')) {
		snprintf(buffer, buflen, "%d", di->txcount);
	} else if (strchr(data, 'r')) {
		snprintf(buffer, buflen, "%d", di->rxcount);
	} else {
		ast_log(LOG_WARNING, "Invalid direction: %s\n", data);
	}

	return 0;
}

static int detect_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	char *parse;
	struct ast_datastore *datastore = NULL;
	struct detect_information *di = NULL;
	struct ast_flags flags = { 0 };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct ast_dsp *dsp;
	int freq1 = 0, freq2 = 0, duration = 500, db = 16, squelch = 0, hitsrequired = 1;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(freqs);
		AST_APP_ARG(duration);
		AST_APP_ARG(options);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_test_flag(&flags, OPT_END_FILTER)) {
		return remove_detect(chan);
	}
	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(td_opts, &flags, opt_args, args.options);
	}
	if (freq_parser(args.freqs, &freq1, &freq2)) {
		return -1;
	}
	if (!ast_strlen_zero(args.duration) && (ast_str_to_int(args.duration, &duration) || duration < 1)) {
		ast_log(LOG_WARNING, "Invalid duration: %s\n", args.duration);
		return -1;
	}
	if (ast_test_flag(&flags, OPT_HITS_REQ) && !ast_strlen_zero(opt_args[OPT_ARG_HITS_REQ])) {
		if ((ast_str_to_int(opt_args[OPT_ARG_HITS_REQ], &hitsrequired) || hitsrequired < 1)) {
			ast_log(LOG_WARNING, "Invalid number hits required: %s\n", opt_args[OPT_ARG_HITS_REQ]);
			return -1;
		}
	}
	if (ast_test_flag(&flags, OPT_DECIBEL) && !ast_strlen_zero(opt_args[OPT_ARG_DECIBEL])) {
		if ((ast_str_to_int(opt_args[OPT_ARG_DECIBEL], &db) || db < 1)) {
			ast_log(LOG_WARNING, "Invalid decibel level: %s\n", opt_args[OPT_ARG_DECIBEL]);
			return -1;
		}
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &detect_datastore, NULL))) {
		if (!(datastore = ast_datastore_alloc(&detect_datastore, NULL))) {
			ast_channel_unlock(chan);
			return 0;
		}
		if (!(di = ast_calloc(1, sizeof(*di)))) {
			ast_datastore_free(datastore);
			ast_channel_unlock(chan);
			return 0;
		}
		ast_audiohook_init(&di->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "Tone Detector", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		di->audiohook.manipulate_callback = detect_callback;
		if (!(dsp = ast_dsp_new())) {
			ast_datastore_free(datastore);
			ast_channel_unlock(chan);
			ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
			return -1;
		}
		ast_dsp_set_features(dsp, DSP_FEATURE_FREQ_DETECT);
		ast_dsp_set_freqmode(dsp, freq1, duration, db, squelch);
		di->dsp = dsp;
		di->txcount = 0;
		di->rxcount = 0;
		ast_debug(1, "Keeping our ears open for %s Hz, %d db\n", args.freqs, db);
		datastore->data = di;
		ast_channel_datastore_add(chan, datastore);
		ast_audiohook_attach(chan, &di->audiohook);
	} else {
		di = datastore->data;
		dsp = di->dsp;
		ast_dsp_set_freqmode(dsp, freq1, duration, db, squelch);
	}
	di->duration = duration;
	di->gotorx = NULL;
	di->gototx = NULL;
	/* resolve gotos now, in case a full context,exten,pri wasn't specified */
	if (ast_test_flag(&flags, OPT_GOTO_RX) && !ast_strlen_zero(opt_args[OPT_ARG_GOTO_RX])) {
		di->gotorx = goto_parser(chan, opt_args[OPT_ARG_GOTO_RX]);
	}
	if (ast_test_flag(&flags, OPT_GOTO_TX) && !ast_strlen_zero(opt_args[OPT_ARG_GOTO_TX])) {
		di->gototx = goto_parser(chan, opt_args[OPT_ARG_GOTO_TX]);
	}
	di->db = db;
	di->hitsrequired = hitsrequired;
	di->squelch = ast_test_flag(&flags, OPT_SQUELCH);
	di->tx = 1;
	di->rx = 1;
	if (ast_strlen_zero(args.options) || ast_test_flag(&flags, OPT_TX)) {
		di->tx = 1;
		di->rx = 0;
	}
	if (ast_strlen_zero(args.options) || ast_test_flag(&flags, OPT_RX)) {
		di->rx = 1;
		di->tx = 0;
	}
	ast_channel_unlock(chan);

	return 0;
}

enum {
	OPT_APP_DECIBEL =  (1 << 0),
	OPT_APP_SQUELCH =  (1 << 1),
};

enum {
	OPT_APP_ARG_DECIBEL,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_APP_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(wait_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('d', OPT_APP_DECIBEL, OPT_APP_ARG_DECIBEL),
	AST_APP_OPTION('s', OPT_APP_SQUELCH),
END_OPTIONS);

static int wait_exec(struct ast_channel *chan, const char *data)
{
	char *appdata;
	struct ast_flags flags = {0};
	char *opt_args[OPT_APP_ARG_ARRAY_SIZE];
	double timeoutf = 0;
	int freq1 = 0, freq2 = 0, timeout = 0, duration = 500, times = 1, db = 16, squelch = 0;
	struct ast_frame *frame = NULL;
	struct ast_dsp *dsp;
	struct timeval start;
	int remaining_time = 0;
	int hits = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(freqs);
		AST_APP_ARG(duration);
		AST_APP_ARG(timeout);
		AST_APP_ARG(times);
		AST_APP_ARG(options);
	);

	appdata = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, appdata);

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(wait_exec_options, &flags, opt_args, args.options);
	}
	if (freq_parser(args.freqs, &freq1, &freq2)) {
		pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "ERROR");
		return -1;
	}
	if (!ast_strlen_zero(args.timeout) && (sscanf(args.timeout, "%30lf", &timeoutf) != 1 || timeout < 0)) {
		ast_log(LOG_WARNING, "Invalid timeout: %s\n", args.timeout);
		pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "ERROR");
		return -1;
	}
	timeout = 1000 * timeoutf;
	if (!ast_strlen_zero(args.duration) && (ast_str_to_int(args.duration, &duration) || duration < 1)) {
		ast_log(LOG_WARNING, "Invalid duration: %s\n", args.duration);
		pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "ERROR");
		return -1;
	}
	if (!ast_strlen_zero(args.times) && (ast_str_to_int(args.times, &times) || times < 1)) {
		ast_log(LOG_WARNING, "Invalid number of times: %s\n", args.times);
		pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "ERROR");
		return -1;
	}
	if (ast_test_flag(&flags, OPT_APP_DECIBEL) && !ast_strlen_zero(opt_args[OPT_APP_ARG_DECIBEL])) {
		if ((ast_str_to_int(opt_args[OPT_APP_ARG_DECIBEL], &db) || db < 1)) {
			ast_log(LOG_WARNING, "Invalid decibel level: %s\n", opt_args[OPT_APP_ARG_DECIBEL]);
			pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "ERROR");
			return -1;
		}
	}
	squelch = ast_test_flag(&flags, OPT_APP_SQUELCH);
	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "ERROR");
		return -1;
	}
	ast_dsp_set_features(dsp, DSP_FEATURE_FREQ_DETECT);
	ast_dsp_set_freqmode(dsp,  freq1, duration, db, squelch);
	ast_debug(1, "Waiting for %s Hz, %d time(s), timeout %d ms, %d db\n", args.freqs, times, timeout, db);
	start = ast_tvnow();
	do {
		if (timeout > 0) {
			remaining_time = ast_remaining_ms(start, timeout);
			if (remaining_time <= 0) {
				pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "TIMEOUT");
				break;
			}
		}
		if (ast_waitfor(chan, 1000) > 0) {
			if (!(frame = ast_read(chan))) {
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", ast_channel_name(chan));
				pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "HANGUP");
				break;
			} else if (frame->frametype == AST_FRAME_VOICE) {
				frame = ast_dsp_process(chan, dsp, frame);
				if (frame->frametype == AST_FRAME_DTMF) {
					char result = frame->subclass.integer;
					if (result == 'q') {
						hits++;
						ast_debug(1, "We just detected %s Hz (hit #%d)\n", args.freqs, hits);
						if (hits >= times) {
							ast_frfree(frame);
							pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "SUCCESS");
							break;
						}
					}
				}
			}
			ast_frfree(frame);
		} else {
			pbx_builtin_setvar_helper(chan, "WAITFORTONESTATUS", "HANGUP");
		}
	} while (timeout == 0 || remaining_time > 0);
	ast_dsp_free(dsp);

	return 0;
}

static char *waitapp = "WaitForTone";

static struct ast_custom_function detect_function = {
	.name = "TONE_DETECT",
	.read = detect_read,
	.write = detect_write,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(waitapp);
	res |= ast_custom_function_unregister(&detect_function);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(waitapp, wait_exec);
	res |= ast_custom_function_register(&detect_function);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Tone detection module");
