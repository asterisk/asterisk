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

#include <asterisk/frame.h>

/* Declared by individual translators */
struct ast_translator_pvt;

struct ast_translator {
	char name[80];
	int srcfmt;
	int dstfmt;
	struct ast_translator_pvt *(*new)();
	int (*framein)(struct ast_translator_pvt *pvt, struct ast_frame *in);
	struct ast_frame * (*frameout)(struct ast_translator_pvt *pvt);
	void (*destroy)(struct ast_translator_pvt *pvt);
	/* For performance measurements */
	/* Generate an example frame */
	struct ast_frame * (*sample)(void);
	/* Cost in milliseconds for encoding/decoding 1 second of sound */
	int cost;
	/* For linking, not to be modified by the translator */
	struct ast_translator *next;
};

struct ast_trans_pvt;

/* Create a pseudo channel which translates from a real channel into our
   desired format.  When a translator is installed, you should not use the
   sub channel until you have stopped the translator.  For all other
   actions, use the real channel. Generally, translators should be created 
   when needed and immediately destroyed when no longer needed.  */

/* Directions */
#define AST_DIRECTION_OUT  1
#define AST_DIRECTION_IN   2
#define AST_DIRECTION_BOTH 3

extern struct ast_channel *ast_translator_create(struct ast_channel *real, int format, int direction);
extern void ast_translator_destroy(struct ast_channel *tran);
/* Register a Codec translator */
extern int ast_register_translator(struct ast_translator *t);
/* Unregister same */
extern int ast_unregister_translator(struct ast_translator *t);
/* Given a list of sources, and a designed destination format, which should
   I choose? */
extern int ast_translator_best_choice(int dst, int srcs);
extern struct ast_trans_pvt *ast_translator_build_path(int source, int dest);
extern void ast_translator_free_path(struct ast_trans_pvt *tr);
extern struct ast_frame_chain *ast_translate(struct ast_trans_pvt *tr, struct ast_frame *f);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
