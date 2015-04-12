/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matthew Fredrickson <creslin@digium.com>
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
 * \brief Trivial application to record a sound file
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"	/* use dsp routines for silence detection */
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="Record" language="en_US">
		<synopsis>
			Record to a file.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" argsep=".">
				<argument name="filename" required="true" />
				<argument name="format" required="true">
					<para>Is the format of the file type to be recorded (wav, gsm, etc).</para>
				</argument>
			</parameter>
			<parameter name="silence">
				<para>Is the number of seconds of silence to allow before returning.</para>
			</parameter>
			<parameter name="maxduration">
				<para>Is the maximum recording duration in seconds. If missing
				or 0 there is no maximum.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Append to existing recording rather than replacing.</para>
					</option>
					<option name="n">
						<para>Do not answer, but record anyway if line not yet answered.</para>
					</option>
					<option name="o">
						<para>Exit when 0 is pressed, setting the variable <variable>RECORD_STATUS</variable>
						to <literal>OPERATOR</literal> instead of <literal>DTMF</literal></para>
					</option>
					<option name="q">
						<para>quiet (do not play a beep tone).</para>
					</option>
					<option name="s">
						<para>skip recording if the line is not yet answered.</para>
					</option>
					<option name="t">
						<para>use alternate '*' terminator key (DTMF) instead of default '#'</para>
					</option>
					<option name="x">
						<para>Ignore all terminator keys (DTMF) and keep recording until hangup.</para>
					</option>
					<option name="k">
					        <para>Keep recorded file upon hangup.</para>
					</option>
					<option name="y">
					        <para>Terminate recording if *any* DTMF digit is received.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>If filename contains <literal>%d</literal>, these characters will be replaced with a number
			incremented by one each time the file is recorded.
			Use <astcli>core show file formats</astcli> to see the available formats on your system
			User can press <literal>#</literal> to terminate the recording and continue to the next priority.
			If the user hangs up during a recording, all data will be lost and the application will terminate.</para>
			<variablelist>
				<variable name="RECORDED_FILE">
					<para>Will be set to the final filename of the recording.</para>
				</variable>
				<variable name="RECORD_STATUS">
					<para>This is the final status of the command</para>
					<value name="DTMF">A terminating DTMF was received ('#' or '*', depending upon option 't')</value>
					<value name="SILENCE">The maximum silence occurred in the recording.</value>
					<value name="SKIP">The line was not yet answered and the 's' option was specified.</value>
					<value name="TIMEOUT">The maximum length was reached.</value>
					<value name="HANGUP">The channel was hung up.</value>
					<value name="ERROR">An unrecoverable error occurred, which resulted in a WARNING to the logs.</value>
				</variable>
			</variablelist>
		</description>
	</application>

 ***/

#define OPERATOR_KEY '0'

static char *app = "Record";

enum {
	OPTION_APPEND = (1 << 0),
	OPTION_NOANSWER = (1 << 1),
	OPTION_QUIET = (1 << 2),
	OPTION_SKIP = (1 << 3),
	OPTION_STAR_TERMINATE = (1 << 4),
	OPTION_IGNORE_TERMINATE = (1 << 5),
	OPTION_KEEP = (1 << 6),
	FLAG_HAS_PERCENT = (1 << 7),
	OPTION_ANY_TERMINATE = (1 << 8),
	OPTION_OPERATOR_EXIT = (1 << 9),
};

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('a', OPTION_APPEND),
	AST_APP_OPTION('k', OPTION_KEEP),
	AST_APP_OPTION('n', OPTION_NOANSWER),
	AST_APP_OPTION('o', OPTION_OPERATOR_EXIT),
	AST_APP_OPTION('q', OPTION_QUIET),
	AST_APP_OPTION('s', OPTION_SKIP),
	AST_APP_OPTION('t', OPTION_STAR_TERMINATE),
	AST_APP_OPTION('y', OPTION_ANY_TERMINATE),
	AST_APP_OPTION('x', OPTION_IGNORE_TERMINATE),
});

/*!
 * \internal
 * \brief Used to determine what action to take when DTMF is received while recording
 * \since 13.0.0
 *
 * \param chan channel being recorded
 * \param flags option flags in use by the record application
 * \param dtmf_integer the integer value of the DTMF key received
 * \param terminator key currently set to be pressed for normal termination
 *
 * \retval 0 do not exit
 * \retval -1 do exit
 */
static int record_dtmf_response(struct ast_channel *chan, struct ast_flags *flags, int dtmf_integer, int terminator)
{
	if ((dtmf_integer == OPERATOR_KEY) &&
		(ast_test_flag(flags, OPTION_OPERATOR_EXIT))) {
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "OPERATOR");
		return -1;
	}

	if ((dtmf_integer == terminator) ||
		(ast_test_flag(flags, OPTION_ANY_TERMINATE))) {
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "DTMF");
		return -1;
	}

	return 0;
}

static int record_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int count = 0;
	char *ext = NULL, *opts[0];
	char *parse, *dir, *file;
	int i = 0;
	char tmp[256];

	struct ast_filestream *s = NULL;
	struct ast_frame *f = NULL;
	
	struct ast_dsp *sildet = NULL;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int silence = 0;		/* amount of silence to allow */
	int gotsilence = 0;		/* did we timeout for silence? */
	int maxduration = 0;		/* max duration of recording in milliseconds */
	int gottimeout = 0;		/* did we timeout for maxduration exceeded? */
	int terminator = '#';
	RAII_VAR(struct ast_format *, rfmt, NULL, ao2_cleanup);
	int ioflags;
	struct ast_silence_generator *silgen = NULL;
	struct ast_flags flags = { 0, };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(silence);
		AST_APP_ARG(maxduration);
		AST_APP_ARG(options);
	);
	int ms;
	struct timeval start;

	/* The next few lines of code parse out the filename and header from the input string */
	if (ast_strlen_zero(data)) { /* no data implies no filename or anything is present */
		ast_log(LOG_WARNING, "Record requires an argument (filename)\n");
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc == 4)
		ast_app_parse_options(app_opts, &flags, opts, args.options);

	if (!ast_strlen_zero(args.filename)) {
		if (strstr(args.filename, "%d"))
			ast_set_flag(&flags, FLAG_HAS_PERCENT);
		ext = strrchr(args.filename, '.'); /* to support filename with a . in the filename, not format */
		if (!ext)
			ext = strchr(args.filename, ':');
		if (ext) {
			*ext = '\0';
			ext++;
		}
	}
	if (!ext) {
		ast_log(LOG_WARNING, "No extension specified to filename!\n");
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
		return -1;
	}
	if (args.silence) {
		if ((sscanf(args.silence, "%30d", &i) == 1) && (i > -1)) {
			silence = i * 1000;
		} else if (!ast_strlen_zero(args.silence)) {
			ast_log(LOG_WARNING, "'%s' is not a valid silence duration\n", args.silence);
		}
	}
	
	if (args.maxduration) {
		if ((sscanf(args.maxduration, "%30d", &i) == 1) && (i > -1))
			/* Convert duration to milliseconds */
			maxduration = i * 1000;
		else if (!ast_strlen_zero(args.maxduration))
			ast_log(LOG_WARNING, "'%s' is not a valid maximum duration\n", args.maxduration);
	}

	if (ast_test_flag(&flags, OPTION_STAR_TERMINATE))
		terminator = '*';
	if (ast_test_flag(&flags, OPTION_IGNORE_TERMINATE))
		terminator = '\0';

	/* done parsing */

	/* these are to allow the use of the %d in the config file for a wild card of sort to
	  create a new file with the inputed name scheme */
	if (ast_test_flag(&flags, FLAG_HAS_PERCENT)) {
		AST_DECLARE_APP_ARGS(fname,
			AST_APP_ARG(piece)[100];
		);
		char *tmp2 = ast_strdupa(args.filename);
		char countstring[15];
		int idx;

		/* Separate each piece out by the format specifier */
		AST_NONSTANDARD_APP_ARGS(fname, tmp2, '%');
		do {
			int tmplen;
			/* First piece has no leading percent, so it's copied verbatim */
			ast_copy_string(tmp, fname.piece[0], sizeof(tmp));
			tmplen = strlen(tmp);
			for (idx = 1; idx < fname.argc; idx++) {
				if (fname.piece[idx][0] == 'd') {
					/* Substitute the count */
					snprintf(countstring, sizeof(countstring), "%d", count);
					ast_copy_string(tmp + tmplen, countstring, sizeof(tmp) - tmplen);
					tmplen += strlen(countstring);
				} else if (tmplen + 2 < sizeof(tmp)) {
					/* Unknown format specifier - just copy it verbatim */
					tmp[tmplen++] = '%';
					tmp[tmplen++] = fname.piece[idx][0];
				}
				/* Copy the remaining portion of the piece */
				ast_copy_string(tmp + tmplen, &(fname.piece[idx][1]), sizeof(tmp) - tmplen);
			}
			count++;
		} while (ast_fileexists(tmp, ext, ast_channel_language(chan)) > 0);
		pbx_builtin_setvar_helper(chan, "RECORDED_FILE", tmp);
	} else
		ast_copy_string(tmp, args.filename, sizeof(tmp));
	/* end of routine mentioned */

	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_test_flag(&flags, OPTION_SKIP)) {
			/* At the user's option, skip if the line is not up */
			pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "SKIP");
			return 0;
		} else if (!ast_test_flag(&flags, OPTION_NOANSWER)) {
			/* Otherwise answer unless we're supposed to record while on-hook */
			res = ast_answer(chan);
		}
	}

	if (res) {
		ast_log(LOG_WARNING, "Could not answer channel '%s'\n", ast_channel_name(chan));
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
		goto out;
	}

	if (!ast_test_flag(&flags, OPTION_QUIET)) {
		/* Some code to play a nice little beep to signify the start of the record operation */
		res = ast_streamfile(chan, "beep", ast_channel_language(chan));
		if (!res) {
			res = ast_waitstream(chan, "");
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(chan));
		}
		ast_stopstream(chan);
	}

	/* The end of beep code.  Now the recording starts */

	if (silence > 0) {
		rfmt = ao2_bump(ast_channel_readformat(chan));
		res = ast_set_read_format(chan, ast_format_slin);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
			return -1;
		}
		sildet = ast_dsp_new();
		if (!sildet) {
			ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
			pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
			return -1;
		}
		ast_dsp_set_threshold(sildet, ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE));
	} 

	/* Create the directory if it does not exist. */
	dir = ast_strdupa(tmp);
	if ((file = strrchr(dir, '/')))
		*file++ = '\0';
	ast_mkdir (dir, 0777);

	ioflags = ast_test_flag(&flags, OPTION_APPEND) ? O_CREAT|O_APPEND|O_WRONLY : O_CREAT|O_TRUNC|O_WRONLY;
	s = ast_writefile(tmp, ext, NULL, ioflags, 0, AST_FILE_MODE);

	if (!s) {
		ast_log(LOG_WARNING, "Could not create file %s\n", args.filename);
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
		goto out;
	}

	if (ast_opt_transmit_silence)
		silgen = ast_channel_start_silence_generator(chan);

	/* Request a video update */
	ast_indicate(chan, AST_CONTROL_VIDUPDATE);

	if (maxduration <= 0)
		maxduration = -1;

	start = ast_tvnow();
	while ((ms = ast_remaining_ms(start, maxduration))) {
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			break;
		}

		if (maxduration > 0 && ms == 0) {
			break;
		}

		f = ast_read(chan);
		if (!f) {
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			res = ast_writestream(s, f);

			if (res) {
				ast_log(LOG_WARNING, "Problem writing frame\n");
				ast_frfree(f);
				pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
				break;
			}

			if (silence > 0) {
				dspsilence = 0;
				ast_dsp_silence(sildet, f, &dspsilence);
				if (dspsilence) {
					totalsilence = dspsilence;
				} else {
					totalsilence = 0;
				}
				if (totalsilence > silence) {
					/* Ended happily with silence */
					ast_frfree(f);
					gotsilence = 1;
					pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "SILENCE");
					break;
				}
			}
		} else if (f->frametype == AST_FRAME_VIDEO) {
			res = ast_writestream(s, f);

			if (res) {
				ast_log(LOG_WARNING, "Problem writing frame\n");
				pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "ERROR");
				ast_frfree(f);
				break;
			}
		} else if (f->frametype == AST_FRAME_DTMF) {
			if (record_dtmf_response(chan, &flags, f->subclass.integer, terminator)) {
				ast_frfree(f);
				break;
			}
		}
		ast_frfree(f);
	}

	if (maxduration > 0 && !ms) {
		gottimeout = 1;
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "TIMEOUT");
	}

	if (!f) {
		ast_debug(1, "Got hangup\n");
		res = -1;
		pbx_builtin_setvar_helper(chan, "RECORD_STATUS", "HANGUP");
		if (!ast_test_flag(&flags, OPTION_KEEP)) {
			ast_filedelete(args.filename, NULL);
		}
	}

	if (gotsilence) {
		ast_stream_rewind(s, silence - 1000);
		ast_truncstream(s);
	} else if (!gottimeout && f) {
		/*
		 * Strip off the last 1/4 second of it, if we didn't end because of a timeout,
		 * or a hangup.  This must mean we ended because of a DTMF tone and while this
		 * 1/4 second stripping is very old code the most likely explanation is that it
		 * relates to stripping a partial DTMF tone.
		 */
		ast_stream_rewind(s, 250);
		ast_truncstream(s);
	}
	ast_closestream(s);

	if (silgen)
		ast_channel_stop_silence_generator(chan, silgen);

out:
	if ((silence > 0) && rfmt) {
		res = ast_set_read_format(chan, rfmt);
		if (res) {
			ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", ast_channel_name(chan));
		}
	}

	if (sildet) {
		ast_dsp_free(sildet);
	}
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, record_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Trivial Record Application");
