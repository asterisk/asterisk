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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/speech.h"

/* Descriptions for each application */
static char *speechcreate_descrip =
"SpeechCreate(engine name)\n"
"This application creates information to be used by all the other applications. It must be called before doing any speech recognition activities such as activating a grammar.\n"
"It takes the engine name to use as the argument, if not specified the default engine will be used.\n";

static char *speechactivategrammar_descrip =
"SpeechActivateGrammar(Grammar Name)\n"
"This activates the specified grammar to be recognized by the engine. A grammar tells the speech recognition engine what to recognize, \n"
	"and how to portray it back to you in the dialplan. The grammar name is the only argument to this application.\n";

static char *speechstart_descrip =
"SpeechStart()\n"
	"Tell the speech recognition engine that it should start trying to get results from audio being fed to it. This has no arguments.\n";

static char *speechbackground_descrip =
"SpeechBackground(Sound File|Timeout)\n"
"This application plays a sound file and waits for the person to speak. Once they start speaking playback of the file stops, and silence is heard.\n"
"Once they stop talking the processing sound is played to indicate the speech recognition engine is working.\n"
"Once results are available the application returns and results (score and text) are available using dialplan functions.\n"
"The first text and score are ${SPEECH_TEXT(0)} AND ${SPEECH_SCORE(0)} while the second are ${SPEECH_TEXT(1)} and ${SPEECH_SCORE(1)}.\n"
"The first argument is the sound file and the second is the timeout. Note the timeout will only start once the sound file has stopped playing.\n";

static char *speechdeactivategrammar_descrip =
"SpeechDeactivateGrammar(Grammar Name)\n"
	"This deactivates the specified grammar so that it is no longer recognized. The only argument is the grammar name to deactivate.\n";

static char *speechprocessingsound_descrip =
"SpeechProcessingSound(Sound File)\n"
"This changes the processing sound that SpeechBackground plays back when the speech recognition engine is processing and working to get results.\n"
	"It takes the sound file as the only argument.\n";

static char *speechdestroy_descrip =
"SpeechDestroy()\n"
"This destroys the information used by all the other speech recognition applications.\n"
"If you call this application but end up wanting to recognize more speech, you must call SpeechCreate\n"
	"again before calling any other application. It takes no arguments.\n";

static char *speechload_descrip =
"SpeechLoadGrammar(Grammar Name|Path)\n"
"Load a grammar only on the channel, not globally.\n"
"It takes the grammar name as first argument and path as second.\n";

static char *speechunload_descrip =
"SpeechUnloadGrammar(Grammar Name)\n"
"Unload a grammar. It takes the grammar name as the only argument.\n";

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
	
	datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
	if (datastore == NULL) {
		return NULL;
	}
	speech = datastore->data;

	return speech;
}

/* Helper function to find a specific speech recognition result by number and nbest alternative */
static struct ast_speech_result *find_result(struct ast_speech_result *results, char *result_num)
{
	struct ast_speech_result *result = results;
	char *tmp = NULL;
	int nbest_num = 0, wanted_num = 0, i = 0;

	if (!result)
		return NULL;

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
	} while ((result = result->next));

	return result;
}

/*! \brief SPEECH_SCORE() Dialplan Function */
static int speech_score(struct ast_channel *chan, char *cmd, char *data,
		       char *buf, size_t len)
{
	struct ast_speech_result *result = NULL;
	struct ast_speech *speech = find_speech(chan);
	char tmp[128] = "";

	if (data == NULL || speech == NULL || !(result = find_result(speech->results, data)))
		return -1;
	
	snprintf(tmp, sizeof(tmp), "%d", result->score);
	
	ast_copy_string(buf, tmp, len);

	return 0;
}

static struct ast_custom_function speech_score_function = {
        .name = "SPEECH_SCORE",
        .synopsis = "Gets the confidence score of a result.",
        .syntax = "SPEECH_SCORE([nbest number/]result number)",
        .desc =
        "Gets the confidence score of a result.\n",
        .read = speech_score,
        .write = NULL,
};

/*! \brief SPEECH_TEXT() Dialplan Function */
static int speech_text(struct ast_channel *chan, char *cmd, char *data,
			char *buf, size_t len)
{
        struct ast_speech_result *result = NULL;
        struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL || !(result = find_result(speech->results, data)))
                return -1;

	if (result->text != NULL) {
		ast_copy_string(buf, result->text, len);
	} else {
		buf[0] = '\0';
	}

        return 0;
}

static struct ast_custom_function speech_text_function = {
        .name = "SPEECH_TEXT",
        .synopsis = "Gets the recognized text of a result.",
        .syntax = "SPEECH_TEXT([nbest number/]result number)",
        .desc =
        "Gets the recognized text of a result.\n",
        .read = speech_text,
        .write = NULL,
};

/*! \brief SPEECH_GRAMMAR() Dialplan Function */
static int speech_grammar(struct ast_channel *chan, char *cmd, char *data,
			char *buf, size_t len)
{
        struct ast_speech_result *result = NULL;
        struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL || !(result = find_result(speech->results, data)))
                return -1;

	if (result->grammar != NULL) {
		ast_copy_string(buf, result->grammar, len);
	} else {
		buf[0] = '\0';
	}

        return 0;
}

static struct ast_custom_function speech_grammar_function = {
        .name = "SPEECH_GRAMMAR",
        .synopsis = "Gets the matched grammar of a result if available.",
        .syntax = "SPEECH_GRAMMAR([nbest number/]result number)",
        .desc =
        "Gets the matched grammar of a result if available.\n",
        .read = speech_grammar,
        .write = NULL,
};

/*! \brief SPEECH_ENGINE() Dialplan Function */
static int speech_engine_write(struct ast_channel *chan, char *cmd, char *data, const char *value)
{
	struct ast_speech *speech = find_speech(chan);

	if (data == NULL || speech == NULL)
		return -1;

	ast_speech_change(speech, data, value);

	return 0;
}

static struct ast_custom_function speech_engine_function = {
	.name = "SPEECH_ENGINE",
	.synopsis = "Change a speech engine specific attribute.",
	.syntax = "SPEECH_ENGINE(name)=value",
	.desc =
	"Changes a speech engine specific attribute.\n",
	.read = NULL,
	.write = speech_engine_write,
};

/*! \brief SPEECH_RESULTS_TYPE() Dialplan Function */
static int speech_results_type_write(struct ast_channel *chan, char *cmd, char *data, const char *value)
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
	.synopsis = "Sets the type of results that will be returned.",
	.syntax = "SPEECH_RESULTS_TYPE()=results type",
	.desc =
	"Sets the type of results that will be returned. Valid options are normal or nbest.",
	.read = NULL,
	.write = speech_results_type_write,
};

/*! \brief SPEECH() Dialplan Function */
static int speech_read(struct ast_channel *chan, char *cmd, char *data,
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
		result = speech->results;
		while (result) {
			results++;
			result = result->next;
		}
		snprintf(tmp, sizeof(tmp), "%d", results);
		ast_copy_string(buf, tmp, len);
	} else {
		buf[0] = '\0';
	}

	return 0;
}

static struct ast_custom_function speech_function = {
        .name = "SPEECH",
        .synopsis = "Gets information about speech recognition results.",
        .syntax = "SPEECH(argument)",
        .desc =
	"Gets information about speech recognition results.\n"
	"status:   Returns 1 upon speech object existing, or 0 if not\n"
	"spoke:  Returns 1 if spoker spoke, or 0 if not\n"
	"results:  Returns number of results that were recognized\n",
        .read = speech_read,
        .write = NULL,
};



/*! \brief SpeechCreate() Dialplan Application */
static int speech_create(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;
	struct ast_speech *speech = NULL;
	struct ast_datastore *datastore = NULL;

	u = ast_module_user_add(chan);

	/* Request a speech object */
	speech = ast_speech_new(data, AST_FORMAT_SLINEAR);
	if (speech == NULL) {
		/* Not available */
		pbx_builtin_setvar_helper(chan, "ERROR", "1");
		ast_module_user_remove(u);
		return 0;
	}

	datastore = ast_channel_datastore_alloc(&speech_datastore, NULL);
	if (datastore == NULL) {
		ast_speech_destroy(speech);
		pbx_builtin_setvar_helper(chan, "ERROR", "1");
		ast_module_user_remove(u);
		return 0;
	}
	datastore->data = speech;
	ast_channel_datastore_add(chan, datastore);

	ast_module_user_remove(u);

	return 0;
}

/*! \brief SpeechLoadGrammar(Grammar Name|Path) Dialplan Application */
static int speech_load(struct ast_channel *chan, void *data)
{
	int res = 0, argc = 0;
	struct ast_module_user *u = NULL;
	struct ast_speech *speech = find_speech(chan);
	char *argv[2], *args = NULL, *name = NULL, *path = NULL;

	args = ast_strdupa(data);

	u = ast_module_user_add(chan);

	if (speech == NULL) {
		ast_module_user_remove(u);
                return -1;
        }

	/* Parse out arguments */
	argc = ast_app_separate_args(args, '|', argv, sizeof(argv) / sizeof(argv[0]));
	if (argc != 2) {
		ast_module_user_remove(u);
		return -1;
	}
	name = argv[0];
	path = argv[1];

        /* Load the grammar locally on the object */
        res = ast_speech_grammar_load(speech, name, path);

        ast_module_user_remove(u);

        return res;
}

/*! \brief SpeechUnloadGrammar(Grammar Name) Dialplan Application */
static int speech_unload(struct ast_channel *chan, void *data)
{
        int res = 0;
        struct ast_module_user *u = NULL;
        struct ast_speech *speech = find_speech(chan);

        u = ast_module_user_add(chan);

        if (speech == NULL) {
                ast_module_user_remove(u);
                return -1;
        }

        /* Unload the grammar */
        res = ast_speech_grammar_unload(speech, data);

        ast_module_user_remove(u);

        return res;
}

/*! \brief SpeechDeactivateGrammar(Grammar Name) Dialplan Application */
static int speech_deactivate(struct ast_channel *chan, void *data)
{
        int res = 0;
        struct ast_module_user *u = NULL;
        struct ast_speech *speech = find_speech(chan);

        u = ast_module_user_add(chan);

        if (speech == NULL) {
                ast_module_user_remove(u);
                return -1;
        }

        /* Deactivate the grammar on the speech object */
        res = ast_speech_grammar_deactivate(speech, data);

        ast_module_user_remove(u);

        return res;
}

/*! \brief SpeechActivateGrammar(Grammar Name) Dialplan Application */
static int speech_activate(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u = NULL;
	struct ast_speech *speech = find_speech(chan);

	u = ast_module_user_add(chan);

	if (speech == NULL) {
		ast_module_user_remove(u);
		return -1;
	}

	/* Activate the grammar on the speech object */
	res = ast_speech_grammar_activate(speech, data);

	ast_module_user_remove(u);

	return res;
}

/*! \brief SpeechStart() Dialplan Application */
static int speech_start(struct ast_channel *chan, void *data)
{
	int res = 0;
        struct ast_module_user *u = NULL;
	struct ast_speech *speech = find_speech(chan);

	u = ast_module_user_add(chan);

	if (speech == NULL) {
		ast_module_user_remove(u);
		return -1;
	}

	ast_speech_start(speech);

	ast_module_user_remove(u);

	return res;
}

/*! \brief SpeechProcessingSound(Sound File) Dialplan Application */
static int speech_processing_sound(struct ast_channel *chan, void *data)
{
        int res = 0;
        struct ast_module_user *u = NULL;
        struct ast_speech *speech = find_speech(chan);

        u = ast_module_user_add(chan);

        if (speech == NULL) {
                ast_module_user_remove(u);
                return -1;
        }

	if (speech->processing_sound != NULL) {
		free(speech->processing_sound);
		speech->processing_sound = NULL;
	}

	speech->processing_sound = strdup(data);

        ast_module_user_remove(u);

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

/*! \brief SpeechBackground(Sound File|Timeout) Dialplan Application */
static int speech_background(struct ast_channel *chan, void *data)
{
        unsigned int timeout = 0;
        int res = 0, done = 0, argc = 0, started = 0, quieted = 0, max_dtmf_len = 0;
        struct ast_module_user *u = NULL;
        struct ast_speech *speech = find_speech(chan);
        struct ast_frame *f = NULL;
        int oldreadformat = AST_FORMAT_SLINEAR;
        char dtmf[AST_MAX_EXTENSION] = "";
        time_t start, current;
        struct ast_datastore *datastore = NULL;
        char *argv[2], *args = NULL, *filename_tmp = NULL, *filename = NULL, tmp[2] = "", dtmf_terminator = '#';
	const char *tmp2 = NULL;

        args = ast_strdupa(data);

        u = ast_module_user_add(chan);

        if (speech == NULL) {
                ast_module_user_remove(u);
                return -1;
        }

	/* If channel is not already answered, then answer it */
	if (chan->_state != AST_STATE_UP && ast_answer(chan)) {
		ast_module_user_remove(u);
		return -1;
	}

        /* Record old read format */
        oldreadformat = chan->readformat;

        /* Change read format to be signed linear */
        if (ast_set_read_format(chan, AST_FORMAT_SLINEAR)) {
                ast_module_user_remove(u);
                return -1;
        }

        /* Parse out options */
        argc = ast_app_separate_args(args, '|', argv, sizeof(argv) / sizeof(argv[0]));
        if (argc > 0) {
                /* Yay sound file */
                filename_tmp = ast_strdupa(argv[0]);
		if (!ast_strlen_zero(argv[1])) {
			if ((timeout = atoi(argv[1])) == 0)
				timeout = -1;
		} else
			timeout = 0;
        }

	/* See if the maximum DTMF length variable is set... we use a variable in case they want to carry it through their entire dialplan */
	if ((tmp2 = pbx_builtin_getvar_helper(chan, "SPEECH_DTMF_MAXLEN")) && !ast_strlen_zero(tmp2))
		max_dtmf_len = atoi(tmp2);

	/* See if a terminator is specified */
	if ((tmp2 = pbx_builtin_getvar_helper(chan, "SPEECH_DTMF_TERMINATOR"))) {
		if (ast_strlen_zero(tmp2))
			dtmf_terminator = '\0';
		else
			dtmf_terminator = tmp2[0];
	}

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
		if (!quieted && (chan->streamid == -1 && chan->timingfunc == NULL) && (filename = strsep(&filename_tmp, "&"))) {
			/* Discard old stream information */
			ast_stopstream(chan);
			/* Start new stream */
			speech_streamfile(chan, filename, chan->language);
		}

                /* Run scheduled stuff */
                ast_sched_runq(chan->sched);

                /* Yay scheduling */
                res = ast_sched_wait(chan->sched);
                if (res < 0) {
                        res = 1000;
                }

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
			time(&current);
			if ((current-start) >= timeout) {
				done = 1;
				if (f)
					ast_frfree(f);
				break;
			}
		}

                /* Do checks on speech structure to see if it's changed */
                ast_mutex_lock(&speech->lock);
                if (ast_test_flag(speech, AST_SPEECH_QUIET)) {
			if (chan->stream)
				ast_stopstream(chan);
			ast_clear_flag(speech, AST_SPEECH_QUIET);
			quieted = 1;
                }
                /* Check state so we can see what to do */
                switch (speech->state) {
                case AST_SPEECH_STATE_READY:
                        /* If audio playback has stopped do a check for timeout purposes */
                        if (chan->streamid == -1 && chan->timingfunc == NULL)
                                ast_stopstream(chan);
                        if (!quieted && chan->stream == NULL && timeout && started == 0 && !filename_tmp) {
				if (timeout == -1) {
					done = 1;
					if (f)
						ast_frfree(f);
					break;
				}
				time(&start);
				started = 1;
                        }
                        /* Write audio frame out to speech engine if no DTMF has been received */
                        if (!strlen(dtmf) && f != NULL && f->frametype == AST_FRAME_VOICE) {
                                ast_speech_write(speech, f->data, f->datalen);
                        }
                        break;
                case AST_SPEECH_STATE_WAIT:
                        /* Cue up waiting sound if not already playing */
			if (!strlen(dtmf)) {
				if (chan->stream == NULL) {
					if (speech->processing_sound != NULL) {
						if (strlen(speech->processing_sound) > 0 && strcasecmp(speech->processing_sound,"none")) {
							speech_streamfile(chan, speech->processing_sound, chan->language);
						}
					}
				} else if (chan->streamid == -1 && chan->timingfunc == NULL) {
					ast_stopstream(chan);
					if (speech->processing_sound != NULL) {
						if (strlen(speech->processing_sound) > 0 && strcasecmp(speech->processing_sound,"none")) {
							speech_streamfile(chan, speech->processing_sound, chan->language);
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
				if (chan->stream != NULL) {
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
				if (dtmf_terminator != '\0' && f->subclass == dtmf_terminator) {
					done = 1;
				} else {
					if (chan->stream != NULL) {
						ast_stopstream(chan);
					}
					if (!started) {
						/* Change timeout to be 5 seconds for DTMF input */
						timeout = (chan->pbx && chan->pbx->dtimeout) ? chan->pbx->dtimeout : 5;
						started = 1;
					}
					time(&start);
					snprintf(tmp, sizeof(tmp), "%c", f->subclass);
					strncat(dtmf, tmp, sizeof(dtmf) - strlen(dtmf) - 1);
					/* If the maximum length of the DTMF has been reached, stop now */
					if (max_dtmf_len && strlen(dtmf) == max_dtmf_len)
						done = 1;
				}
                                break;
                        case AST_FRAME_CONTROL:
                                switch (f->subclass) {
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

	if (strlen(dtmf)) {
		/* We sort of make a results entry */
		speech->results = ast_calloc(1, sizeof(*speech->results));
		if (speech->results != NULL) {
			ast_speech_dtmf(speech, dtmf);
			speech->results->score = 1000;
			speech->results->text = strdup(dtmf);
			speech->results->grammar = strdup("dtmf");
		}
		ast_speech_change_state(speech, AST_SPEECH_STATE_NOT_READY);
	}

        /* See if it was because they hung up */
        if (done == 3) {
                /* Destroy speech structure */
                ast_speech_destroy(speech);
                datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
                if (datastore != NULL) {
                        ast_channel_datastore_remove(chan, datastore);
                }
        } else {
                /* Channel is okay so restore read format */
                ast_set_read_format(chan, oldreadformat);
        }

        ast_module_user_remove(u);

        return 0;
}


/*! \brief SpeechDestroy() Dialplan Application */
static int speech_destroy(struct ast_channel *chan, void *data)
{
	int res = 0;
        struct ast_module_user *u = NULL;
	struct ast_speech *speech = find_speech(chan);
	struct ast_datastore *datastore = NULL;

	u = ast_module_user_add(chan);

	if (speech == NULL) {
		ast_module_user_remove(u);
		return -1;
	}

	/* Destroy speech structure */
	ast_speech_destroy(speech);

	datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
	if (datastore != NULL) {
		ast_channel_datastore_remove(chan, datastore);
	}

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res = 0;

	res = ast_unregister_application("SpeechCreate");
	res |= ast_unregister_application("SpeechLoadGrammar");
	res |= ast_unregister_application("SpeechUnloadGrammar");
	res |= ast_unregister_application("SpeechActivateGrammar");
        res |= ast_unregister_application("SpeechDeactivateGrammar");
	res |= ast_unregister_application("SpeechStart");
	res |= ast_unregister_application("SpeechBackground");
	res |= ast_unregister_application("SpeechDestroy");
	res |= ast_unregister_application("SpeechProcessingSound");
	res |= ast_custom_function_unregister(&speech_function);
	res |= ast_custom_function_unregister(&speech_score_function);
	res |= ast_custom_function_unregister(&speech_text_function);
	res |= ast_custom_function_unregister(&speech_grammar_function);
	res |= ast_custom_function_unregister(&speech_engine_function);
	res |= ast_custom_function_unregister(&speech_results_type_function);

	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	int res = 0;

	res = ast_register_application("SpeechCreate", speech_create, "Create a Speech Structure", speechcreate_descrip);
	res |= ast_register_application("SpeechLoadGrammar", speech_load, "Load a Grammar", speechload_descrip);
	res |= ast_register_application("SpeechUnloadGrammar", speech_unload, "Unload a Grammar", speechunload_descrip);
	res |= ast_register_application("SpeechActivateGrammar", speech_activate, "Activate a Grammar", speechactivategrammar_descrip);
        res |= ast_register_application("SpeechDeactivateGrammar", speech_deactivate, "Deactivate a Grammar", speechdeactivategrammar_descrip);
	res |= ast_register_application("SpeechStart", speech_start, "Start recognizing voice in the audio stream", speechstart_descrip);
	res |= ast_register_application("SpeechBackground", speech_background, "Play a sound file and wait for speech to be recognized", speechbackground_descrip);
	res |= ast_register_application("SpeechDestroy", speech_destroy, "End speech recognition", speechdestroy_descrip);
	res |= ast_register_application("SpeechProcessingSound", speech_processing_sound, "Change background processing sound", speechprocessingsound_descrip);
	res |= ast_custom_function_register(&speech_function);
	res |= ast_custom_function_register(&speech_score_function);
	res |= ast_custom_function_register(&speech_text_function);
	res |= ast_custom_function_register(&speech_grammar_function);
	res |= ast_custom_function_register(&speech_engine_function);
	res |= ast_custom_function_register(&speech_results_type_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialplan Speech Applications");
