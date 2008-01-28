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

/*! \file
 *
 * \brief Answering machine detection
 *
 * \author Claude Klimos (claude.klimos@aheeva.com)
 */


#include "asterisk.h"
 
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/app.h"


static char *app = "AMD";
static char *synopsis = "Attempts to detect answering machines";
static char *descrip =
"  AMD([initialSilence],[greeting],[afterGreetingSilence],[totalAnalysisTime]\n"
"      ,[minimumWordLength],[betweenWordsSilence],[maximumNumberOfWords]\n"
"      ,[silenceThreshold],[|maximumWordLength])\n"
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
"- 'maximumWordLength' is the maximum duration of a word to accept. If exceeded then MACHINE\n"
"This application sets the following channel variables upon completion:\n"
"    AMDSTATUS - This is the status of the answering machine detection.\n"
"                Possible values are:\n"
"                MACHINE | HUMAN | NOTSURE | HANGUP\n"
"    AMDCAUSE - Indicates the cause that led to the conclusion.\n"
"               Possible values are:\n"
"               TOOLONG-<%d total_time>\n"
"               INITIALSILENCE-<%d silenceDuration>-<%d initialSilence>\n"
"               HUMAN-<%d silenceDuration>-<%d afterGreetingSilence>\n"
"               MAXWORDS-<%d wordsCount>-<%d maximumNumberOfWords>\n"
"               LONGGREETING-<%d voiceDuration>-<%d greeting>\n"
"               MAXWORDLENGTH-<%d consecutiveVoiceDuration>\n";

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
static int dfltMaximumWordLength    = 5000; /* Setting this to a large default so it is not used unless specify it in the configs or command line */

static void isAnsweringMachine(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_frame *f = NULL;
	struct ast_dsp *silenceDetector = NULL;
	int dspsilence = 0, readFormat, framelength;
	int inInitialSilence = 1;
	int inGreeting = 0;
	int voiceDuration = 0;
	int silenceDuration = 0;
	int iTotalTime = 0;
	int iWordsCount = 0;
	int currentState = STATE_IN_SILENCE;
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
	int maximumWordLength	 = dfltMaximumWordLength;

	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(argInitialSilence);
			     AST_APP_ARG(argGreeting);
			     AST_APP_ARG(argAfterGreetingSilence);
			     AST_APP_ARG(argTotalAnalysisTime);
			     AST_APP_ARG(argMinimumWordLength);
			     AST_APP_ARG(argBetweenWordsSilence);
			     AST_APP_ARG(argMaximumNumberOfWords);
			     AST_APP_ARG(argSilenceThreshold);
			     AST_APP_ARG(argMaximumWordLength);
	);

	ast_verb(3, "AMD: %s %s %s (Fmt: %d)\n", chan->name ,chan->cid.cid_ani, chan->cid.cid_rdnis, chan->readformat);

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
 		if (!ast_strlen_zero(args.argMaximumWordLength))
 			maximumWordLength = atoi(args.argMaximumWordLength);			
	} else {
		ast_debug(1, "AMD using the default parameters.\n");
	}

	/* Now we're ready to roll! */
	ast_verb(3, "AMD: initialSilence [%d] greeting [%d] afterGreetingSilence [%d] "
		"totalAnalysisTime [%d] minimumWordLength [%d] betweenWordsSilence [%d] maximumNumberOfWords [%d] silenceThreshold [%d] maximumWordLength [%d] \n",
				initialSilence, greeting, afterGreetingSilence, totalAnalysisTime,
				minimumWordLength, betweenWordsSilence, maximumNumberOfWords, silenceThreshold, maximumWordLength);

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
	while ((res = ast_waitfor(chan, totalAnalysisTime)) > -1) {
		/* If we fail to read in a frame, that means they hung up */
		if (!(f = ast_read(chan))) {
			ast_verb(3, "AMD: Channel [%s]. HANGUP\n", chan->name);
			ast_debug(1, "Got hangup\n");
			strcpy(amdStatus, "HANGUP");
			break;
		}

		if (f->frametype == AST_FRAME_VOICE) {
			/* If the total time exceeds the analysis time then give up as we are not too sure */
			framelength = (ast_codec_get_samples(f) / DEFAULT_SAMPLES_PER_MS);
			iTotalTime += framelength;
			if (iTotalTime >= totalAnalysisTime) {
				ast_verb(3, "AMD: Channel [%s]. Too long...\n", chan->name );
				ast_frfree(f);
				strcpy(amdStatus , "NOTSURE");
				sprintf(amdCause , "TOOLONG-%d", iTotalTime);
				break;
			}

			/* Feed the frame of audio into the silence detector and see if we get a result */
			dspsilence = 0;
			ast_dsp_silence(silenceDetector, f, &dspsilence);
			if (dspsilence) {
				silenceDuration = dspsilence;
				
				if (silenceDuration >= betweenWordsSilence) {
					if (currentState != STATE_IN_SILENCE ) {
						previousState = currentState;
						ast_verb(3, "AMD: Channel [%s]. Changed state to STATE_IN_SILENCE\n", chan->name);
					}
					/* Find words less than word duration */
 					if (consecutiveVoiceDuration < minimumWordLength && consecutiveVoiceDuration > 0){
 						ast_verb(3, "AMD: Channel [%s]. Short Word Duration: %d\n", chan->name, consecutiveVoiceDuration);
 					}					
					currentState  = STATE_IN_SILENCE;
					consecutiveVoiceDuration = 0;
				}
				
				if (inInitialSilence == 1  && silenceDuration >= initialSilence) {
					ast_verb(3, "AMD: Channel [%s]. ANSWERING MACHINE: silenceDuration:%d initialSilence:%d\n",
							    chan->name, silenceDuration, initialSilence);
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE");
					sprintf(amdCause , "INITIALSILENCE-%d-%d", silenceDuration, initialSilence);
					break;
				}
				
				if (silenceDuration >= afterGreetingSilence  &&  inGreeting == 1) {
					ast_verb(3, "AMD: Channel [%s]. HUMAN: silenceDuration:%d afterGreetingSilence:%d\n",
							    chan->name, silenceDuration, afterGreetingSilence);
					ast_frfree(f);
					strcpy(amdStatus , "HUMAN");
					sprintf(amdCause , "HUMAN-%d-%d", silenceDuration, afterGreetingSilence);
					break;
				}
				
			} else {
				consecutiveVoiceDuration += framelength;
				voiceDuration += framelength;
				
				/* If I have enough consecutive voice to say that I am in a Word, I can only increment the
				   number of words if my previous state was Silence, which means that I moved into a word. */
				if (consecutiveVoiceDuration >= minimumWordLength && currentState == STATE_IN_SILENCE) {
					iWordsCount++;
					ast_verb(3, "AMD: Channel [%s]. Word detected. iWordsCount:%d\n", chan->name, iWordsCount);
					previousState = currentState;
					currentState = STATE_IN_WORD;
				}
 				if (consecutiveVoiceDuration >= maximumWordLength){
 					ast_verb(3, "AMD: Channel [%s]. Maximum Word Length detected. [%d]\n", chan->name, consecutiveVoiceDuration);
 					ast_frfree(f);
 					strcpy(amdStatus , "MACHINE");
 					sprintf(amdCause , "MAXWORDLENGTH-%d", consecutiveVoiceDuration);
 					break;
 				}				
				if (iWordsCount >= maximumNumberOfWords) {
					ast_verb(3, "AMD: Channel [%s]. ANSWERING MACHINE: iWordsCount:%d\n", chan->name, iWordsCount);
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE");
					sprintf(amdCause , "MAXWORDS-%d-%d", iWordsCount, maximumNumberOfWords);
					break;
				}
				
				if (inGreeting == 1 && voiceDuration >= greeting) {
					ast_verb(3, "AMD: Channel [%s]. ANSWERING MACHINE: voiceDuration:%d greeting:%d\n", chan->name, voiceDuration, greeting);
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE");
					sprintf(amdCause , "LONGGREETING-%d-%d", voiceDuration, greeting);
					break;
				}
				
				if (voiceDuration >= minimumWordLength ) {
					if (silenceDuration > 0)
						ast_verb(3, "AMD: Channel [%s]. Detected Talk, previous silence duration: %d\n", chan->name, silenceDuration);
					silenceDuration = 0;
				}
				if (consecutiveVoiceDuration >= minimumWordLength && inGreeting == 0){
					/* Only go in here once to change the greeting flag when we detect the 1st word */
					if (silenceDuration > 0)
						ast_verb(3, "AMD: Channel [%s]. Before Greeting Time:  silenceDuration: %d voiceDuration: %d\n", chan->name, silenceDuration, voiceDuration);
					inInitialSilence = 0;
					inGreeting = 1;
				}
				
			}
		}
		ast_frfree(f);
	}
	
	if (!res) {
		/* It took too long to get a frame back. Giving up. */
		ast_verb(3, "AMD: Channel [%s]. Too long...\n", chan->name);
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
	isAnsweringMachine(chan, data);

	return 0;
}

static int load_config(int reload)
{
	struct ast_config *cfg = NULL;
	char *cat = NULL;
	struct ast_variable *var = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if (!(cfg = ast_config_load("amd.conf", config_flags))) {
		ast_log(LOG_ERROR, "Configuration file amd.conf missing.\n");
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

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
				} else if (!strcasecmp(var->name, "maximum_word_length")) {
					dfltMaximumWordLength = atoi(var->value);
					
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

	ast_verb(3, "AMD defaults: initialSilence [%d] greeting [%d] afterGreetingSilence [%d] "
		"totalAnalysisTime [%d] minimumWordLength [%d] betweenWordsSilence [%d] maximumNumberOfWords [%d] silenceThreshold [%d] maximumWordLength [%d]\n",
				dfltInitialSilence, dfltGreeting, dfltAfterGreetingSilence, dfltTotalAnalysisTime,
				dfltMinimumWordLength, dfltBetweenWordsSilence, dfltMaximumNumberOfWords, dfltSilenceThreshold, dfltMaximumWordLength);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	if (load_config(0))
		return AST_MODULE_LOAD_DECLINE;
	if (ast_register_application(app, amd_exec, synopsis, descrip))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (load_config(1))
		return AST_MODULE_LOAD_DECLINE;
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Answering Machine Detection Application",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
