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
 * \brief Generic Speech Recognition API
 */

#ifndef _ASTERISK_SPEECH_H
#define _ASTERISK_SPEECH_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Speech structure flags */
enum ast_speech_flags {
	AST_SPEECH_QUIET = (1 << 0),        /* Quiet down output... they are talking */
	AST_SPEECH_SPOKE = (1 << 1),        /* Speaker spoke! */
	AST_SPEECH_HAVE_RESULTS = (1 << 2), /* Results are present */
};

/* Speech structure states - in order of expected change */
enum ast_speech_states {
	AST_SPEECH_STATE_NOT_READY = 0, /* Not ready to accept audio */
	AST_SPEECH_STATE_READY, /* Accepting audio */
	AST_SPEECH_STATE_WAIT, /* Wait for results to become available */
	AST_SPEECH_STATE_DONE, /* Processing is all done */
};

enum ast_speech_results_type {
	AST_SPEECH_RESULTS_TYPE_NORMAL = 0,
	AST_SPEECH_RESULTS_TYPE_NBEST,
};

/* Speech structure */
struct ast_speech {
	/*! Structure lock */
	ast_mutex_t lock;
	/*! Set flags */
	unsigned int flags;
	/*! Processing sound (used when engine is processing audio and getting results) */
	char *processing_sound;
	/*! Current state of structure */
	int state;
	/*! Expected write format */
	int format;
	/*! Data for speech engine */
	void *data;
	/*! Cached results */
	struct ast_speech_result *results;
	/*! Type of results we want */
	enum ast_speech_results_type results_type;
	/*! Pointer to the engine used by this speech structure */
	struct ast_speech_engine *engine;
};
  
/* Speech recognition engine structure */
struct ast_speech_engine {
	/*! Name of speech engine */
	char *name;
	/*! Set up the speech structure within the engine */
	int (*create)(struct ast_speech *speech, int format);
	/*! Destroy any data set on the speech structure by the engine */
	int (*destroy)(struct ast_speech *speech);
	/*! Load a local grammar on the speech structure */
	int (*load)(struct ast_speech *speech, char *grammar_name, char *grammar);
	/*! Unload a local grammar */
	int (*unload)(struct ast_speech *speech, char *grammar_name);
	/*! Activate a loaded grammar */
	int (*activate)(struct ast_speech *speech, char *grammar_name);
	/*! Deactivate a loaded grammar */
	int (*deactivate)(struct ast_speech *speech, char *grammar_name);
	/*! Write audio to the speech engine */
	int (*write)(struct ast_speech *speech, void *data, int len);
	/*! Signal DTMF was received */
	int (*dtmf)(struct ast_speech *speech, const char *dtmf);
	/*! Prepare engine to accept audio */
	int (*start)(struct ast_speech *speech);
	/*! Change an engine specific setting */
	int (*change)(struct ast_speech *speech, char *name, const char *value);
	/*! Change the type of results we want back */
	int (*change_results_type)(struct ast_speech *speech, enum ast_speech_results_type results_type);
	/*! Try to get results */
	struct ast_speech_result *(*get)(struct ast_speech *speech);
	/*! Accepted formats by the engine */
	int formats;
	AST_LIST_ENTRY(ast_speech_engine) list;
};

/* Result structure */
struct ast_speech_result {
	/*! Recognized text */
	char *text;
	/*! Result score */
	int score;
	/*! NBest Alternative number if in NBest results type */
	int nbest_num;
	/*! Matched grammar */
	char *grammar;
	/*! List information */
	AST_LIST_ENTRY(ast_speech_result) list;
};

/*! \brief Activate a grammar on a speech structure */
int ast_speech_grammar_activate(struct ast_speech *speech, char *grammar_name);
/*! \brief Deactivate a grammar on a speech structure */
int ast_speech_grammar_deactivate(struct ast_speech *speech, char *grammar_name);
/*! \brief Load a grammar on a speech structure (not globally) */
int ast_speech_grammar_load(struct ast_speech *speech, char *grammar_name, char *grammar);
/*! \brief Unload a grammar */
int ast_speech_grammar_unload(struct ast_speech *speech, char *grammar_name);
/*! \brief Get speech recognition results */
struct ast_speech_result *ast_speech_results_get(struct ast_speech *speech);
/*! \brief Free a set of results */
int ast_speech_results_free(struct ast_speech_result *result);
/*! \brief Indicate to the speech engine that audio is now going to start being written */
void ast_speech_start(struct ast_speech *speech);
/*! \brief Create a new speech structure */
struct ast_speech *ast_speech_new(char *engine_name, int formats);
/*! \brief Destroy a speech structure */
int ast_speech_destroy(struct ast_speech *speech);
/*! \brief Write audio to the speech engine */
int ast_speech_write(struct ast_speech *speech, void *data, int len);
/*! \brief Signal to the engine that DTMF was received */
int ast_speech_dtmf(struct ast_speech *speech, const char *dtmf);
/*! \brief Change an engine specific attribute */
int ast_speech_change(struct ast_speech *speech, char *name, const char *value);
/*! \brief Change the type of results we want */
int ast_speech_change_results_type(struct ast_speech *speech, enum ast_speech_results_type results_type);
/*! \brief Change state of a speech structure */
int ast_speech_change_state(struct ast_speech *speech, int state);
/*! \brief Register a speech recognition engine */
int ast_speech_register(struct ast_speech_engine *engine);
/*! \brief Unregister a speech recognition engine */
int ast_speech_unregister(char *engine_name);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_SPEECH_H */
