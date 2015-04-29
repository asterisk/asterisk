/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Speech Recognition Utility Applications
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
	<use type="module">res_speech</use>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE();

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/speech.h"

/*** DOCUMENTATION
	<application name="SpeechCreate" language="en_US">
		<synopsis>
			Create a Speech Structure.
		</synopsis>
		<syntax>
			<parameter name="engine_name" required="true" />
		</syntax>
		<description>
			<para>This application creates information to be used by all the other applications.
			It must be called before doing any speech recognition activities such as activating a grammar.
			It takes the engine name to use as the argument, if not specified the default engine will be used.</para>
			<para>Sets the ERROR channel variable to 1 if the engine cannot be used.</para>
		</description>
	</application>
	<application name="SpeechActivateGrammar" language="en_US">
		<synopsis>
			Activate a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar_name" required="true" />
		</syntax>
		<description>
			<para>This activates the specified grammar to be recognized by the engine.
			A grammar tells the speech recognition engine what to recognize, and how to portray it back to you
			in the dialplan. The grammar name is the only argument to this application.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<application name="SpeechStart" language="en_US">
		<synopsis>
			Start recognizing voice in the audio stream.
		</synopsis>
		<syntax />
		<description>
			<para>Tell the speech recognition engine that it should start trying to get results from audio being
			fed to it.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<application name="SpeechBackground" language="en_US">
		<synopsis>
			Play a sound file and wait for speech to be recognized.
		</synopsis>
		<syntax>
			<parameter name="sound_file" required="true" />
			<parameter name="timeout">
				<para>Timeout integer in seconds. Note the timeout will only start
				once the sound file has stopped playing.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="n">
						<para>Don't answer the channel if it has not already been answered.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application plays a sound file and waits for the person to speak. Once they start speaking playback
			of the file stops, and silence is heard. Once they stop talking the processing sound is played to indicate
			the speech recognition engine is working. Once results are available the application returns and results
			(score and text) are available using dialplan functions.</para>
			<para>The first text and score are ${SPEECH_TEXT(0)} AND ${SPEECH_SCORE(0)} while the second are ${SPEECH_TEXT(1)}
			and ${SPEECH_SCORE(1)}.</para>
			<para>The first argument is the sound file and the second is the timeout integer in seconds.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
			
		</description>
	</application>
	<application name="SpeechDeactivateGrammar" language="en_US">
		<synopsis>
			Deactivate a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar_name" required="true">
				<para>The grammar name to deactivate</para>
			</parameter>
		</syntax>
		<description>
			<para>This deactivates the specified grammar so that it is no longer recognized.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<application name="SpeechProcessingSound" language="en_US">
		<synopsis>
			Change background processing sound.
		</synopsis>
		<syntax>
			<parameter name="sound_file" required="true" />
		</syntax>
		<description>
			<para>This changes the processing sound that SpeechBackground plays back when the speech recognition engine is
			processing and working to get results.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<application name="SpeechDestroy" language="en_US">
		<synopsis>
			End speech recognition.
		</synopsis>
		<syntax />
		<description>
			<para>This destroys the information used by all the other speech recognition applications.
			If you call this application but end up wanting to recognize more speech, you must call SpeechCreate()
			again before calling any other application.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<application name="SpeechLoadGrammar" language="en_US">
		<synopsis>
			Load a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar_name" required="true" />
			<parameter name="path" required="true" />
		</syntax>
		<description>
			<para>Load a grammar only on the channel, not globally.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<application name="SpeechUnloadGrammar" language="en_US">
		<synopsis>
			Unload a grammar.
		</synopsis>
		<syntax>
			<parameter name="grammar_name" required="true" />
		</syntax>
		<description>
			<para>Unload a grammar.</para>
			<para>Hangs up the channel on failure. If this is not desired, use TryExec.</para>
		</description>
	</application>
	<function name="SPEECH_SCORE" language="en_US">
		<synopsis>
			Gets the confidence score of a result.
		</synopsis>
		<syntax argsep="/">
			<parameter name="nbest_number" />
			<parameter name="result_number" required="true" />
		</syntax>
		<description>
			<para>Gets the confidence score of a result.</para>
		</description>
	</function>
	<function name="SPEECH_TEXT" language="en_US">
		<synopsis>
			Gets the recognized text of a result.
		</synopsis>
		<syntax argsep="/">
			<parameter name="nbest_number" />
			<parameter name="result_number" required="true" />
		</syntax>
		<description>
			<para>Gets the recognized text of a result.</para>
		</description>
	</function>
	<function name="SPEECH_GRAMMAR" language="en_US">
		<synopsis>
			Gets the matched grammar of a result if available.
		</synopsis>
		<syntax argsep="/">
			<parameter name="nbest_number" />
			<parameter name="result_number" required="true" />
		</syntax>
		<description>
			<para>Gets the matched grammar of a result if available.</para>
		</description>
	</function>
	<function name="SPEECH_ENGINE" language="en_US">
		<synopsis>
			Get or change a speech engine specific attribute.
		</synopsis>
		<syntax>
			<parameter name="name" required="true" />
		</syntax>
		<description>
			<para>Changes a speech engine specific attribute.</para>
		</description>
	</function>
	<function name="SPEECH_RESULTS_TYPE" language="en_US">
		<synopsis>
			Sets the type of results that will be returned.
		</synopsis>
		<syntax />
		<description>
			<para>Sets the type of results that will be returned. Valid options are normal or nbest.</para>
		</description>
	</function>
	<function name="SPEECH" language="en_US">
		<synopsis>
			Gets information about speech recognition results.
		</synopsis>
		<syntax>
			<parameter name="argument" required="true">
				<enumlist>
					<enum name="status">
						<para>Returns <literal>1</literal> upon speech object existing,
						or <literal>0</literal> if not</para>
					</enum>
					<enum name="spoke">
						<para>Returns <literal>1</literal> if spoker spoke,
						or <literal>0</literal> if not</para>
					</enum>
					<enum name="results">
						<para>Returns number of results that were recognized.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Gets information about speech recognition results.</para>
		</description>
	</function>
 ***/

/*! \brief Helper function used by datastores to destroy the speech structure upon hangup */
static void destroy_callback(void *data)
{
	struct ast_speech *speech = (struct ast_speech*)data;

	if (speech == NULL) {
		return;
	}

	/* Deallocate now */
	ast_speech_destroy(speech);

	return;
}

/*! \brief Static structure for datastore information */
static const struct ast_datastore_info speech_datastore = {
	.type = "speech",
	.destroy = destroy_callback
};

/*! \brief Helper function used to find the speech structure attached to a channel */
static struct ast_speech *find_speech(struct ast_channel *chan)
{
	struct ast_speech *speech = NULL;
	struct ast_datastore *datastore = NULL;

	if (!chan) {
		return NULL;
	}

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
	ast_channel_unlock(chan);
	if (datastore == NULL) {
		return NULL;
	}
	speech = datastore->data;

	return speech;
}

/*!
 * \internal
 * \brief Destroy the speech datastore on the given channel.
 *
 * \param chan Channel to destroy speech datastore.
 *
 * \retval 0 on success.
 * \retval -1 not found.
 */
static int speech_datastore_destroy(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	int res;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
	if (datastore) {
		ast_channel_datastore_remove(chan, datastore);
	}
	ast_channel_unlock(chan);
	if (datastore) {
		ast_datastore_free(datastore);
		res = 0;
	} else {
		res = -1;
	}
	return res;
}

/* Helper function to find a specific speech recognition result by number and nbest alternative */
static struct ast_speech_result *find_result(struct ast_speech_result *results, char *result_num)
{
	struct ast_speech_result *result = results;
	char *tmp = NULL;
	int nbest_num = 0, wanted_num = 0, i = 0;

	if (!result) {
		return NULL;
	}

	if ((tmp = strchr(result_num, '/'))) {
		*tmp++ = '\0';
		nbest_num = atoi(result_num);
		wanted_num = atoi(tmp);
	} else {
		wanted_num = atoi(result_num);
	}

	do {
		if (result->nbest_num != nbest_num)
			continue;
		if (i == wanted_num)
			break;
		i++;
	} while ((result = AST_LIST_NEXT(result, list)));

	return result;
}

/*! \brief SPEECH_SCORE() Dialplan Function */
static int speech_score(struct ast_channel *chan, const char *cmd, char *data,
		       char *buf, size_t len)
{
	struct ast_speech_result *result = NULL;
	struct ast_speech *speech = find_speech(chan);
	char tmp[128] = "";

	if (data == NULL || speech == NULL || !(result = find_result(speech->results, data))) {
		return -1;
	}
	
	snprintf(tmp, sizeof(tmp), "%d", result->score);
	
	ast_copy_string(buf, tmp, len);

	return 0;
}

static struct ast_custom_function speech_score_function = {
	.name = "SPEECH_SCORE",
	.read = speech_score,
	.write = NULL,
};

/*! \brief SPEECH_TEXT() Dialplan Function */
static int speech_text(struct ast_channel *chan, const char *cmd, char *data,
			char *buf, size_t len)
{
	struct ast_speech_result *result = NULL;
	struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL || !(result = find_result(speech->results, data))) {
		return -1;
	}

	if (result->text != NULL) {
		ast_copy_string(buf, result->text, len);
	} else {
		buf[0] = '\0';
	}

	return 0;
}

static struct ast_custom_function speech_text_function = {
	.name = "SPEECH_TEXT",
	.read = speech_text,
	.write = NULL,
};

/*! \brief SPEECH_GRAMMAR() Dialplan Function */
static int speech_grammar(struct ast_channel *chan, const char *cmd, char *data,
			char *buf, size_t len)
{
	struct ast_speech_result *result = NULL;
	struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL || !(result = find_result(speech->results, data))) {
		return -1;
	}

	if (result->grammar != NULL) {
		ast_copy_string(buf, result->grammar, len);
	} else {
		buf[0] = '\0';
	}

	return 0;
}

static struct ast_custom_function speech_grammar_function = {
	.name = "SPEECH_GRAMMAR",
	.read = speech_grammar,
	.write = NULL,
};

/*! \brief SPEECH_ENGINE() Dialplan Set Function */
static int speech_engine_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL) {
		return -1;
	}

	ast_speech_change(speech, data, value);

	return 0;
}

/*! \brief SPEECH_ENGINE() Dialplan Get Function */
static int speech_engine_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_speech *speech = find_speech(chan);

	if (!data || !speech) {
		return -1;
	}

	return ast_speech_get_setting(speech, data, buf, len);
}

static struct ast_custom_function speech_engine_function = {
	.name = "SPEECH_ENGINE",
	.read = speech_engine_read,
	.write = speech_engine_write,
};

/*! \brief SPEECH_RESULTS_TYPE() Dialplan Function */
static int speech_results_type_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL)
		return -1;

	if (!strcasecmp(value, "normal"))
		ast_speech_change_results_type(speech, AST_SPEECH_RESULTS_TYPE_NORMAL);
	else if (!strcasecmp(value, "nbest"))
		ast_speech_change_results_type(speech, AST_SPEECH_RESULTS_TYPE_NBEST);

	return 0;
}

static struct ast_custom_function speech_results_type_function = {
	.name = "SPEECH_RESULTS_TYPE",
	.read = NULL,
	.write = speech_results_type_write,
};

/*! \brief SPEECH() Dialplan Function */
static int speech_read(struct ast_channel *chan, const char *cmd, char *data,
			char *buf, size_t len)
{
	int results = 0;
	struct ast_speech_result *result = NULL;
	struct ast_speech *speech = find_speech(chan);
	char tmp[128] = "";

	/* Now go for the various options */
	if (!strcasecmp(data, "status")) {
		if (speech != NULL)
			ast_copy_string(buf, "1", len);
		else
			ast_copy_string(buf, "0", len);
		return 0;
	}

	/* Make sure we have a speech structure for everything else */
	if (speech == NULL) {
		return -1;
	}

	/* Check to see if they are checking for silence */
	if (!strcasecmp(data, "spoke")) {
		if (ast_test_flag(speech, AST_SPEECH_SPOKE))
			ast_copy_string(buf, "1", len);
		else
			ast_copy_string(buf, "0", len);
	} else if (!strcasecmp(data, "results")) {
		/* Count number of results */
		for (result = speech->results; result; result = AST_LIST_NEXT(result, list))
			results++;
		snprintf(tmp, sizeof(tmp), "%d", results);
		ast_copy_string(buf, tmp, len);
	} else {
		buf[0] = '\0';
	}

	return 0;
}

static struct ast_custom_function speech_function = {
	.name = "SPEECH",
	.read = speech_read,
	.write = NULL,
};



/*! \brief SpeechCreate() Dialplan Application */
static int speech_create(struct ast_channel *chan, const char *data)
{
	struct ast_speech *speech = NULL;
	struct ast_datastore *datastore = NULL;

	/* Request a speech object */
	speech = ast_speech_new(data, ast_channel_nativeformats(chan));
	if (speech == NULL) {
		/* Not available */
		pbx_builtin_setvar_helper(chan, "ERROR", "1");
		return 0;
	}

	datastore = ast_datastore_alloc(&speech_datastore, NULL);
	if (datastore == NULL) {
		ast_speech_destroy(speech);
		pbx_builtin_setvar_helper(chan, "ERROR", "1");
		return 0;
	}
	pbx_builtin_setvar_helper(chan, "ERROR", NULL);
	datastore->data = speech;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	return 0;
}

/*! \brief SpeechLoadGrammar(Grammar Name,Path) Dialplan Application */
static int speech_load(struct ast_channel *chan, const char *vdata)
{
	int res = 0;
	struct ast_speech *speech = find_speech(chan);
	char *data;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(grammar);
		AST_APP_ARG(path);
	);

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	if (speech == NULL)
		return -1;

	if (args.argc != 2)
		return -1;

	/* Load the grammar locally on the object */
	res = ast_speech_grammar_load(speech, args.grammar, args.path);

	return res;
}

/*! \brief SpeechUnloadGrammar(Grammar Name) Dialplan Application */
static int speech_unload(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_speech *speech = find_speech(chan);

	if (speech == NULL)
		return -1;

	/* Unload the grammar */
	res = ast_speech_grammar_unload(speech, data);

	return res;
}

/*! \brief SpeechDeactivateGrammar(Grammar Name) Dialplan Application */
static int speech_deactivate(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_speech *speech = find_speech(chan);

	if (speech == NULL)
		return -1;

	/* Deactivate the grammar on the speech object */
	res = ast_speech_grammar_deactivate(speech, data);

	return res;
}

/*! \brief SpeechActivateGrammar(Grammar Name) Dialplan Application */
static int speech_activate(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_speech *speech = find_speech(chan);

	if (speech == NULL)
		return -1;

	/* Activate the grammar on the speech object */
	res = ast_speech_grammar_activate(speech, data);

	return res;
}

/*! \brief SpeechStart() Dialplan Application */
static int speech_start(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_speech *speech = find_speech(chan);

	if (speech == NULL)
		return -1;

	ast_speech_start(speech);

	return res;
}

/*! \brief SpeechProcessingSound(Sound File) Dialplan Application */
static int speech_processing_sound(struct ast_channel *chan, const char *data)
{
	int res = 0;
	struct ast_speech *speech = find_speech(chan);

	if (speech == NULL)
		return -1;

	if (speech->processing_sound != NULL) {
		ast_free(speech->processing_sound);
		speech->processing_sound = NULL;
	}

	speech->processing_sound = ast_strdup(data);

	return res;
}

/*! \brief Helper function used by speech_background to playback a soundfile */
static int speech_streamfile(struct ast_channel *chan, const char *filename, const char *preflang)
{
	struct ast_filestream *fs = NULL;

	if (!(fs = ast_openstream(chan, filename, preflang)))
		return -1;
	
	if (ast_applystream(chan, fs))
		return -1;
	
	ast_playstream(fs);

	return 0;
}

enum {
	SB_OPT_NOANSWER = (1 << 0),
};

AST_APP_OPTIONS(speech_background_options, BEGIN_OPTIONS
	AST_APP_OPTION('n', SB_OPT_NOANSWER),
END_OPTIONS );

/*! \brief SpeechBackground(Sound File,Timeout) Dialplan Application */
static int speech_background(struct ast_channel *chan, const char *data)
{
	unsigned int timeout = 0;
	int res = 0, done = 0, started = 0, quieted = 0, max_dtmf_len = 0;
	struct ast_speech *speech = find_speech(chan);
	struct ast_frame *f = NULL;
	RAII_VAR(struct ast_format *, oldreadformat, NULL, ao2_cleanup);
	char dtmf[AST_MAX_EXTENSION] = "";
	struct timeval start = { 0, 0 }, current;
	char *parse, *filename_tmp = NULL, *filename = NULL, tmp[2] = "", dtmf_terminator = '#';
	const char *tmp2 = NULL;
	struct ast_flags options = { 0 };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(soundfile);
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (speech == NULL)
		return -1;

	if (!ast_strlen_zero(args.options)) {
		char *options_buf = ast_strdupa(args.options);
		ast_app_parse_options(speech_background_options, &options, NULL, options_buf);
	}

	/* If channel is not already answered, then answer it */
	if (ast_channel_state(chan) != AST_STATE_UP && !ast_test_flag(&options, SB_OPT_NOANSWER)
		&& ast_answer(chan)) {
			return -1;
	}

	/* Record old read format */
	oldreadformat = ao2_bump(ast_channel_readformat(chan));

	/* Change read format to be signed linear */
	if (ast_set_read_format(chan, speech->format))
		return -1;

	if (!ast_strlen_zero(args.soundfile)) {
		/* Yay sound file */
		filename_tmp = ast_strdupa(args.soundfile);
		if (!ast_strlen_zero(args.timeout)) {
			if ((timeout = atof(args.timeout) * 1000.0) == 0)
				timeout = -1;
		} else
			timeout = 0;
	}

	/* See if the maximum DTMF length variable is set... we use a variable in case they want to carry it through their entire dialplan */
	ast_channel_lock(chan);
	if ((tmp2 = pbx_builtin_getvar_helper(chan, "SPEECH_DTMF_MAXLEN")) && !ast_strlen_zero(tmp2)) {
		max_dtmf_len = atoi(tmp2);
	}
	
	/* See if a terminator is specified */
	if ((tmp2 = pbx_builtin_getvar_helper(chan, "SPEECH_DTMF_TERMINATOR"))) {
		if (ast_strlen_zero(tmp2))
			dtmf_terminator = '\0';
		else
			dtmf_terminator = tmp2[0];
	}
	ast_channel_unlock(chan);

	/* Before we go into waiting for stuff... make sure the structure is ready, if not - start it again */
	if (speech->state == AST_SPEECH_STATE_NOT_READY || speech->state == AST_SPEECH_STATE_DONE) {
		ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);
		ast_speech_start(speech);
	}

	/* Ensure no streams are currently running */
	ast_stopstream(chan);

	/* Okay it's streaming so go into a loop grabbing frames! */
	while (done == 0) {
		/* If the filename is null and stream is not running, start up a new sound file */
		if (!quieted && (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL) && (filename = strsep(&filename_tmp, "&"))) {
			/* Discard old stream information */
			ast_stopstream(chan);
			/* Start new stream */
			speech_streamfile(chan, filename, ast_channel_language(chan));
		}

		/* Run scheduled stuff */
		ast_sched_runq(ast_channel_sched(chan));

		/* Yay scheduling */
		res = ast_sched_wait(ast_channel_sched(chan));
		if (res < 0)
			res = 1000;

		/* If there is a frame waiting, get it - if not - oh well */
		if (ast_waitfor(chan, res) > 0) {
			f = ast_read(chan);
			if (f == NULL) {
				/* The channel has hung up most likely */
				done = 3;
				break;
			}
		}

		/* Do timeout check (shared between audio/dtmf) */
		if ((!quieted || strlen(dtmf)) && started == 1) {
			current = ast_tvnow();
			if ((ast_tvdiff_ms(current, start)) >= timeout) {
				done = 1;
				if (f)
					ast_frfree(f);
				break;
			}
		}

		/* Do checks on speech structure to see if it's changed */
		ast_mutex_lock(&speech->lock);
		if (ast_test_flag(speech, AST_SPEECH_QUIET)) {
			if (ast_channel_stream(chan))
				ast_stopstream(chan);
			ast_clear_flag(speech, AST_SPEECH_QUIET);
			quieted = 1;
		}
		/* Check state so we can see what to do */
		switch (speech->state) {
		case AST_SPEECH_STATE_READY:
			/* If audio playback has stopped do a check for timeout purposes */
			if (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL)
				ast_stopstream(chan);
			if (!quieted && ast_channel_stream(chan) == NULL && timeout && started == 0 && !filename_tmp) {
				if (timeout == -1) {
					done = 1;
					if (f)
						ast_frfree(f);
					break;
				}
				start = ast_tvnow();
				started = 1;
			}
			/* Write audio frame out to speech engine if no DTMF has been received */
			if (!strlen(dtmf) && f != NULL && f->frametype == AST_FRAME_VOICE) {
				ast_speech_write(speech, f->data.ptr, f->datalen);
			}
			break;
		case AST_SPEECH_STATE_WAIT:
			/* Cue up waiting sound if not already playing */
			if (!strlen(dtmf)) {
				if (ast_channel_stream(chan) == NULL) {
					if (speech->processing_sound != NULL) {
						if (strlen(speech->processing_sound) > 0 && strcasecmp(speech->processing_sound, "none")) {
							speech_streamfile(chan, speech->processing_sound, ast_channel_language(chan));
						}
					}
				} else if (ast_channel_streamid(chan) == -1 && ast_channel_timingfunc(chan) == NULL) {
					ast_stopstream(chan);
					if (speech->processing_sound != NULL) {
						if (strlen(speech->processing_sound) > 0 && strcasecmp(speech->processing_sound, "none")) {
							speech_streamfile(chan, speech->processing_sound, ast_channel_language(chan));
						}
					}
				}
			}
			break;
		case AST_SPEECH_STATE_DONE:
			/* Now that we are done... let's switch back to not ready state */
			ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);
			if (!strlen(dtmf)) {
				/* Copy to speech structure the results, if available */
				speech->results = ast_speech_results_get(speech);
				/* Break out of our background too */
				done = 1;
				/* Stop audio playback */
				if (ast_channel_stream(chan) != NULL) {
					ast_stopstream(chan);
				}
			}
			break;
		default:
			break;
		}
		ast_mutex_unlock(&speech->lock);

		/* Deal with other frame types */
		if (f != NULL) {
			/* Free the frame we received */
			switch (f->frametype) {
			case AST_FRAME_DTMF:
				if (dtmf_terminator != '\0' && f->subclass.integer == dtmf_terminator) {
					done = 1;
				} else {
					quieted = 1;
					if (ast_channel_stream(chan) != NULL) {
						ast_stopstream(chan);
					}
					if (!started) {
						/* Change timeout to be 5 seconds for DTMF input */
						timeout = (ast_channel_pbx(chan) && ast_channel_pbx(chan)->dtimeoutms) ? ast_channel_pbx(chan)->dtimeoutms : 5000;
						started = 1;
					}
					start = ast_tvnow();
					snprintf(tmp, sizeof(tmp), "%c", f->subclass.integer);
					strncat(dtmf, tmp, sizeof(dtmf) - strlen(dtmf) - 1);
					/* If the maximum length of the DTMF has been reached, stop now */
					if (max_dtmf_len && strlen(dtmf) == max_dtmf_len)
						done = 1;
				}
				break;
			case AST_FRAME_CONTROL:
				switch (f->subclass.integer) {
				case AST_CONTROL_HANGUP:
					/* Since they hung up we should destroy the speech structure */
					done = 3;
				default:
					break;
				}
			default:
				break;
			}
			ast_frfree(f);
			f = NULL;
		}
	}

	if (!ast_strlen_zero(dtmf)) {
		/* We sort of make a results entry */
		speech->results = ast_calloc(1, sizeof(*speech->results));
		if (speech->results != NULL) {
			ast_speech_dtmf(speech, dtmf);
			speech->results->score = 1000;
			speech->results->text = ast_strdup(dtmf);
			speech->results->grammar = ast_strdup("dtmf");
		}
		ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);
	}

	/* See if it was because they hung up */
	if (done == 3) {
		speech_datastore_destroy(chan);
	} else {
		/* Channel is okay so restore read format */
		ast_set_read_format(chan, oldreadformat);
	}

	return 0;
}


/*! \brief SpeechDestroy() Dialplan Application */
static int speech_destroy(struct ast_channel *chan, const char *data)
{
	if (!chan) {
		return -1;
	}
	return speech_datastore_destroy(chan);
}

static int load_module(void)
{
	int res = 0;

	res = ast_register_application_xml("SpeechCreate", speech_create);
	res |= ast_register_application_xml("SpeechLoadGrammar", speech_load);
	res |= ast_register_application_xml("SpeechUnloadGrammar", speech_unload);
	res |= ast_register_application_xml("SpeechActivateGrammar", speech_activate);
	res |= ast_register_application_xml("SpeechDeactivateGrammar", speech_deactivate);
	res |= ast_register_application_xml("SpeechStart", speech_start);
	res |= ast_register_application_xml("SpeechBackground", speech_background);
	res |= ast_register_application_xml("SpeechDestroy", speech_destroy);
	res |= ast_register_application_xml("SpeechProcessingSound", speech_processing_sound);
	res |= ast_custom_function_register(&speech_function);
	res |= ast_custom_function_register(&speech_score_function);
	res |= ast_custom_function_register(&speech_text_function);
	res |= ast_custom_function_register(&speech_grammar_function);
	res |= ast_custom_function_register(&speech_engine_function);
	res |= ast_custom_function_register(&speech_results_type_function);

	return res;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Dialplan Speech Applications");
