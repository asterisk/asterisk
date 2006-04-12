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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/speech.h"

static char *tdesc = "Dialplan Speech Applications";

LOCAL_USER_DECL;

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
"Once results are available the application returns and results (score and text) are available as dialplan variables.\n"
"The first text and score are ${TEXT0} AND ${SCORE0} while the second are ${TEXT1} and ${SCORE1}.\n"
"This may change in the future, however, to use a dialplan function instead of dialplan variables. Note it is possible to have more then one result.\n"
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

/*! \brief SpeechCreate() Dialplan Application */
static int speech_create(struct ast_channel *chan, void *data)
{
	struct localuser *u = NULL;
	struct ast_speech *speech = NULL;
	struct ast_datastore *datastore = NULL;

	LOCAL_USER_ADD(u);

	/* Request a speech object */
	speech = ast_speech_new(data, AST_FORMAT_SLINEAR);
	if (speech == NULL) {
		/* Not available */
		pbx_builtin_setvar_helper(chan, "ERROR", "1");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	datastore = ast_channel_datastore_alloc(&speech_datastore, NULL);
	if (datastore == NULL) {
		ast_speech_destroy(speech);
		pbx_builtin_setvar_helper(chan, "ERROR", "1");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	datastore->data = speech;
	ast_channel_datastore_add(chan, datastore);

	LOCAL_USER_REMOVE(u);

	return 0;
}

/*! \brief SpeechDeactivateGrammar(Grammar Name) Dialplan Application */
static int speech_deactivate(struct ast_channel *chan, void *data)
{
        int res = 0;
        struct localuser *u = NULL;
        struct ast_speech *speech = find_speech(chan);

        LOCAL_USER_ADD(u);

        if (speech == NULL) {
                LOCAL_USER_REMOVE(u);
                return -1;
        }

        /* Deactivate the grammar on the speech object */
        res = ast_speech_grammar_deactivate(speech, data);

        LOCAL_USER_REMOVE(u);

        return res;
}

/*! \brief SpeechActivateGrammar(Grammar Name) Dialplan Application */
static int speech_activate(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u = NULL;
	struct ast_speech *speech = find_speech(chan);

	LOCAL_USER_ADD(u);

	if (speech == NULL) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	/* Activate the grammar on the speech object */
	res = ast_speech_grammar_activate(speech, data);

	LOCAL_USER_REMOVE(u);

	return res;
}

/*! \brief SpeechStart() Dialplan Application */
static int speech_start(struct ast_channel *chan, void *data)
{
	int res = 0;
        struct localuser *u = NULL;
	struct ast_speech *speech = find_speech(chan);

	LOCAL_USER_ADD(u);

	if (speech == NULL) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	ast_speech_start(speech);

	LOCAL_USER_REMOVE(u);

	return res;
}

/*! \brief SpeechProcessingSound(Sound File) Dialplan Application */
static int speech_processing_sound(struct ast_channel *chan, void *data)
{
        int res = 0;
        struct localuser *u = NULL;
        struct ast_speech *speech = find_speech(chan);

        LOCAL_USER_ADD(u);

        if (speech == NULL) {
                LOCAL_USER_REMOVE(u);
                return -1;
        }

	if (speech->processing_sound != NULL) {
		free(speech->processing_sound);
		speech->processing_sound = NULL;
	}

	speech->processing_sound = strdup(data);

        LOCAL_USER_REMOVE(u);

        return res;
}

/*! \brief Helper function used by speech_background to playback a soundfile */
static int speech_streamfile(struct ast_channel *chan, const char *filename, const char *preflang)
{
        struct ast_filestream *fs;
        struct ast_filestream *vfs=NULL;

        fs = ast_openstream(chan, filename, preflang);
        if (fs)
                vfs = ast_openvstream(chan, filename, preflang);
        if (fs){
                if (ast_applystream(chan, fs))
                        return -1;
                if (vfs && ast_applystream(chan, vfs))
                        return -1;
                if (ast_playstream(fs))
                        return -1;
                if (vfs && ast_playstream(vfs))
                        return -1;
                return 0;
        }
        return -1;
}

/*! \brief SpeechBackground(Sound File|Timeout) Dialplan Application */
static int speech_background(struct ast_channel *chan, void *data)
{
	unsigned int timeout = 0;
	int res = 0, done = 0, concepts = 0, argc = 0, started = 0;
	struct localuser *u = NULL;
	struct ast_speech *speech = find_speech(chan);
	struct ast_speech_result *results = NULL, *result = NULL;
	struct ast_frame *f = NULL;
	int oldreadformat = AST_FORMAT_SLINEAR;
	char tmp[256] = "", tmp2[256] = "";
	char dtmf[AST_MAX_EXTENSION] = "";
	time_t start, current;
	struct ast_datastore *datastore = NULL;
	char *argv[2], *args = NULL, *filename = NULL;

	if (!(args = ast_strdupa(data)))
                return -1;

	LOCAL_USER_ADD(u);

	if (speech == NULL) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	/* Record old read format */
	oldreadformat = chan->readformat;

	/* Change read format to be signed linear */
	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR)) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	/* Parse out options */
	argc = ast_app_separate_args(args, '|', argv, sizeof(argv) / sizeof(argv[0]));
	if (argc > 0) {
		/* Yay sound file */
		filename = argv[0];
		if (argv[1] != NULL)
			timeout = atoi(argv[1]);
	}

	/* Start streaming the file if possible and specified */
	if (filename != NULL && ast_streamfile(chan, filename, chan->language)) {
		/* An error occured while streaming */
		ast_set_read_format(chan, oldreadformat);
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	/* Before we go into waiting for stuff... make sure the structure is ready, if not - start it again */
	if (speech->state == AST_SPEECH_STATE_NOT_READY || speech->state == AST_SPEECH_STATE_DONE) {
		speech->state = AST_SPEECH_STATE_NOT_READY;
		ast_speech_start(speech);
	}

	/* Okay it's streaming so go into a loop grabbing frames! */
	while (done == 0) {
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

		/* Do checks on speech structure to see if it's changed */
		ast_mutex_lock(&speech->lock);
		if (ast_test_flag(speech, AST_SPEECH_QUIET) && chan->stream != NULL) {
			ast_stopstream(chan);
			ast_clear_flag(speech, AST_SPEECH_QUIET);
		}
		/* Check state so we can see what to do */
		switch (speech->state) {
		case AST_SPEECH_STATE_READY:
			/* If audio playback has stopped do a check for timeout purposes */
			if (chan->streamid == -1 && chan->timingfunc == NULL)
				ast_stopstream(chan);
			if (chan->stream == NULL && timeout > 0) {
				/* If start time is not yet done... do it */
				if (started == 0) {
					time(&start);
					started = 1;
				} else {
					time(&current);
					if ((current-start) >= timeout) {
						pbx_builtin_setvar_helper(chan, "SILENCE", "1");
						done = 1;
						break;
					}
				}
			}
			/* Deal with audio frames if present */
			if (f != NULL && f->frametype == AST_FRAME_VOICE) {
				ast_speech_write(speech, f->data, f->datalen);
			}
			break;
		case AST_SPEECH_STATE_WAIT:
			/* Cue up waiting sound if not already playing */
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
			break;
		case AST_SPEECH_STATE_DONE:
			/* Assume there will be no results by default */
			pbx_builtin_setvar_helper(chan, "RESULTS", "0");
			pbx_builtin_setvar_helper(chan, "SILENCE", "0");
			/* Decoding is done and over... see if we have results */
			results = ast_speech_results_get(speech);
			if (results != NULL) {
				for (result=results; result!=NULL; result=result->next) {
					/* Text */
					snprintf(tmp, sizeof(tmp), "TEXT%d", concepts);
					pbx_builtin_setvar_helper(chan, tmp, result->text);
					/* Now... score! */
					snprintf(tmp, sizeof(tmp), "SCORE%d", concepts);
					snprintf(tmp2, sizeof(tmp2), "%d", result->score);
					pbx_builtin_setvar_helper(chan, tmp, tmp2);
					concepts++;
				}
				/* Expose number of results to dialplan */
				snprintf(tmp, sizeof(tmp), "%d", concepts);
				pbx_builtin_setvar_helper(chan, "RESULTS", tmp);
				/* Destroy the results since they are now in the dialplan */
				ast_speech_results_free(results);
			}
			/* Now that we are done... let's switch back to not ready state */
			speech->state = AST_SPEECH_STATE_NOT_READY;
			/* Break out of our background too */
			done = 1;
			/* Stop audio playback */
			if (chan->stream != NULL) {
				ast_stopstream(chan);
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
				if (f->subclass == '#') {
					/* Input is done, throw it into the dialplan */
					pbx_builtin_setvar_helper(chan, "RESULTS", "1");
					pbx_builtin_setvar_helper(chan, "SCORE0", "1000");
					pbx_builtin_setvar_helper(chan, "TEXT0", dtmf);
					done = 1;
				} else {
					if (chan->stream != NULL) {
						ast_stopstream(chan);
					}
					/* Start timeout if not already started */
					if (strlen(dtmf) == 0) {
						time(&start);
					}
					/* Append to the current information */
					snprintf(tmp, sizeof(tmp), "%c", f->subclass);
					strncat(dtmf, tmp, sizeof(dtmf));
				}
				break;
			case AST_FRAME_CONTROL:
				ast_log(LOG_NOTICE, "Have a control frame of subclass %d\n", f->subclass);
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

	LOCAL_USER_REMOVE(u);

	return 0;
}

/*! \brief SpeechDestroy() Dialplan Application */
static int speech_destroy(struct ast_channel *chan, void *data)
{
	int res = 0;
        struct localuser *u = NULL;
	struct ast_speech *speech = find_speech(chan);
	struct ast_datastore *datastore = NULL;

	LOCAL_USER_ADD(u);

	if (speech == NULL) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	/* Destroy speech structure */
	ast_speech_destroy(speech);

	datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
	if (datastore != NULL) {
		ast_channel_datastore_remove(chan, datastore);
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

int unload_module(void)
{
	int res = 0;

	res = ast_unregister_application("SpeechCreate");
	res |= ast_unregister_application("SpeechActivateGrammar");
        res |= ast_unregister_application("SpeechDeactivateGrammar");
	res |= ast_unregister_application("SpeechStart");
	res |= ast_unregister_application("SpeechBackground");
	res |= ast_unregister_application("SpeechDestroy");

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

int load_module(void)
{
	int res = 0;

	res = ast_register_application("SpeechCreate", speech_create, "Create a Speech Structure", speechcreate_descrip);
	res |= ast_register_application("SpeechActivateGrammar", speech_activate, "Activate a Grammar", speechactivategrammar_descrip);
        res |= ast_register_application("SpeechDeactivateGrammar", speech_deactivate, "Deactivate a Grammar", speechdeactivategrammar_descrip);
	res |= ast_register_application("SpeechStart", speech_start, "Start recognizing", speechstart_descrip);
	res |= ast_register_application("SpeechBackground", speech_background, "Play a sound file and wait for speech to be recognized", speechbackground_descrip);
	res |= ast_register_application("SpeechDestroy", speech_destroy, "End speech recognition", speechdestroy_descrip);
	res |= ast_register_application("SpeechProcessingSound", speech_processing_sound, "Change background processing sound", speechprocessingsound_descrip);
	
	return res;
}

int reload(void)
{
	return 0;
}

const char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
