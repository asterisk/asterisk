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
struct ast_exten;     
struct ast_include;
struct ast_ignorepat;
struct ast_sw;

struct ast_switch {
	struct ast_switch *next;	/* NULL */
	char *name;				/* Name of the switch */
	char *description;		/* Description of the switch */
	int (*exists)(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, char *data);
	int (*canmatch)(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, char *data);
	int (*exec)(struct ast_channel *chan, char *context, char *exten, int priority, char *callerid, int newstack, char *data);
};

/* Register an alternative switch */
extern int ast_register_switch(struct ast_switch *sw);

/* Unregister an alternative switch */
extern void ast_unregister_switch(struct ast_switch *sw);

/* Look up an application */
extern struct ast_app *pbx_findapp(char *app);

int pbx_exec(struct ast_channel *c, struct ast_app *app, void *data, int newstack);

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

/* Add and extension to an extension context.  Callerid is a pattern to match CallerID, or NULL to match any
   callerid */
int ast_add_extension(char *context, int replace, char *extension, int priority, char *callerid,
	char *application, void *data, void (*datad)(void *), char *registrar);

/* Add an extension to an extension context, this time with an ast_context *.  CallerID is a pattern to match
   on callerid, or NULL to not care about callerid */
int ast_add_extension2(struct ast_context *con,
				      int replace, char *extension, int priority, char *callerid, 
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
int ast_exists_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid);

/* If "exten" *could be* a valid extension in this context with or without
   some more digits, return non-zero.  Basically, when this returns 0, no matter
   what you add to exten, it's not going to be a valid extension anymore */
int ast_canmatch_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid);

/* Determine if a given extension matches a given pattern (in NXX format) */
int ast_extension_match(char *pattern, char *extension);

/* Launch a new extension (i.e. new stack) */
int ast_spawn_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid);

/* Execute an extension.  If it's not available, do whatever you should do for
   default extensions and halt the thread if necessary.  This function does not
   return, except on error. */
int ast_exec_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid);

/* Add an include */
int ast_context_add_include(char *context, char *include, char *registrar);
int ast_context_add_include2(struct ast_context *con, char *include, char *registrar);

/* Remove an include */
int ast_context_remove_include(char *context, char *include, char *registrar);
int ast_context_remove_include2(struct ast_context *con, char *include, char *registrar);

/* Add a switch */
int ast_context_add_switch(char *context, char *sw, char *data, char *registrar);
int ast_context_add_switch2(struct ast_context *con, char *sw, char *data, char *registrar);

/* Remove a switch */
int ast_context_remove_switch(char *context, char *sw, char *data, char *registrar);
int ast_context_remove_switch2(struct ast_context *con, char *sw, char *data, char *registrar);

/* Simply remove extension from context */
int ast_context_remove_extension(char *context, char *extension, int priority,
	char *registrar);
int ast_context_remove_extension2(struct ast_context *con, char *extension,
	int priority, char *registrar);

/* Add an ignorepat */
int ast_context_add_ignorepat(char *context, char *ignorepat, char *registrar);
int ast_context_add_ignorepat2(struct ast_context *con, char *ignorepat, char *registrar);

/* Remove an ignorepat */
int ast_context_remove_ignorepat(char *context, char *ignorepat, char *registrar);
int ast_context_remove_ignorepat2(struct ast_context *con, char *ignorepat, char *registrar);

/* Check if a number should be ignored with respect to dialtone cancellation.  Returns 0 if
   the pattern should not be ignored, or non-zero if the pattern should be ignored */
int ast_ignore_pattern(char *context, char *pattern);

/* Locking functions for outer modules, especially for completion functions */
int ast_lock_contexts(void);
int ast_unlock_contexts(void);

int ast_lock_context(struct ast_context *con);
int ast_unlock_context(struct ast_context *con);

/* Functions for returning values from structures */
char *ast_get_context_name(struct ast_context *con);
char *ast_get_extension_name(struct ast_exten *exten);
char *ast_get_include_name(struct ast_include *include);
char *ast_get_ignorepat_name(struct ast_ignorepat *ip);
char *ast_get_switch_name(struct ast_sw *sw);
char *ast_get_switch_data(struct ast_sw *sw);

/* Other extension stuff */
int ast_get_extension_priority(struct ast_exten *exten);
char *ast_get_extension_app(struct ast_exten *e);
void *ast_get_extension_app_data(struct ast_exten *e);

/* Registrar info functions ... */
char *ast_get_context_registrar(struct ast_context *c);
char *ast_get_extension_registrar(struct ast_exten *e);
char *ast_get_include_registrar(struct ast_include *i);
char *ast_get_ignorepat_registrar(struct ast_ignorepat *ip);
char *ast_get_switch_registrar(struct ast_sw *sw);

/* Walking functions ... */
struct ast_context *ast_walk_contexts(struct ast_context *con);
struct ast_exten *ast_walk_context_extensions(struct ast_context *con,
	struct ast_exten *priority);
struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority);
struct ast_include *ast_walk_context_includes(struct ast_context *con,
	struct ast_include *inc);
struct ast_ignorepat *ast_walk_context_ignorepats(struct ast_context *con,
	struct ast_ignorepat *ip);
struct ast_sw *ast_walk_context_switches(struct ast_context *con, struct ast_sw *sw);
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
