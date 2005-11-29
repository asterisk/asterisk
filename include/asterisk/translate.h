/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Translate via the use of pseudo channels
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_TRANSLATE_H
#define _ASTERISK_TRANSLATE_H

#define MAX_FORMAT 32

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/frame.h"
#include "asterisk/plc.h"

/* Declared by individual translators */
struct ast_translator_pvt;

/*! data structure associated with a translator */
struct ast_translator {
	/*! Name of translator */
	char name[80];
	/*! Source format */
	int srcfmt;
	/*! Destination format */
	int dstfmt;
	/*! Private data associated with the translator */
	struct ast_translator_pvt *(*newpvt)(void);
	/*! Input frame callback */
	int (*framein)(struct ast_translator_pvt *pvt, struct ast_frame *in);
	/*! Output frame callback */
	struct ast_frame * (*frameout)(struct ast_translator_pvt *pvt);
	/*! Destroy translator callback */
	void (*destroy)(struct ast_translator_pvt *pvt);
	/* For performance measurements */
	/*! Generate an example frame */
	struct ast_frame * (*sample)(void);
	/*! Cost in milliseconds for encoding/decoding 1 second of sound */
	int cost;
	/*! For linking, not to be modified by the translator */
	struct ast_translator *next;
};

struct ast_trans_pvt;

/*! Register a translator */
/*! 
 * \param t populated ast_translator structure
 * This registers a codec translator with asterisk
 * Returns 0 on success, -1 on failure
 */
extern int ast_register_translator(struct ast_translator *t);

/*! Unregister a translator */
/*!
 * \param t translator to unregister
 * Unregisters the given tranlator
 * Returns 0 on success, -1 on failure
 */
extern int ast_unregister_translator(struct ast_translator *t);

/*! Chooses the best translation path */
/*! 
 * Given a list of sources, and a designed destination format, which should
   I choose? Returns 0 on success, -1 if no path could be found.  Modifies
   dests and srcs in place 
   */
extern int ast_translator_best_choice(int *dsts, int *srcs);

/*!Builds a translator path */
/*! 
 * \param dest destination format
 * \param source source format
 * Build a path (possibly NULL) from source to dest 
 * Returns ast_trans_pvt on success, NULL on failure
 * */
extern struct ast_trans_pvt *ast_translator_build_path(int dest, int source);

/*! Frees a translator path */
/*!
 * \param tr translator path to get rid of
 * Frees the given translator path structure
 */
extern void ast_translator_free_path(struct ast_trans_pvt *tr);

/*! translates one or more frames */
/*! 
 * \param tr translator structure to use for translation
 * \param f frame to translate
 * \param consume Whether or not to free the original frame
 * Apply an input frame into the translator and receive zero or one output frames.  Consume
 * determines whether the original frame should be freed
 * Returns an ast_frame of the new translation format on success, NULL on failure
 */
extern struct ast_frame *ast_translate(struct ast_trans_pvt *tr, struct ast_frame *f, int consume);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
