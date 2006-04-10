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
 * \brief Generic Speech Recognition API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/options.h"
#include "asterisk/speech.h"

static char *tdesc = "Generic Speech Recognition API";

static AST_LIST_HEAD_STATIC(engines, ast_speech_engine);
static struct ast_speech_engine *default_engine = NULL;

/*! \brief Find a speech recognition engine of specified name, if NULL then use the default one */
static struct ast_speech_engine *find_engine(char *engine_name)
{
	struct ast_speech_engine *engine = NULL;

	/* If no name is specified -- use the default engine */
	if (engine_name == NULL || strlen(engine_name) == 0) {
		return default_engine;
	}

	AST_LIST_LOCK(&engines);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&engines, engine, list) {
		if (!strcasecmp(engine->name, engine_name)) {
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&engines);

	return engine;
}

/*! \brief Activate a loaded (either local or global) grammar */
int ast_speech_grammar_activate(struct ast_speech *speech, char *grammar_name)
{
	int res = 0;

	if (speech->engine->activate != NULL) {
		res = speech->engine->activate(speech, grammar_name);
	}

	return res;
}

/*! \brief Deactivate a loaded grammar on a speech structure */
int ast_speech_grammar_deactivate(struct ast_speech *speech, char *grammar_name)
{
	int res = 0;

        if (speech->engine->deactivate != NULL) {
                res = speech->engine->deactivate(speech, grammar_name);
        }

	return res;
}

/*! \brief Load a local grammar on a speech structure */
int ast_speech_grammar_load(struct ast_speech *speech, char *grammar_name, char *grammar)
{
	int res = 0;

	if (speech->engine->load != NULL) {
		res = speech->engine->load(speech, grammar_name, grammar);
	}

	return res;
}

/*! \brief Unload a local grammar from a speech structure */
int ast_speech_grammar_unload(struct ast_speech *speech, char *grammar_name)
{
        int res = 0;

        if (speech->engine->unload != NULL) {
                res = speech->engine->unload(speech, grammar_name);
        }

        return res;
}

/*! \brief Return the results of a recognition from the speech structure */
struct ast_speech_result *ast_speech_results_get(struct ast_speech *speech)
{
	struct ast_speech_result *result = NULL;

	if (speech->engine->get != NULL) {
		result = speech->engine->get(speech);
	}

	return result;
}

/*! \brief Free a list of results */
int ast_speech_results_free(struct ast_speech_result *result)
{
	struct ast_speech_result *current_result = result, *prev_result = NULL;
	int res = 0;

	while (current_result != NULL) {
		prev_result = current_result;
		/* Deallocate what we can */
		if (current_result->text != NULL) {
			free(current_result->text);
			current_result->text = NULL;
		}
		/* Move on and then free ourselves */
		current_result = current_result->next;
		free(prev_result);
		prev_result = NULL;
	}

	return res;
}

/*! \brief Start speech recognition on a speech structure */
void ast_speech_start(struct ast_speech *speech)
{
	/* If the engine needs to start stuff up, do it */
	if (speech->engine->start != NULL) {
		speech->engine->start(speech);
	}

	return;
}

/*! \brief Write in signed linear audio to be recognized */
int ast_speech_write(struct ast_speech *speech, void *data, int len)
{
	int res = 0;

	/* Make sure the speech engine is ready to accept audio */
	if (speech->state != AST_SPEECH_STATE_READY) {
		return -1;
	}

	if (speech->engine->write != NULL) {
		speech->engine->write(speech, data, len);
	}

	return res;
}

/*! \brief Create a new speech structure using the engine specified */
struct ast_speech *ast_speech_new(char *engine_name, int format)
{
	struct ast_speech_engine *engine = NULL;
	struct ast_speech *new_speech = NULL;

	/* Try to find the speech recognition engine that was requested */
	engine = find_engine(engine_name);
	if (engine == NULL) {
		/* Invalid engine or no engine available */
		return NULL;
	}

	/* Allocate our own speech structure, and try to allocate a structure from the engine too */
	new_speech = ast_calloc(1, sizeof(*new_speech));
	if (new_speech == NULL) {
		/* Ran out of memory while trying to allocate some for a speech structure */
		return NULL;
	}

	/* Initialize the lock */
	ast_mutex_init(&new_speech->lock);

	/* Copy over our engine pointer */
	new_speech->engine = engine;

	/* We are not ready to accept audio yet */
	ast_speech_change_state(new_speech, AST_SPEECH_STATE_NOT_READY);

	/* Pass ourselves to the engine so they can set us up some more */
	engine->new(new_speech);

	return new_speech;
}

/*! \brief Destroy a speech structure */
int ast_speech_destroy(struct ast_speech *speech)
{
	int res = 0;

	/* Call our engine so we are destroyed properly */
	speech->engine->destroy(speech);

	/* Deinitialize the lock */
	ast_mutex_destroy(&speech->lock);

	/* If a processing sound is set - free the memory used by it */
	if (speech->processing_sound != NULL) {
		free(speech->processing_sound);
		speech->processing_sound = NULL;
	}

	/* Aloha we are done */
	free(speech);
	speech = NULL;

	return res;
}

/*! \brief Change state of a speech structure */
int ast_speech_change_state(struct ast_speech *speech, int state)
{
	int res = 0;

	speech->state = state;

	return res;
}

/*! \brief Register a speech recognition engine */
int ast_speech_register(struct ast_speech_engine *engine)
{
	struct ast_speech_engine *existing_engine = NULL;
	int res = 0;

	existing_engine = find_engine(engine->name);
	if (existing_engine != NULL) {
		/* Engine already loaded */
		return -1;
	}

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered speech recognition engine '%s'\n", engine->name);

	/* Add to the engine linked list and make default if needed */
	AST_LIST_LOCK(&engines);
	AST_LIST_INSERT_HEAD(&engines, engine, list);
	if (default_engine == NULL) {
		default_engine = engine;
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Made '%s' the default speech recognition engine\n", engine->name);
	}
	AST_LIST_UNLOCK(&engines);

	return res;
}

/*! \brief Unregister a speech recognition engine */
int ast_speech_unregister(char *engine_name)
{
	struct ast_speech_engine *engine = NULL;
	int res = -1;

	if (engine_name == NULL) {
		return res;
	}

	AST_LIST_LOCK(&engines);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&engines, engine, list) {
		if (!strcasecmp(engine->name, engine_name)) {
			/* We have our engine... removed it */
			AST_LIST_REMOVE_CURRENT(&engines, list);
			/* If this was the default engine, we need to pick a new one */
			if (default_engine == engine) {
				default_engine = AST_LIST_FIRST(&engines);
			}
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Unregistered speech recognition engine '%s'\n", engine_name);
			/* All went well */
			res = 0;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&engines);

	return res;
}

int unload_module(void)
{
	/* We can not be unloaded */
	return -1;
}

int load_module(void)
{
	int res = 0;

	/* Initialize our list of engines */
	AST_LIST_HEAD_INIT_NOLOCK(&engines);

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
	int res = 0;

	return res;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
