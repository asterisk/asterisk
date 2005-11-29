/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Core PBX routines and definitions.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
#ifndef _ASTERISK_PBX_H
#define _ASTERISK_PBX_H

#include <asterisk/sched.h>
#include <asterisk/channel.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_PBX_KEEP    0
#define AST_PBX_REPLACE 1

/* Max length of an application */
#define AST_MAX_APP	32

/* Special return values from applications to the PBX */
#define AST_PBX_KEEPALIVE	10		/* Destroy the thread, but don't hang up the channel */

struct ast_context;

/* Register a new context */
struct ast_context *ast_context_create(char *name, char *registrar);

/* Destroy a context (matches the specified context (or ANY context if
   NULL) */
void ast_context_destroy(struct ast_context *, char *registrar);

/* Find a context */
struct ast_context *ast_context_find(char *name);

/* Create a new thread and start the PBX (or whatever) */
int ast_pbx_start(struct ast_channel *c);

/* Execute the PBX in the current thread */
int ast_pbx_run(struct ast_channel *c);

/* Add an extension to an extension context, this time with an ast_context * */
int ast_add_extension2(struct ast_context *con,
				      int replace, char *extension, int priority, 
					  char *application, void *data, void (*datad)(void *),
					  char *registrar);

/* Add an application.  The function 'execute' should return non-zero if the line needs to be hung up. 
   Include a one-line synopsis (e.g. 'hangs up a channel') and a more lengthy, multiline
   description with more detail, including under what conditions the application
   will return 0 or -1.  */
int ast_register_application(char *app, int (*execute)(struct ast_channel *, void *),
			     char *synopsis, char *description);

/* Remove an application */
int ast_unregister_application(char *app);

/* If an extension exists, return non-zero */
int ast_exists_extension(struct ast_channel *c, char *context, char *exten, int priority);

/* If "exten" *could be* a valid extension in this context with or without
   some more digits, return non-zero.  Basically, when this returns 0, no matter
   what you add to exten, it's not going to be a valid extension anymore */
int ast_canmatch_extension(struct ast_channel *c, char *context, char *exten, int priority);

/* Determine if a given extension matches a given pattern (in NXX format) */
int ast_extension_match(char *pattern, char *extension);

/* Launch a new extension (i.e. new stack) */
int ast_spawn_extension(struct ast_channel *c, char *context, char *exten, int priority);

/* Execute an extension.  If it's not available, do whatever you should do for
   default extensions and halt the thread if necessary.  This function does not
   return, except on error. */
int ast_exec_extension(struct ast_channel *c, char *context, char *exten, int priority);
/* Longest extension */
int ast_pbx_longest_extension(char *context);

/* Add an include */
int ast_context_add_include(char *context, char *include, char *registrar);
int ast_context_add_include2(struct ast_context *con, char *include, char *registrar);

/* Remove an include */
int ast_context_remove_include(char *context, char *include, char *registrar);
int ast_context_remove_include2(struct ast_context *con, char *include, char *registrar);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
