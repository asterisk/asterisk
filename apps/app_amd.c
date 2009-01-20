/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2003 - 2006, Aheeva Technology.
 *
 * Claude Klimos (claude.klimos@aheeva.com)
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
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 */

#include "asterisk.h"
 
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>

#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/app.h"


static char *app = "AMD";
static char *synopsis = "Attempts to detect answering machines";
static char *descrip =
"  AMD([initialSilence][|greeting][|afterGreetingSilence][|totalAnalysisTime]\n"
"      [|minimumWordLength][|betweenWordsSilence][|maximumNumberOfWords]\n"
"      [|silenceThreshold])\n"
"  This application attempts to detect answering machines at the beginning\n"
"  of outbound calls.  Simply call this application after the call\n"
"  has been answered (outbound only, of course).\n"
"  When loaded, AMD reads amd.conf and uses the parameters specified as\n"
"  default values. Those default values get overwritten when calling AMD\n"
"  with parameters.\n"
"- 'initialSilence' is the maximum silence duration before the greeting. If\n"
"   exceeded then MACHINE.\n"
"- 'greeting' is the maximum length of a greeting. If exceeded then MACHINE.\n"
"- 'afterGreetingSilence' is the silence after detecting a greeting.\n"
"   If exceeded then HUMAN.\n"
"- 'totalAnalysisTime' is the maximum time allowed for the algorithm to decide\n"
"   on a HUMAN or MACHINE.\n"
"- 'minimumWordLength'is the minimum duration of Voice to considered as a word.\n"
"- 'betweenWordsSilence' is the minimum duration of silence after a word to \n"
"   consider the audio that follows as a new word.\n"
"- 'maximumNumberOfWords'is the maximum number of words in the greeting. \n"
"   If exceeded then MACHINE.\n"
"- 'silenceThreshold' is the silence threshold.\n"
"This application sets the following channel variable upon completion:\n"
"    AMDSTATUS - This is the status of the answering machine detection.\n"
"                Possible values are:\n"
"                MACHINE | HUMAN | NOTSURE | HANGUP\n"
"    AMDCAUSE - Indicates the cause that led to the conclusion.\n"
"               Possible values are:\n"
"               TOOLONG-<%d total_time>\n"
"               INITIALSILENCE-<%d silenceDuration>-<%d initialSilence>\n"
"               HUMAN-<%d silenceDuration>-<%d afterGreetingSilence>\n"
"               MAXWORDS-<%d wordsCount>-<%d maximumNumberOfWords>\n"
"               LONGGREETING-<%d voiceDuration>-<%d greeting>\n";

#define STATE_IN_WORD       1
#define STATE_IN_SILENCE    2

/* Some default values for the algorithm parameters. These defaults will be overwritten from amd.conf */
static int dfltInitialSilence       = 2500;
static int dfltGreeting             = 1500;
static int dfltAfterGreetingSilence = 800;
static int dfltTotalAnalysisTime    = 5000;
static int dfltMinimumWordLength    = 100;
static int dfltBetweenWordsSilence  = 50;
static int dfltMaximumNumberOfWords = 3;
static int dfltSilenceThreshold     = 256;

/* Set to the lowest ms value provided in amd.conf or application parameters */
static int dfltMaxWaitTimeForFrame  = 50;

static void isAnsweringMachine(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_frame *f = NULL;
	struct ast_dsp *silenceDetector = NULL;
	int dspsilence = 0, readFormat, framelength = 0;
	int inInitialSilence = 1;
	int inGreeting = 0;
	int voiceDuration = 0;
	int silenceDuration = 0;
	int iTotalTime = 0;
	int iWordsCount = 0;
	int currentState = STATE_IN_WORD;
	int previousState = STATE_IN_SILENCE;
	int consecutiveVoiceDuration = 0;
	char amdCause[256] = "", amdStatus[256] = "";
	char *parse = ast_strdupa(data);

	/* Lets set the initial values of the variables that will control the algorithm.
	   The initial values are the default ones. If they are passed as arguments
	   when invoking the application, then the default values will be overwritten
	   by the ones passed as parameters. */
	int initialSilence       = dfltInitialSilence;
	int greeting             = dfltGreeting;
	int afterGreetingSilence = dfltAfterGreetingSilence;
	int totalAnalysisTime    = dfltTotalAnalysisTime;
	int minimumWordLength    = dfltMinimumWordLength;
	int betweenWordsSilence  = dfltBetweenWordsSilence;
	int maximumNumberOfWords = dfltMaximumNumberOfWords;
	int silenceThreshold     = dfltSilenceThreshold;
	int maxWaitTimeForFrame  = dfltMaxWaitTimeForFrame;

	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(argInitialSilence);
			     AST_APP_ARG(argGreeting);
			     AST_APP_ARG(argAfterGreetingSilence);
			     AST_APP_ARG(argTotalAnalysisTime);
			     AST_APP_ARG(argMinimumWordLength);
			     AST_APP_ARG(argBetweenWordsSilence);
			     AST_APP_ARG(argMaximumNumberOfWords);
			     AST_APP_ARG(argSilenceThreshold);
	);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AMD: %s %s %s (Fmt: %d)\n", chan->name ,chan->cid.cid_ani, chan->cid.cid_rdnis, chan->readformat);

	/* Lets parse the arguments. */
	if (!ast_strlen_zero(parse)) {
		/* Some arguments have been passed. Lets parse them and overwrite the defaults. */
		AST_STANDARD_APP_ARGS(args, parse);
		if (!ast_strlen_zero(args.argInitialSilence))
			initialSilence = atoi(args.argInitialSilence);
		if (!ast_strlen_zero(args.argGreeting))
			greeting = atoi(args.argGreeting);
		if (!ast_strlen_zero(args.argAfterGreetingSilence))
			afterGreetingSilence = atoi(args.argAfterGreetingSilence);
		if (!ast_strlen_zero(args.argTotalAnalysisTime))
			totalAnalysisTime = atoi(args.argTotalAnalysisTime);
		if (!ast_strlen_zero(args.argMinimumWordLength))
			minimumWordLength = atoi(args.argMinimumWordLength);
		if (!ast_strlen_zero(args.argBetweenWordsSilence))
			betweenWordsSilence = atoi(args.argBetweenWordsSilence);
		if (!ast_strlen_zero(args.argMaximumNumberOfWords))
			maximumNumberOfWords = atoi(args.argMaximumNumberOfWords);
		if (!ast_strlen_zero(args.argSilenceThreshold))
			silenceThreshold = atoi(args.argSilenceThreshold);
	} else if (option_debug)
		ast_log(LOG_DEBUG, "AMD using the default parameters.\n");

	/* Find lowest ms value, that will be max wait time for a frame */
	if (maxWaitTimeForFrame > initialSilence)
		maxWaitTimeForFrame = initialSilence;
	if (maxWaitTimeForFrame > greeting)
		maxWaitTimeForFrame = greeting;
	if (maxWaitTimeForFrame > afterGreetingSilence)
		maxWaitTimeForFrame = afterGreetingSilence;
	if (maxWaitTimeForFrame > totalAnalysisTime)
		maxWaitTimeForFrame = totalAnalysisTime;
	if (maxWaitTimeForFrame > minimumWordLength)
		maxWaitTimeForFrame = minimumWordLength;
	if (maxWaitTimeForFrame > betweenWordsSilence)
		maxWaitTimeForFrame = betweenWordsSilence;

	/* Now we're ready to roll! */
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AMD: initialSilence [%d] greeting [%d] afterGreetingSilence [%d] "
		"totalAnalysisTime [%d] minimumWordLength [%d] betweenWordsSilence [%d] maximumNumberOfWords [%d] silenceThreshold [%d] \n",
				initialSilence, greeting, afterGreetingSilence, totalAnalysisTime,
				minimumWordLength, betweenWordsSilence, maximumNumberOfWords, silenceThreshold );

	/* Set read format to signed linear so we get signed linear frames in */
	readFormat = chan->readformat;
	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0 ) {
		ast_log(LOG_WARNING, "AMD: Channel [%s]. Unable to set to linear mode, giving up\n", chan->name );
		pbx_builtin_setvar_helper(chan , "AMDSTATUS", "");
		pbx_builtin_setvar_helper(chan , "AMDCAUSE", "");
		return;
	}

	/* Create a new DSP that will detect the silence */
	if (!(silenceDetector = ast_dsp_new())) {
		ast_log(LOG_WARNING, "AMD: Channel [%s]. Unable to create silence detector :(\n", chan->name );
		pbx_builtin_setvar_helper(chan , "AMDSTATUS", "");
		pbx_builtin_setvar_helper(chan , "AMDCAUSE", "");
		return;
	}

	/* Set silence threshold to specified value */
	ast_dsp_set_threshold(silenceDetector, silenceThreshold);

	/* Now we go into a loop waiting for frames from the channel */
	while ((res = ast_waitfor(chan, 2 * maxWaitTimeForFrame)) > -1) {

		/* If we fail to read in a frame, that means they hung up */
		if (!(f = ast_read(chan))) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "AMD: HANGUP\n");
			if (option_debug)
				ast_log(LOG_DEBUG, "Got hangup\n");
			strcpy(amdStatus, "HANGUP");
			break;
		}

		if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_NULL || f->frametype == AST_FRAME_CNG) {
			/* If the total time exceeds the analysis time then give up as we are not too sure */
			if (f->frametype == AST_FRAME_VOICE)
				framelength = (ast_codec_get_samples(f) / DEFAULT_SAMPLES_PER_MS);
			else
				framelength += 2 * maxWaitTimeForFrame;

			iTotalTime += framelength;
			if (iTotalTime >= totalAnalysisTime) {
				if (option_verbose > 2)	
					ast_verbose(VERBOSE_PREFIX_3 "AMD: Channel [%s]. Too long...\n", chan->name );
				ast_frfree(f);
				strcpy(amdStatus , "NOTSURE");
				sprintf(amdCause , "TOOLONG-%d", iTotalTime);
				break;
			}

			/* Feed the frame of audio into the silence detector and see if we get a result */
			if (f->frametype != AST_FRAME_VOICE)
				dspsilence += 2 * maxWaitTimeForFrame;
			else {
				dspsilence = 0;
				ast_dsp_silence(silenceDetector, f, &dspsilence);
			}

			if (dspsilence > 0) {
				silenceDuration = dspsilence;
				
				if (silenceDuration >= betweenWordsSilence) {
					if (currentState != STATE_IN_SILENCE ) {
						previousState = currentState;
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "AMD: Changed state to STATE_IN_SILENCE\n");
					}
					currentState  = STATE_IN_SILENCE;
					consecutiveVoiceDuration = 0;
				}
				
				if (inInitialSilence == 1  && silenceDuration >= initialSilence) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: ANSWERING MACHINE: silenceDuration:%d initialSilence:%d\n",
							    silenceDuration, initialSilence);
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE");
					sprintf(amdCause , "INITIALSILENCE-%d-%d", silenceDuration, initialSilence);
					res = 1;
					break;
				}
				
				if (silenceDuration >= afterGreetingSilence  &&  inGreeting == 1) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: HUMAN: silenceDuration:%d afterGreetingSilence:%d\n",
							    silenceDuration, afterGreetingSilence);
					ast_frfree(f);
					strcpy(amdStatus , "HUMAN");
					sprintf(amdCause , "HUMAN-%d-%d", silenceDuration, afterGreetingSilence);
					res = 1;
					break;
				}
				
			} else {
				consecutiveVoiceDuration += framelength;
				voiceDuration += framelength;
				
				/* If I have enough consecutive voice to say that I am in a Word, I can only increment the
				   number of words if my previous state was Silence, which means that I moved into a word. */
				if (consecutiveVoiceDuration >= minimumWordLength && currentState == STATE_IN_SILENCE) {
					iWordsCount++;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: Word detected. iWordsCount:%d\n", iWordsCount);
					previousState = currentState;
					currentState = STATE_IN_WORD;
				}
				
				if (iWordsCount >= maximumNumberOfWords) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: ANSWERING MACHINE: iWordsCount:%d\n", iWordsCount);
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE");
					sprintf(amdCause , "MAXWORDS-%d-%d", iWordsCount, maximumNumberOfWords);
					res = 1;
					break;
				}
				
				if (inGreeting == 1 && voiceDuration >= greeting) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: ANSWERING MACHINE: voiceDuration:%d greeting:%d\n", voiceDuration, greeting);
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE");
					sprintf(amdCause , "LONGGREETING-%d-%d", voiceDuration, greeting);
					res = 1;
					break;
				}
				
				if (voiceDuration >= minimumWordLength ) {
					silenceDuration = 0;
					inInitialSilence = 0;
					inGreeting = 1;
				}
				
			}
		}
		ast_frfree(f);
	}
	
	if (!res) {
		/* It took too long to get a frame back. Giving up. */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "AMD: Channel [%s]. Too long...\n", chan->name);
		strcpy(amdStatus , "NOTSURE");
		sprintf(amdCause , "TOOLONG-%d", iTotalTime);
	}

	/* Set the status and cause on the channel */
	pbx_builtin_setvar_helper(chan , "AMDSTATUS" , amdStatus);
	pbx_builtin_setvar_helper(chan , "AMDCAUSE" , amdCause);

	/* Restore channel read format */
	if (readFormat && ast_set_read_format(chan, readFormat))
		ast_log(LOG_WARNING, "AMD: Unable to restore read format on '%s'\n", chan->name);

	/* Free the DSP used to detect silence */
	ast_dsp_free(silenceDetector);

	return;
}


static int amd_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;

	u = ast_module_user_add(chan);
	isAnsweringMachine(chan, data);
	ast_module_user_remove(u);

	return 0;
}

static void load_config(void)
{
	struct ast_config *cfg = NULL;
	char *cat = NULL;
	struct ast_variable *var = NULL;

	if (!(cfg = ast_config_load("amd.conf"))) {
		ast_log(LOG_ERROR, "Configuration file amd.conf missing.\n");
		return;
	}

	cat = ast_category_browse(cfg, NULL);

	while (cat) {
		if (!strcasecmp(cat, "general") ) {
			var = ast_variable_browse(cfg, cat);
			while (var) {
				if (!strcasecmp(var->name, "initial_silence")) {
					dfltInitialSilence = atoi(var->value);
				} else if (!strcasecmp(var->name, "greeting")) {
					dfltGreeting = atoi(var->value);
				} else if (!strcasecmp(var->name, "after_greeting_silence")) {
					dfltAfterGreetingSilence = atoi(var->value);
				} else if (!strcasecmp(var->name, "silence_threshold")) {
					dfltSilenceThreshold = atoi(var->value);
				} else if (!strcasecmp(var->name, "total_analysis_time")) {
					dfltTotalAnalysisTime = atoi(var->value);
				} else if (!strcasecmp(var->name, "min_word_length")) {
					dfltMinimumWordLength = atoi(var->value);
				} else if (!strcasecmp(var->name, "between_words_silence")) {
					dfltBetweenWordsSilence = atoi(var->value);
				} else if (!strcasecmp(var->name, "maximum_number_of_words")) {
					dfltMaximumNumberOfWords = atoi(var->value);
				} else {
					ast_log(LOG_WARNING, "%s: Cat:%s. Unknown keyword %s at line %d of amd.conf\n",
						app, cat, var->name, var->lineno);
				}
				var = var->next;
			}
		}
		cat = ast_category_browse(cfg, cat);
	}

	ast_config_destroy(cfg);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AMD defaults: initialSilence [%d] greeting [%d] afterGreetingSilence [%d] "
		"totalAnalysisTime [%d] minimumWordLength [%d] betweenWordsSilence [%d] maximumNumberOfWords [%d] silenceThreshold [%d] \n",
				dfltInitialSilence, dfltGreeting, dfltAfterGreetingSilence, dfltTotalAnalysisTime,
				dfltMinimumWordLength, dfltBetweenWordsSilence, dfltMaximumNumberOfWords, dfltSilenceThreshold );

	return;
}

static int unload_module(void)
{
	ast_module_user_hangup_all();
	return ast_unregister_application(app);
}

static int load_module(void)
{
	load_config();
	return ast_register_application(app, amd_exec, synopsis, descrip);
}

static int reload(void)
{
	load_config();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Answering Machine Detection Application",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
