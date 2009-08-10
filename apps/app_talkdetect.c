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
 * \brief Playback a file with audio detect
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"
#include "asterisk/dsp.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="BackgroundDetect" language="en_US">
		<synopsis>
			Background a file with talk detect.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
			<parameter name="sil">
				<para>If not specified, defaults to <literal>1000</literal>.</para>
			</parameter>
			<parameter name="min">
				<para>If not specified, defaults to <literal>100</literal>.</para>
			</parameter>
			<parameter name="max">
				<para>If not specified, defaults to <literal>infinity</literal>.</para>
			</parameter>
			<parameter name="analysistime">
				<para>If not specified, defaults to <literal>infinity</literal>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Plays back <replaceable>filename</replaceable>, waiting for interruption from a given digit (the digit
			must start the beginning of a valid extension, or it will be ignored). During
			the playback of the file, audio is monitored in the receive direction, and if
			a period of non-silence which is greater than <replaceable>min</replaceable> ms yet less than
			<replaceable>max</replaceable> ms is followed by silence for at least <replaceable>sil</replaceable> ms,
			which occurs during the first <replaceable>analysistime</replaceable> ms, then the audio playback is
			aborted and processing jumps to the <replaceable>talk</replaceable> extension, if available.</para>
		</description>
	</application>
 ***/

static char *app = "BackgroundDetect";

static int background_detect_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *tmp;
	struct ast_frame *fr;
	int notsilent = 0;
	struct timeval start = { 0, 0 };
	struct timeval detection_start = { 0, 0 };
	int sil = 1000;
	int min = 100;
	int max = -1;
	int analysistime = -1;
	int continue_analysis = 1;
	int x;
	int origrformat = 0;
	struct ast_dsp *dsp = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(silence);
		AST_APP_ARG(min);
		AST_APP_ARG(max);
		AST_APP_ARG(analysistime);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "BackgroundDetect requires an argument (filename)\n");
		return -1;
	}

	tmp = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, tmp);

	if (!ast_strlen_zero(args.silence) && (sscanf(args.silence, "%30d", &x) == 1) && (x > 0)) {
		sil = x;
	}
	if (!ast_strlen_zero(args.min) && (sscanf(args.min, "%30d", &x) == 1) && (x > 0)) {
		min = x;
	}
	if (!ast_strlen_zero(args.max) && (sscanf(args.max, "%30d", &x) == 1) && (x > 0)) {
		max = x;
	}
	if (!ast_strlen_zero(args.analysistime) && (sscanf(args.analysistime, "%30d", &x) == 1) && (x > 0)) {
		analysistime = x;
	}

	ast_debug(1, "Preparing detect of '%s', sil=%d, min=%d, max=%d, analysistime=%d\n", args.filename, sil, min, max, analysistime);
	do {
		if (chan->_state != AST_STATE_UP) {
			if ((res = ast_answer(chan))) {
				break;
			}
		}

		origrformat = chan->readformat;
		if ((ast_set_read_format(chan, AST_FORMAT_SLINEAR))) {
			ast_log(LOG_WARNING, "Unable to set read format to linear!\n");
			res = -1;
			break;
		}

		if (!(dsp = ast_dsp_new())) {
			ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
			res = -1;
			break;
		}
		ast_stopstream(chan);
		if (ast_streamfile(chan, tmp, chan->language)) {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char *)data);
			break;
		}
		detection_start = ast_tvnow();
		while (chan->stream) {
			res = ast_sched_wait(chan->sched);
			if ((res < 0) && !chan->timingfunc) {
				res = 0;
				break;
			}
			if (res < 0) {
				res = 1000;
			}
			res = ast_waitfor(chan, res);
			if (res < 0) {
				ast_log(LOG_WARNING, "Waitfor failed on %s\n", chan->name);
				break;
			} else if (res > 0) {
				fr = ast_read(chan);
				if (continue_analysis && analysistime >= 0) {
					/* If we have a limit for the time to analyze voice
					 * frames and the time has not expired */
					if (ast_tvdiff_ms(ast_tvnow(), detection_start) >= analysistime) {
						continue_analysis = 0;
						ast_verb(3, "BackgroundDetect: Talk analysis time complete on %s.\n", chan->name);
					}
				}
				
				if (!fr) {
					res = -1;
					break;
				} else if (fr->frametype == AST_FRAME_DTMF) {
					char t[2];
					t[0] = fr->subclass;
					t[1] = '\0';
					if (ast_canmatch_extension(chan, chan->context, t, 1, chan->cid.cid_num)) {
						/* They entered a valid  extension, or might be anyhow */
						res = fr->subclass;
						ast_frfree(fr);
						break;
					}
				} else if ((fr->frametype == AST_FRAME_VOICE) && (fr->subclass == AST_FORMAT_SLINEAR) && continue_analysis) {
					int totalsilence;
					int ms;
					res = ast_dsp_silence(dsp, fr, &totalsilence);
					if (res && (totalsilence > sil)) {
						/* We've been quiet a little while */
						if (notsilent) {
							/* We had heard some talking */
							ms = ast_tvdiff_ms(ast_tvnow(), start);
							ms -= sil;
							if (ms < 0)
								ms = 0;
							if ((ms > min) && ((max < 0) || (ms < max))) {
								char ms_str[12];
								ast_debug(1, "Found qualified token of %d ms\n", ms);

								/* Save detected talk time (in milliseconds) */ 
								snprintf(ms_str, sizeof(ms_str), "%d", ms);	
								pbx_builtin_setvar_helper(chan, "TALK_DETECTED", ms_str);

								ast_goto_if_exists(chan, chan->context, "talk", 1);
								res = 0;
								ast_frfree(fr);
								break;
							} else {
								ast_debug(1, "Found unqualified token of %d ms\n", ms);
							}
							notsilent = 0;
						}
					} else {
						if (!notsilent) {
							/* Heard some audio, mark the begining of the token */
							start = ast_tvnow();
							ast_debug(1, "Start of voice token!\n");
							notsilent = 1;
						}
					}
				}
				ast_frfree(fr);
			}
			ast_sched_runq(chan->sched);
		}
		ast_stopstream(chan);
	} while (0);

	if (res > -1) {
		if (origrformat && ast_set_read_format(chan, origrformat)) {
			ast_log(LOG_WARNING, "Failed to restore read format for %s to %s\n", 
				chan->name, ast_getformatname(origrformat));
		}
	}
	if (dsp) {
		ast_dsp_free(dsp);
	}
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, background_detect_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Playback with Talk Detection");
