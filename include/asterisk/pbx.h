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

//! Max length of an application
#define AST_MAX_APP	32

//! Special return values from applications to the PBX
#define AST_PBX_KEEPALIVE	10		/* Destroy the thread, but don't hang up the channel */
#define AST_PBX_NO_HANGUP_PEER       11

//! Special Priority for an hint
#define PRIORITY_HINT	-1

//! Extension states
//! No device INUSE or BUSY 
#define AST_EXTENSION_NOT_INUSE		0
//! One or more devices INUSE
#define AST_EXTENSION_INUSE		1
//! All devices BUSY
#define AST_EXTENSION_BUSY		2
//! All devices UNAVAILABLE/UNREGISTERED
#define AST_EXTENSION_UNAVAILABLE 	3

struct ast_context;
struct ast_exten;     
struct ast_include;
struct ast_ignorepat;
struct ast_sw;

typedef int (*ast_state_cb_type)(char *context, char* id, int state, void *data);

//! Data structure associated with an asterisk switch
struct ast_switch {
	/*! NULL */
	struct ast_switch *next;	
	/*! Name of the switch */
	const char *name;				
	/*! Description of the switch */
	const char *description;		
	
	int (*exists)(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
	
	int (*canmatch)(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
	
	int (*exec)(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, int newstack, const char *data);

	int (*matchmore)(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
};

struct ast_pbx {
        int dtimeout;                                   /* Timeout between digits (seconds) */
        int rtimeout;                                   /* Timeout for response
							   (seconds) */
};


//! Register an alternative switch
/*!
 * \param sw switch to register
 * This function registers a populated ast_switch structure with the
 * asterisk switching architecture.
 * It returns 0 on success, and other than 0 on failure
 */
extern int ast_register_switch(struct ast_switch *sw);

//! Unregister an alternative switch
/*!
 * \param sw switch to unregister
 * Unregisters a switch from asterisk.
 * Returns nothing
 */
extern void ast_unregister_switch(struct ast_switch *sw);

//! Look up an application
/*!
 * \param app name of the app
 * This function searches for the ast_app structure within
 * the apps that are registered for the one with the name
 * you passed in.
 * Returns the ast_app structure that matches on success, or NULL on failure
 */
extern struct ast_app *pbx_findapp(const char *app);

//! executes an application
/*!
 * \param c channel to execute on
 * \param app which app to execute
 * \param data the data passed into the app
 * \param newstack stack pointer
 * This application executes an application on a given channel.  It
 * saves the stack and executes the given appliation passing in
 * the given data.
 * It returns 0 on success, and -1 on failure
 */
int pbx_exec(struct ast_channel *c, struct ast_app *app, void *data, int newstack);

//! Register a new context
/*!
 * \param extcontexts pointer to the ast_context structure pointer
 * \param name name of the new context
 * \param registrar registrar of the context
 * This will first search for a context with your name.  If it exists already, it will not
 * create a new one.  If it does not exist, it will create a new one with the given name
 * and registrar.
 * It returns NULL on failure, and an ast_context structure on success
 */
struct ast_context *ast_context_create(struct ast_context **extcontexts, const char *name, const char *registrar);

//! Merge the temporary contexts into a global contexts list and delete from the global list the ones that are being added
/*!
 * \param extcontexts pointer to the ast_context structure pointer
 * \param registar of the context; if it's set the routine will delete all contexts that belong to that registrar; if NULL only the contexts that are specified in extcontexts
 */
void ast_merge_contexts_and_delete(struct ast_context **extcontexts, const char *registrar);

//! Destroy a context (matches the specified context (or ANY context if NULL)
/*!
 * \param con context to destroy
 * \param registrar who registered it
 * You can optionally leave out either parameter.  It will find it
 * based on either the ast_context or the registrar name.
 * Returns nothing
 */
void ast_context_destroy(struct ast_context *con, const char *registrar);

//! Find a context
/*!
 * \param name name of the context to find
 * Will search for the context with the given name.
 * Returns the ast_context on success, NULL on failure.
 */
struct ast_context *ast_context_find(const char *name);

//! Create a new thread and start the PBX (or whatever)
/*!
 * \param c channel to start the pbx on
 * Starts a pbx thread on a given channel
 * It returns -1 on failure, and 0 on success
 */
int ast_pbx_start(struct ast_channel *c);

//! Execute the PBX in the current thread
/*!
 * \param c channel to run the pbx on
 * This executes the PBX on a given channel.  It allocates a new
 * PBX structure for the channel, and provides all PBX functionality.
 */
int ast_pbx_run(struct ast_channel *c);

/*! 
 * \param context context to add the extension to
 * \param replace
 * \param extension extension to add
 * \param priority priority level of extension addition
 * \param callerid callerid of extension
 * \param application application to run on the extension with that priority level
 * \param data data to pass to the application
 * \param datad
 * \param registrar who registered the extension
 * Add and extension to an extension context.  
 * Callerid is a pattern to match CallerID, or NULL to match any callerid
 * Returns 0 on success, -1 on failure
 */
int ast_add_extension(const char *context, int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar);

//! Add an extension to an extension context, this time with an ast_context *.  CallerID is a pattern to match on callerid, or NULL to not care about callerid
/*! 
 * For details about the arguements, check ast_add_extension()
 */
int ast_add_extension2(struct ast_context *con,
				      int replace, const char *extension, int priority, const char *label, const char *callerid, 
					  const char *application, void *data, void (*datad)(void *),
					  const char *registrar);

//! Add an application.  The function 'execute' should return non-zero if the line needs to be hung up. 
/*!
  \param app Short name of the application
  \param execute a function callback to execute the application
  \param synopsis a short description of the application
  \param description long description of the application
   Include a one-line synopsis (e.g. 'hangs up a channel') and a more lengthy, multiline
   description with more detail, including under what conditions the application
   will return 0 or -1.
   This registers an application with asterisks internal application list.  Please note:
   The individual applications themselves are responsible for registering and unregistering
   CLI commands.
   It returns 0 on success, -1 on failure.
*/
int ast_register_application(const char *app, int (*execute)(struct ast_channel *, void *),
			     const char *synopsis, const char *description);

//! Remove an application
/*!
 * \param app name of the application (does not have to be the same string as the one that was registered)
 * This unregisters an application from asterisk's internal registration mechanisms.
 * It returns 0 on success, and -1 on failure.
 */
int ast_unregister_application(const char *app);

//! Uses hint and devicestate callback to get the state of an extension
/*!
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to get state
 * Returns extension state !! = AST_EXTENSION_???
 */
int ast_extension_state(struct ast_channel *c, char *context, char *exten);

//! Tells Asterisk the State for Device is changed
/*!
 * \param fmt devicename like a dialstring with format parameters
 * Asterisk polls the new extensionstates and calls the registered
 * callbacks for the changed extensions
 * Returns 0 on success, -1 on failure
 */
int ast_device_state_changed(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

//! Registers a state change callback
/*!
 * \param context which context to look in
 * \param exten which extension to get state
 * \param callback callback to call if state changed
 * \param data to pass to callback
 * The callback is called if the state for extension is changed
 * Return -1 on failure, ID on success
 */ 
int ast_extension_state_add(const char *context, const char *exten, 
			    ast_state_cb_type callback, void *data);

//! Deletes a registered state change callback by ID
/*!
 * \param id of the callback to delete
 * Removes the callback from list of callbacks
 * Return 0 on success, -1 on failure
 */
int ast_extension_state_del(int id, ast_state_cb_type callback);

//! If an extension exists, return non-zero
/*!
 * \param hint buffer for hint
 * \param maxlen size of hint buffer
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * If an extension within the given context with the priority PRIORITY_HINT
 * is found a non zero value will be returned.
 * Otherwise, 0 is returned.
 */
int ast_get_hint(char *hint, int maxlen, struct ast_channel *c, const char *context, const char *exten);

//! If an extension exists, return non-zero
// work
/*!
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * \param priority priority of the action within the extension
 * \param callerid callerid to search for
 * If an extension within the given context(or callerid) with the given priority is found a non zero value will be returned.
 * Otherwise, 0 is returned.
 */
int ast_exists_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid);

//! If an extension exists, return non-zero
// work
/*!
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * \param labellabel of the action within the extension to match to priority
 * \param callerid callerid to search for
 * If an priority which matches given label in extension or -1 if not found.
\ */
int ast_findlabel_extension(struct ast_channel *c, const char *context, const char *exten, const char *label, const char *callerid);

//! Looks for a valid matching extension
/*!
  \param c not really important
  \param context context to serach within
  \param exten extension to check
  \param priority priority of extension path
  \param callerid callerid of extension being searched for
   If "exten" *could be* a valid extension in this context with or without
   some more digits, return non-zero.  Basically, when this returns 0, no matter
   what you add to exten, it's not going to be a valid extension anymore
*/
int ast_canmatch_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid);

//! Looks to see if adding anything to this extension might match something. (exists ^ canmatch)
/*!
  \param c not really important
  \param context context to serach within
  \param exten extension to check
  \param priority priority of extension path
  \param callerid callerid of extension being searched for
   If "exten" *could match* a valid extension in this context with
   some more digits, return non-zero.  Does NOT return non-zero if this is
   an exact-match only.  Basically, when this returns 0, no matter
   what you add to exten, it's not going to be a valid extension anymore
*/
int ast_matchmore_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid);

//! Determine if a given extension matches a given pattern (in NXX format)
/*!
 * \param pattern pattern to match
 * \param extension extension to check against the pattern.
 * Checks whether or not the given extension matches the given pattern.
 * Returns 1 on match, 0 on failure
 */
int ast_extension_match(const char *pattern, const char *extension);

//! Launch a new extension (i.e. new stack)
/*!
 * \param c not important
 * \param context which context to generate the extension within
 * \param exten new extension to add
 * \param priority priority of new extension
 * \param callerid callerid of extension
 * This adds a new extension to the asterisk extension list.
 * It returns 0 on success, -1 on failure.
 */
int ast_spawn_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid);

//! Execute an extension.
/*!
  \param c channel to execute upon
  \param context which context extension is in
  \param exten extension to execute
  \param priority priority to execute within the given extension
   If it's not available, do whatever you should do for
   default extensions and halt the thread if necessary.  This function does not
   return, except on error.
*/
int ast_exec_extension(struct ast_channel *c, const char *context, const char *exten, int priority, const char *callerid);

//! Add an include
/*!
  \param context context to add include to
  \param include new include to add
  \param registrar who's registering it
   Adds an include taking a char * string as the context parameter
   Returns 0 on success, -1 on error
*/
int ast_context_add_include(const char *context, const char *include, const char *registrar);

//! Add an include
/*!
  \param con context to add the include to
  \param include include to add
  \param registrar who registered the context
   Adds an include taking a struct ast_context as the first parameter
   Returns 0 on success, -1 on failure
*/
int ast_context_add_include2(struct ast_context *con, const char *include, const char *registrar);

//! Removes an include
/*!
 * See add_include
 */
int ast_context_remove_include(const char *context, const char *include,const  char *registrar);
//! Removes an include by an ast_context structure
/*!
 * See add_include2
 */
int ast_context_remove_include2(struct ast_context *con, const char *include, const char *registrar);

//! Verifies includes in an ast_contect structure
/*!
 * \param con context in which to verify the includes
 * Returns 0 if no problems found, -1 if there were any missing context
 */
int ast_context_verify_includes(struct ast_context *con);
	  
//! Add a switch
/*!
 * \param context context to which to add the switch
 * \param sw switch to add
 * \param data data to pass to switch
 * \param registrar whoever registered the switch
 * This function registers a switch with the asterisk switch architecture
 * It returns 0 on success, -1 on failure
 */
int ast_context_add_switch(const char *context, const char *sw, const char *data, const char *registrar);
//! Adds a switch (first param is a ast_context)
/*!
 * See ast_context_add_switch()
 */
int ast_context_add_switch2(struct ast_context *con, const char *sw, const char *data, const char *registrar);

//! Remove a switch
/*!
 * Removes a switch with the given parameters
 * Returns 0 on success, -1 on failure
 */
int ast_context_remove_switch(const char *context, const char *sw, const char *data, const char *registrar);
int ast_context_remove_switch2(struct ast_context *con, const char *sw, const char *data, const char *registrar);

//! Simply remove extension from context
/*!
 * \param context context to remove extension from
 * \param extension which extension to remove
 * \param priority priority of extension to remove
 * \param registrar registrar of the extension
 * This function removes an extension from a given context.
 * Returns 0 on success, -1 on failure
 */
int ast_context_remove_extension(const char *context, const char *extension, int priority,
	const char *registrar);
int ast_context_remove_extension2(struct ast_context *con, const char *extension,
	int priority, const char *registrar);

//! Add an ignorepat
/*!
 * \param context which context to add the ignorpattern to
 * \param ignorpat ignorepattern to set up for the extension
 * \param registrar registrar of the ignore pattern
 * Adds an ignore pattern to a particular context.
 * Returns 0 on success, -1 on failure
 */
int ast_context_add_ignorepat(const char *context, const char *ignorepat, const char *registrar);
int ast_context_add_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar);

/* Remove an ignorepat */
/*!
 * \param context context from which to remove the pattern
 * \param ignorepat the pattern to remove
 * \param registrar the registrar of the ignore pattern
 * This removes the given ignorepattern
 * Returns 0 on success, -1 on failure
 */
int ast_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar);
int ast_context_remove_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar);

//! Checks to see if a number should be ignored
/*!
 * \param context context to search within
 * \param extension to check whether it should be ignored or not
 * Check if a number should be ignored with respect to dialtone cancellation.  
 * Returns 0 if the pattern should not be ignored, or non-zero if the pattern should be ignored 
 */
int ast_ignore_pattern(const char *context, const char *pattern);

/* Locking functions for outer modules, especially for completion functions */
//! Locks the contexts
/*! Locks the context list
 * Returns 0 on success, -1 on error
 */
int ast_lock_contexts(void);

//! Unlocks contexts
/*!
 * Returns 0 on success, -1 on failure
 */
int ast_unlock_contexts(void);

//! Locks a given context
/*!
 * \param con context to lock
 * Locks the context.
 * Returns 0 on success, -1 on failure
 */
int ast_lock_context(struct ast_context *con);
//! Unlocks the given context
/*!
 * \param con context to unlock
 * Unlocks the given context
 * Returns 0 on success, -1 on failure
 */
int ast_unlock_context(struct ast_context *con);


int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority);

int ast_async_goto_by_name(const char *chan, const char *context, const char *exten, int priority);

/* Synchronously or asynchronously make an outbound call and send it to a
   particular extension */
int ast_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, const char *variable, const char *account );

/* Synchronously or asynchronously make an outbound call and send it to a
   particular application with given extension */
int ast_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, const char *variable, const char *account);

/* Functions for returning values from structures */
const char *ast_get_context_name(struct ast_context *con);
const char *ast_get_extension_name(struct ast_exten *exten);
const char *ast_get_include_name(struct ast_include *include);
const char *ast_get_ignorepat_name(struct ast_ignorepat *ip);
const char *ast_get_switch_name(struct ast_sw *sw);
const char *ast_get_switch_data(struct ast_sw *sw);

/* Other extension stuff */
int ast_get_extension_priority(struct ast_exten *exten);
int ast_get_extension_matchcid(struct ast_exten *e);
const char *ast_get_extension_cidmatch(struct ast_exten *e);
const char *ast_get_extension_app(struct ast_exten *e);
const char *ast_get_extension_label(struct ast_exten *e);
void *ast_get_extension_app_data(struct ast_exten *e);

/* Registrar info functions ... */
const char *ast_get_context_registrar(struct ast_context *c);
const char *ast_get_extension_registrar(struct ast_exten *e);
const char *ast_get_include_registrar(struct ast_include *i);
const char *ast_get_ignorepat_registrar(struct ast_ignorepat *ip);
const char *ast_get_switch_registrar(struct ast_sw *sw);

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

extern char *pbx_builtin_getvar_helper(struct ast_channel *chan, char *name);
extern void pbx_builtin_setvar_helper(struct ast_channel *chan, char *name, char *value);
extern void pbx_builtin_clear_globals(void);
extern int pbx_builtin_setvar(struct ast_channel *chan, void *data);
extern void pbx_substitute_variables_helper(struct ast_channel *c,const char *cp1,char *cp2,int count);

int ast_extension_patmatch(const char *pattern, const char *data);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
