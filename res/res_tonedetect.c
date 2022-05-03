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
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
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
	<application name="ToneScan" language="en_US">
		<since>
			<version>16.23.0</version>
			<version>18.9.0</version>
			<version>19.1.0</version>
		</since>
		<synopsis>
			Wait for period of time while scanning for call progress tones
		</synopsis>
		<syntax>
			<parameter name="zone" required="false">
				<para>Call progress zone. Default is the system default.</para>
			</parameter>
			<parameter name="timeout" required="false">
				<para>Maximum amount of time, in seconds, to wait for call progress
				or signal tones. Default is forever.</para>
			</parameter>
			<parameter name="threshold" required="false">
				<para>DSP threshold required for a match. A higher number will
				require a longer match and may reduce false positives, at the
				expense of false negatives. Default is 1.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="f">
						<para>Enable fax machine detection. By default, this is disabled.</para>
					</option>
					<option name="v">
						<para>Enable voice detection. By default, this is disabled.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Waits for a a distinguishable call progress tone and then exits.
			Unlike a conventional scanner, this is not currently capable of
			scanning for modem carriers.</para>
			<variablelist>
			<variable name="TONESCANSTATUS">
				This indicates the result of the scan.
				<value name="RINGING">
					Audible ringback tone
				</value>
				<value name="BUSY">
					Busy tone
				</value>
				<value name="SIT">
					Special Information Tones
				</value>
				<value name="VOICE">
					Human voice detected
				</value>
				<value name="DTMF">
					DTMF digit
				</value>
				<value name="FAX">
					Fax (answering)
				</value>
				<value name="MODEM">
					Modem (answering)
				</value>
				<value name="DIALTONE">
					Dial tone
				</value>
				<value name="NUT">
					UK Number Unobtainable tone
				</value>
				<value name="TIMEOUT">
					Timeout reached before any positive detection
				</value>
				<value name="HANGUP">
					Caller hung up before any positive detection
				</value>
			</variable>
		</variablelist>
		</description>
		<see-also>
			<ref type="application">WaitForTone</ref>
		</see-also>
	</application>
	<function name="TONE_DETECT" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Asynchronously detects a tone
		</synopsis>
		<syntax>
			<parameter name="freq" required="true">
				<para>Frequency of the tone to detect. To disable frequency
				detection completely (e.g. for signal detection only),
				specify 0 for the frequency.</para>
			</parameter>
			<parameter name="duration_ms" required="false">
				<para>Minimum duration of tone, in ms. Default is 500ms.
				Using a minimum duration under 50ms is unlikely to produce
				accurate results.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Match immediately on Special Information Tones, instead of or in addition
						to a particular frequency.</para>
					</option>
					<option name="b">
						<para>Match immediately on a busy signal, instead of or in addition to
						a particular frequency.</para>
					</option>
					<option name="c">
						<para>Match immediately on a dial tone, instead of or in addition to
						a particular frequency.</para>
					</option>
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
			same => n,Set(TONE_DETECT(2600,1000,g(got-2600,s,1))=) ; detect 2600 Hz
			same => n,Wait(15)
			same => n,NoOp(${TONE_DETECT(rx)})
			</example>
			<example title="dropondialtone">
			same => n,Set(TONE_DETECT(0,,bg(my-hangup,s,1))=) ; disconnect a call if we hear a busy signal
			same => n,Goto(somewhere-else)
			same => n(myhangup),Hangup()
			</example>
			<example title="removedetector">
			same => n,Set(TONE_DETECT(0,,x)=) ; remove the detector from the channel
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
	int signalfeatures;
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
	OPT_SIT = (1 << 9),
	OPT_BUSY = (1 << 10),
	OPT_DIALTONE = (1 << 11),
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
	AST_APP_OPTION('a', OPT_SIT),
	AST_APP_OPTION('b', OPT_BUSY),
	AST_APP_OPTION('c', OPT_DIALTONE),
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
	int match = 0;

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

	if (!(direction == AST_AUDIOHOOK_DIRECTION_READ ? di->rx : di->tx)) {
		return 0;
	}

	/* ast_dsp_process may free the frame and return a new one */
	frame = ast_frdup(frame);
	frame = ast_dsp_process(chan, di->dsp, frame);
	if (frame->frametype == AST_FRAME_DTMF) {
		char result = frame->subclass.integer;
		if (result == 'q') {
			int now;
			match = 1;
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
	if (di->signalfeatures && !match) { /* skip unless there are call progress/signal options */
		int tstate, tcount;
		tcount = ast_dsp_get_tcount(di->dsp);
		tstate = ast_dsp_get_tstate(di->dsp);
		if (tstate > 0) {
			ast_debug(3, "tcount: %d, tstate: %d\n", tcount, tstate);
			switch (tstate) {
			case DSP_TONE_STATE_DIALTONE:
				if (di->signalfeatures & DSP_FEATURE_WAITDIALTONE) {
					match = 1;
				}
				break;
			case DSP_TONE_STATE_BUSY:
				if (di->signalfeatures & DSP_PROGRESS_BUSY) {
					match = 1;
				}
				break;
			case DSP_TONE_STATE_SPECIAL3:
				if (di->signalfeatures & DSP_PROGRESS_CONGESTION) {
					match = 1;
				}
				break;
			default: /* ignore */
				break;
			}
			if (match) {
				if (direction == AST_AUDIOHOOK_DIRECTION_READ && di->gotorx) {
					ast_async_parseable_goto(chan, di->gotorx);
				} else if (di->gototx) {
					ast_async_parseable_goto(chan, di->gototx);
				} else {
					ast_debug(3, "Detected call progress signal, but don't know where to go\n");
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
	if (*freq1 < 0) {
		ast_log(LOG_WARNING, "Sorry, no negative frequencies: %d\n", *freq1);
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

static int parse_signal_features(struct ast_flags *flags)
{
	int features = 0;

	if (ast_test_flag(flags, OPT_SIT)) {
		features |= DSP_PROGRESS_CONGESTION;
	}
	if (ast_test_flag(flags, OPT_BUSY)) {
		features |= DSP_PROGRESS_BUSY;
	}
	if (ast_test_flag(flags, OPT_DIALTONE)) {
		features |= DSP_FEATURE_WAITDIALTONE;
	}

	return features;
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
	int signalfeatures = 0;

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

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(td_opts, &flags, opt_args, args.options);
	}
	if (ast_test_flag(&flags, OPT_END_FILTER)) {
		return remove_detect(chan);
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
	signalfeatures = parse_signal_features(&flags);

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
		di->signalfeatures = signalfeatures; /* we're not including freq detect */
		if (freq1 > 0) {
			signalfeatures |= DSP_FEATURE_FREQ_DETECT;
			ast_dsp_set_freqmode(dsp, freq1, duration, db, squelch);
		}
		ast_dsp_set_features(dsp, signalfeatures);
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
		di->signalfeatures = signalfeatures; /* we're not including freq detect */
		if (freq1 > 0) {
			signalfeatures |= DSP_FEATURE_FREQ_DETECT;
			ast_dsp_set_freqmode(dsp, freq1, duration, db, squelch);
		}
		ast_dsp_set_features(dsp, signalfeatures);
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
	if (!ast_strlen_zero(args.timeout) && (sscanf(args.timeout, "%30lf", &timeoutf) != 1 || timeoutf < 0)) {
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
static char *scanapp = "ToneScan";

static int scan_exec(struct ast_channel *chan, const char *data)
{
	char *appdata;
	double timeoutf = 0;
	int timeout = 0;
	struct ast_frame *frame = NULL, *frame2 = NULL;
	struct ast_dsp *dsp = NULL, *dsp2 = NULL;
	struct timeval start;
	int remaining_time = 0;
	int features, match = 0, fax = 0, voice = 0, threshold = 1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(zone);
		AST_APP_ARG(timeout);
		AST_APP_ARG(threshold);
		AST_APP_ARG(options);
	);

	appdata = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, appdata);

	if (!ast_strlen_zero(args.timeout) && (sscanf(args.timeout, "%30lf", &timeoutf) != 1 || timeout < 0)) {
		ast_log(LOG_WARNING, "Invalid timeout: %s\n", args.timeout);
		pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "ERROR");
		return -1;
	}
	if (!ast_strlen_zero(args.threshold) && (ast_str_to_int(args.threshold, &threshold) || threshold < 1)) {
		ast_log(LOG_WARNING, "Invalid threshold: %s\n", args.threshold);
		pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "ERROR");
		return -1;
	}
	timeout = 1000 * timeoutf;

	if (!ast_strlen_zero(args.options) && strchr(args.options, 'f')) {
		fax = 1;
	}
	if (!ast_strlen_zero(args.options) && strchr(args.options, 'v')) {
		voice = 1;
	}

	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "ERROR");
		return -1;
	}

	if (!ast_strlen_zero(args.zone)) {
		if (ast_dsp_set_call_progress_zone(dsp, args.zone)) {
			ast_log(LOG_WARNING, "Invalid call progress zone: %s\n", args.zone);
			pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "ERROR");
			ast_dsp_free(dsp);
			return -1;
		}
	}

	if (fax) {
		if (!(dsp2 = ast_dsp_new())) {
			ast_dsp_free(dsp);
			ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
			pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "ERROR");
			return -1;
		}
	}

	features = DSP_PROGRESS_RINGING; /* audible ringback tone */
	features |= DSP_PROGRESS_BUSY; /* busy signal */
	features |= DSP_PROGRESS_CONGESTION; /* SIT tones (not reorder!) */
	features |= DSP_PROGRESS_TALK; /* voice. */
	features |= DSP_FEATURE_WAITDIALTONE; /* dial tone */
	features |= DSP_FEATURE_FREQ_DETECT; /* modem answer */
	if (voice) {
		features |= DSP_TONE_STATE_TALKING; /* voice */
	}
	ast_dsp_set_features(dsp, features);
	/* all modems begin negotiating with Bell 103. An answering modem just sends mark tone, or 2225 Hz */
	ast_dsp_set_freqmode(dsp, 2225, 400, 16, 0); /* this needs to be pretty short, or the progress tones code will thing this is voice */

	if (fax) { /* fax detect uses same tone detect internals as modem and causes things to not work as intended, so use a separate DSP if needed. */
		ast_dsp_set_features(dsp2, DSP_FEATURE_FAX_DETECT); /* fax tone */
		ast_dsp_set_faxmode(dsp2, DSP_FAXMODE_DETECT_CED); /* we only care about the answering side (CED), not originating (CNG) */
	}

	ast_debug(1, "Starting tone scan, timeout: %d ms, threshold: %d\n", timeout, threshold);
	start = ast_tvnow();
	do {
		if (timeout > 0) {
			remaining_time = ast_remaining_ms(start, timeout);
			if (remaining_time <= 0) {
				pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "TIMEOUT");
				break;
			}
		}
		if (ast_waitfor(chan, 1000) > 0) {
			if (!(frame = ast_read(chan))) {
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", ast_channel_name(chan));
				pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "HANGUP");
				break;
			} else if (frame->frametype == AST_FRAME_VOICE) {
				if (fax) {
					frame2 = ast_frdup(frame);
				}
				frame = ast_dsp_process(chan, dsp, frame);
				if (frame->frametype == AST_FRAME_DTMF) {
					char result = frame->subclass.integer;
					match = 1;
					if (result == 'q') {
						pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "MODEM");
					} else {
						pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "DTMF");
					}
				} else if (fax) {
					char result;
					frame2 = ast_dsp_process(chan, dsp2, frame2);
					result = frame->subclass.integer;
					if (result == AST_FRAME_DTMF) {
						if (result == 'e') {
							pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "FAX");
							match = 1;
						} else {
							ast_debug(1, "Ignoring inactionable event\n"); /* shouldn't happen */
						}
					}
					ast_frfree(frame2);
				}
				if (!match) {
					int tstate, tcount;
					tcount = ast_dsp_get_tcount(dsp);
					tstate = ast_dsp_get_tstate(dsp);
					if (tstate > 0) {
						ast_debug(3, "tcount: %d, tstate: %d\n", tcount, tstate);
						if (tcount >= threshold) {
							match = 1;
							switch (tstate) {
							case DSP_TONE_STATE_RINGING:
								pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "RINGING");
								break;
							case DSP_TONE_STATE_DIALTONE:
								pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "DIALTONE");
								break;
							case DSP_TONE_STATE_TALKING:
								/* even if we don't specify this feature, it's still checked, so we always need to handle it.
									Even if we are looking for it, we need to wait a while or tones will be interpreted
									as voice, because this will match first (and this should match last). */
								if (voice && tcount > 15 && tcount >= threshold) {
									pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "VOICE");
								} else {
									match = 0;
								}
								break;
							case DSP_TONE_STATE_BUSY:
								pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "BUSY");
								break;
							case DSP_TONE_STATE_SPECIAL3:
								pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "SIT");
								break;
							case DSP_TONE_STATE_HUNGUP: /* UK only */
								pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "NUT");
								break;
							default:
								match = 0;
								ast_debug(1, "Something else we weren't expecting? tstate: %d, #%d\n", tstate, tcount);
							}
						}
					}
				}
			}
			ast_frfree(frame);
		} else {
			pbx_builtin_setvar_helper(chan, "TONESCANSTATUS", "HANGUP");
		}
	} while (!match && (timeout == 0 || remaining_time > 0));
	ast_dsp_free(dsp);
	if (dsp2) {
		ast_dsp_free(dsp2);
        }

	return 0;
}

static struct ast_custom_function detect_function = {
	.name = "TONE_DETECT",
	.read = detect_read,
	.write = detect_write,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(waitapp);
	res |= ast_unregister_application(scanapp);
	res |= ast_custom_function_unregister(&detect_function);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(waitapp, wait_exec);
	res |= ast_register_application_xml(scanapp, scan_exec);
	res |= ast_custom_function_register(&detect_function);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Tone detection module");
