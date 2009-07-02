/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Core PBX routines and definitions.
 */

#ifndef _ASTERISK_PBX_H
#define _ASTERISK_PBX_H

#include "asterisk/sched.h"
#include "asterisk/channel.h"
#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_MAX_APP	32	/*!< Max length of an application */

#define AST_PBX_KEEP    0
#define AST_PBX_REPLACE 1

/*! \brief Special return values from applications to the PBX { */
#define AST_PBX_KEEPALIVE               10	/*!< Destroy the thread, but don't hang up the channel */
/*! } */

#define PRIORITY_HINT	-1	/*!< Special Priority for a hint */

/*! \brief Extension states */
enum ast_extension_states {
	AST_EXTENSION_REMOVED = -2,	/*!< Extension removed */
	AST_EXTENSION_DEACTIVATED = -1,	/*!< Extension hint removed */
	AST_EXTENSION_NOT_INUSE = 0,	/*!< No device INUSE or BUSY  */
	AST_EXTENSION_INUSE = 1 << 0,	/*!< One or more devices INUSE */
	AST_EXTENSION_BUSY = 1 << 1,	/*!< All devices BUSY */
	AST_EXTENSION_UNAVAILABLE = 1 << 2, /*!< All devices UNAVAILABLE/UNREGISTERED */
	AST_EXTENSION_RINGING = 1 << 3,	/*!< All devices RINGING */
	AST_EXTENSION_ONHOLD = 1 << 4,	/*!< All devices ONHOLD */
};


struct ast_context;
struct ast_exten;     
struct ast_include;
struct ast_ignorepat;
struct ast_sw;

/*! \brief Typedef for devicestate and hint callbacks */
typedef int (*ast_state_cb_type)(char *context, char* id, enum ast_extension_states state, void *data);

/*! \brief Data structure associated with a custom dialplan function */
struct ast_custom_function {
	const char *name;		/*!< Name */
	const char *synopsis;		/*!< Short description for "show functions" */
	const char *desc;		/*!< Help text that explains it all */
	const char *syntax;		/*!< Syntax description */
	int (*read)(struct ast_channel *, char *, char *, char *, size_t);	/*!< Read function, if read is supported */
	int (*write)(struct ast_channel *, char *, char *, const char *);	/*!< Write function, if write is supported */
	AST_LIST_ENTRY(ast_custom_function) acflist;
};

/*! \brief All switch functions have the same interface, so define a type for them */
typedef int (ast_switch_f)(struct ast_channel *chan, const char *context,
	const char *exten, int priority, const char *callerid, const char *data);

/*!< Data structure associated with an Asterisk switch */
struct ast_switch {
	AST_LIST_ENTRY(ast_switch) list;
	const char *name;			/*!< Name of the switch */
	const char *description;		/*!< Description of the switch */
	
	ast_switch_f *exists;
	ast_switch_f *canmatch;
	ast_switch_f *exec;
	ast_switch_f *matchmore;
};

struct ast_timing {
	int hastime;				/*!< If time construct exists */
	unsigned int monthmask;			/*!< Mask for month */
	unsigned int daymask;			/*!< Mask for date */
	unsigned int dowmask;			/*!< Mask for day of week (mon-sun) */
	unsigned int minmask[24];		/*!< Mask for minute */
};

int ast_build_timing(struct ast_timing *i, const char *info);
int ast_check_timing(const struct ast_timing *i);

struct ast_pbx {
	int dtimeout;				/*!< Timeout between digits (seconds) */
	int rtimeout;				/*!< Timeout for response (seconds) */
};


/*!
 * \brief Register an alternative dialplan switch
 *
 * \param sw switch to register
 *
 * This function registers a populated ast_switch structure with the
 * asterisk switching architecture.
 *
 * \return 0 on success, and other than 0 on failure
 */
int ast_register_switch(struct ast_switch *sw);

/*!
 * \brief Unregister an alternative switch
 *
 * \param sw switch to unregister
 * 
 * Unregisters a switch from asterisk.
 *
 * \return nothing
 */
void ast_unregister_switch(struct ast_switch *sw);

/*!
 * \brief Look up an application
 *
 * \param app name of the app
 *
 * This function searches for the ast_app structure within
 * the apps that are registered for the one with the name
 * you passed in.
 *
 * \return the ast_app structure that matches on success, or NULL on failure
 */
struct ast_app *pbx_findapp(const char *app);

/*!
 * \brief Execute an application
 *
 * \param c channel to execute on
 * \param app which app to execute
 * \param data the data passed into the app
 *
 * This application executes an application on a given channel.  It
 * saves the stack and executes the given appliation passing in
 * the given data.
 *
 * \return 0 on success, and -1 on failure
 */
int pbx_exec(struct ast_channel *c, struct ast_app *app, void *data);

/*!
 * \brief Register a new context
 *
 * \param extcontexts pointer to the ast_context structure pointer
 * \param name name of the new context
 * \param registrar registrar of the context
 *
 * This will first search for a context with your name.  If it exists already, it will not
 * create a new one.  If it does not exist, it will create a new one with the given name
 * and registrar.
 *
 * \return NULL on failure, and an ast_context structure on success
 */
struct ast_context *ast_context_create(struct ast_context **extcontexts, const char *name, const char *registrar);
struct ast_context *ast_context_find_or_create(struct ast_context **extcontexts, const char *name, const char *registrar);

/*!
 * \brief Merge the temporary contexts into a global contexts list and delete from the 
 *        global list the ones that are being added
 *
 * \param extcontexts pointer to the ast_context structure pointer
 * \param registrar of the context; if it's set the routine will delete all contexts 
 *        that belong to that registrar; if NULL only the contexts that are specified 
 *        in extcontexts
 */
void ast_merge_contexts_and_delete(struct ast_context **extcontexts, const char *registrar);

/*!
 * \brief Destroy a context (matches the specified context (or ANY context if NULL)
 *
 * \param con context to destroy
 * \param registrar who registered it
 *
 * You can optionally leave out either parameter.  It will find it
 * based on either the ast_context or the registrar name.
 *
 * \return nothing
 */
void ast_context_destroy(struct ast_context *con, const char *registrar);

/*!
 * \brief Find a context
 *
 * \param name name of the context to find
 *
 * Will search for the context with the given name.
 *
 * \return the ast_context on success, NULL on failure.
 */
struct ast_context *ast_context_find(const char *name);

enum ast_pbx_result {
	AST_PBX_SUCCESS = 0,
	AST_PBX_FAILED = -1,
	AST_PBX_CALL_LIMIT = -2,
};

/*!
 * \brief Create a new thread and start the PBX
 *
 * \param c channel to start the pbx on
 *
 * See ast_pbx_run for a synchronous function to run the PBX in the
 * current thread, as opposed to starting a new one.
 *
 * \return Zero on success, non-zero on failure
 */
enum ast_pbx_result ast_pbx_start(struct ast_channel *c);

/*!
 * \brief Execute the PBX in the current thread
 *
 * \param c channel to run the pbx on
 *
 * This executes the PBX on a given channel. It allocates a new
 * PBX structure for the channel, and provides all PBX functionality.
 * See ast_pbx_start for an asynchronous function to run the PBX in a
 * new thread as opposed to the current one.
 * 
 * \return Zero on success, non-zero on failure
 */
enum ast_pbx_result ast_pbx_run(struct ast_channel *c);

/*! 
 * \brief Add and extension to an extension context.  
 * 
 * \param context context to add the extension to
 * \param replace
 * \param extension extension to add
 * \param priority priority level of extension addition
 * \param label extension label
 * \param callerid pattern to match CallerID, or NULL to match any CallerID
 * \param application application to run on the extension with that priority level
 * \param data data to pass to the application
 * \param datad
 * \param registrar who registered the extension
 *
 * \retval 0 success 
 * \retval -1 failure
 */
int ast_add_extension(const char *context, int replace, const char *extension, 
	int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *), const char *registrar);

/*! 
 * \brief Add an extension to an extension context, this time with an ast_context *.
 *
 * \note For details about the arguments, check ast_add_extension()
 */
int ast_add_extension2(struct ast_context *con, int replace, const char *extension,
	int priority, const char *label, const char *callerid, 
	const char *application, void *data, void (*datad)(void *), const char *registrar);


/*! 
 * \brief Register an application.
 *
 * \param app Short name of the application
 * \param execute a function callback to execute the application. It should return
 *                non-zero if the channel needs to be hung up.
 * \param synopsis a short description (one line synopsis) of the application
 * \param description long description with all of the details about the use of 
 *                    the application
 * 
 * This registers an application with Asterisk's internal application list. 
 * \note The individual applications themselves are responsible for registering and unregistering
 *       and unregistering their own CLI commands.
 * 
 * \retval 0 success 
 * \retval -1 failure.
 */
int ast_register_application(const char *app, int (*execute)(struct ast_channel *, void *),
			     const char *synopsis, const char *description);

/*! 
 * \brief Unregister an application
 * 
 * \param app name of the application (does not have to be the same string as the one that was registered)
 * 
 * This unregisters an application from Asterisk's internal application list.
 * 
 * \retval 0 success 
 * \retval -1 failure
 */
int ast_unregister_application(const char *app);

/*! 
 * \brief Uses hint and devicestate callback to get the state of an extension
 *
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to get state
 *
 * \return extension state as defined in the ast_extension_states enum
 */
int ast_extension_state(struct ast_channel *c, const char *context, const char *exten);

/*! 
 * \brief Return string representation of the state of an extension
 * 
 * \param extension_state is the numerical state delivered by ast_extension_state
 *
 * \return the state of an extension as string
 */
const char *ast_extension_state2str(int extension_state);

/*!
 * \brief Registers a state change callback
 * 
 * \param context which context to look in
 * \param exten which extension to get state
 * \param callback callback to call if state changed
 * \param data to pass to callback
 *
 * The callback is called if the state of an extension is changed.
 *
 * \retval -1 on failure
 * \retval ID on success
 */ 
int ast_extension_state_add(const char *context, const char *exten, 
			    ast_state_cb_type callback, void *data);

/*! 
 * \brief Deletes a registered state change callback by ID
 * 
 * \param id of the callback to delete
 * \param callback callback
 *
 * Removes the callback from list of callbacks
 *
 * \retval 0 success 
 * \retval -1 failure
 */
int ast_extension_state_del(int id, ast_state_cb_type callback);

/*! 
 * \brief If an extension exists, return non-zero
 * 
 * \param hint buffer for hint
 * \param maxlen size of hint buffer
 * \param name buffer for name portion of hint
 * \param maxnamelen size of name buffer
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 *
 * \return If an extension within the given context with the priority PRIORITY_HINT
 * is found a non zero value will be returned.
 * Otherwise, 0 is returned.
 */
int ast_get_hint(char *hint, int maxlen, char *name, int maxnamelen, 
	struct ast_channel *c, const char *context, const char *exten);

/*!
 * \brief Determine whether an extension exists
 *
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * \param priority priority of the action within the extension
 * \param callerid callerid to search for
 *
 * \note It is possible for autoservice to be started and stopped on c during this
 * function call, it is important that c is not locked prior to calling this. Otherwise
 * a deadlock may occur
 *
 * \return If an extension within the given context(or callerid) with the given priority 
 *         is found a non zero value will be returned. Otherwise, 0 is returned.
 */
int ast_exists_extension(struct ast_channel *c, const char *context, const char *exten, 
	int priority, const char *callerid);

/*! 
 * \brief Find the priority of an extension that has the specified label
 * 
 * \param c this is not important
 * \param context which context to look in
 * \param exten which extension to search for
 * \param label label of the action within the extension to match to priority
 * \param callerid callerid to search for
 *
 * \note It is possible for autoservice to be started and stopped on c during this
 * function call, it is important that c is not locked prior to calling this. Otherwise
 * a deadlock may occur
 *
 * \return the priority which matches the given label in the extension or -1 if not found.
 */
int ast_findlabel_extension(struct ast_channel *c, const char *context, 
	const char *exten, const char *label, const char *callerid);

/*!
 * \brief Find the priority of an extension that has the specified label
 *
 * \note It is possible for autoservice to be started and stopped on c during this
 * function call, it is important that c is not locked prior to calling this. Otherwise
 * a deadlock may occur
 *
 * \note This function is the same as ast_findlabel_extension, except that it accepts
 * a pointer to an ast_context structure to specify the context instead of the
 * name of the context. Otherwise, the functions behave the same.
 */
int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, 
	const char *exten, const char *label, const char *callerid);

/*! 
 * \brief Looks for a valid matching extension
 * 
 * \param c not really important
 * \param context context to serach within
 * \param exten extension to check
 * \param priority priority of extension path
 * \param callerid callerid of extension being searched for
 *
 * \note It is possible for autoservice to be started and stopped on c during this
 * function call, it is important that c is not locked prior to calling this. Otherwise
 * a deadlock may occur
 *
 * \return If "exten" *could be* a valid extension in this context with or without
 * some more digits, return non-zero.  Basically, when this returns 0, no matter
 * what you add to exten, it's not going to be a valid extension anymore
 */
int ast_canmatch_extension(struct ast_channel *c, const char *context, 
	const char *exten, int priority, const char *callerid);

/*! 
 * \brief Looks to see if adding anything to this extension might match something. (exists ^ canmatch)
 *
 * \param c not really important XXX
 * \param context context to serach within
 * \param exten extension to check
 * \param priority priority of extension path
 * \param callerid callerid of extension being searched for
 *
 * \note It is possible for autoservice to be started and stopped on c during this
 * function call, it is important that c is not locked prior to calling this. Otherwise
 * a deadlock may occur
 *
 * \return If "exten" *could match* a valid extension in this context with
 * some more digits, return non-zero.  Does NOT return non-zero if this is
 * an exact-match only.  Basically, when this returns 0, no matter
 * what you add to exten, it's not going to be a valid extension anymore
 */
int ast_matchmore_extension(struct ast_channel *c, const char *context, 
	const char *exten, int priority, const char *callerid);

/*! 
 * \brief Determine if a given extension matches a given pattern (in NXX format)
 * 
 * \param pattern pattern to match
 * \param extension extension to check against the pattern.
 *
 * Checks whether or not the given extension matches the given pattern.
 *
 * \retval 1 on match
 * \retval 0 on failure
 */
int ast_extension_match(const char *pattern, const char *extension);

int ast_extension_close(const char *pattern, const char *data, int needmore);

/*! 
 * \brief Launch a new extension (i.e. new stack)
 * 
 * \param c not important
 * \param context which context to generate the extension within
 * \param exten new extension to add
 * \param priority priority of new extension
 * \param callerid callerid of extension
 *
 * This adds a new extension to the asterisk extension list.
 *
 * \note It is possible for autoservice to be started and stopped on c during this
 * function call, it is important that c is not locked prior to calling this. Otherwise
 * a deadlock may occur
 *
 * \retval 0 on success 
 * \retval -1 on failure.
 */
int ast_spawn_extension(struct ast_channel *c, const char *context, 
	const char *exten, int priority, const char *callerid);

/*! 
 * \brief Add a context include
 *
 * \param context context to add include to
 * \param include new include to add
 * \param registrar who's registering it
 *
 * Adds an include taking a char * string as the context parameter
 *
 * \retval 0 on success 
 * \retval -1 on error
*/
int ast_context_add_include(const char *context, const char *include, 
	const char *registrar);

/*! 
 * \brief Add a context include
 * 
 * \param con context to add the include to
 * \param include include to add
 * \param registrar who registered the context
 *
 * Adds an include taking a struct ast_context as the first parameter
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_context_add_include2(struct ast_context *con, const char *include, 
	const char *registrar);

/*! 
 * \brief Remove a context include
 * 
 * \note See ast_context_add_include for information on arguments
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_context_remove_include(const char *context, const char *include, 
	const char *registrar);

/*! 
 * \brief Removes an include by an ast_context structure 
 * 
 * \note See ast_context_add_include2 for information on arguments
 *
 * \retval 0 on success
 * \retval -1 on success
 */
int ast_context_remove_include2(struct ast_context *con, const char *include, 
	const char *registrar);

/*! 
 * \brief Verifies includes in an ast_contect structure
 * 
 * \param con context in which to verify the includes
 *
 * \retval 0 if no problems found 
 * \retval -1 if there were any missing context
 */
int ast_context_verify_includes(struct ast_context *con);
	  
/*! 
 * \brief Add a switch
 * 
 * \param context context to which to add the switch
 * \param sw switch to add
 * \param data data to pass to switch
 * \param eval whether to evaluate variables when running switch
 * \param registrar whoever registered the switch
 *
 * This function registers a switch with the asterisk switch architecture
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_context_add_switch(const char *context, const char *sw, const char *data, 
	int eval, const char *registrar);

/*! 
 * \brief Adds a switch (first param is a ast_context)
 * 
 * \note See ast_context_add_switch() for argument information, with the exception of
 *       the first argument. In this case, it's a pointer to an ast_context structure
 *       as opposed to the name.
 */
int ast_context_add_switch2(struct ast_context *con, const char *sw, const char *data, 
	int eval, const char *registrar);

/*! 
 * \brief Remove a switch
 * 
 * Removes a switch with the given parameters
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_context_remove_switch(const char *context, const char *sw, 
	const char *data, const char *registrar);

int ast_context_remove_switch2(struct ast_context *con, const char *sw, 
	const char *data, const char *registrar);

/*! 
 * \brief Simply remove extension from context
 * 
 * \param context context to remove extension from
 * \param extension which extension to remove
 * \param priority priority of extension to remove (0 to remove all)
 * \param callerid NULL to remove all; non-NULL to match a single record per priority
 * \param matchcid non-zero to match callerid element (if non-NULL); 0 to match default case
 * \param registrar registrar of the extension
 *
 * This function removes an extension from a given context.
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_context_remove_extension(const char *context, const char *extension, int priority,
	const char *registrar);

int ast_context_remove_extension2(struct ast_context *con, const char *extension,
	int priority, const char *registrar);

int ast_context_remove_extension_callerid(const char *context, const char *extension,
	int priority, const char *callerid, int matchcid, const char *registrar);

int ast_context_remove_extension_callerid2(struct ast_context *con, const char *extension,
	int priority, const char *callerid, int matchcid, const char *registrar);

/*! 
 * \brief Add an ignorepat
 * 
 * \param context which context to add the ignorpattern to
 * \param ignorepat ignorepattern to set up for the extension
 * \param registrar registrar of the ignore pattern
 *
 * Adds an ignore pattern to a particular context.
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_context_add_ignorepat(const char *context, const char *ignorepat, const char *registrar);

int ast_context_add_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar);

/* 
 * \brief Remove an ignorepat
 * 
 * \param context context from which to remove the pattern
 * \param ignorepat the pattern to remove
 * \param registrar the registrar of the ignore pattern
 *
 * This removes the given ignorepattern
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_context_remove_ignorepat(const char *context, const char *ignorepat, const char *registrar);

int ast_context_remove_ignorepat2(struct ast_context *con, const char *ignorepat, const char *registrar);

/*! 
 * \brief Checks to see if a number should be ignored
 * 
 * \param context context to search within
 * \param pattern to check whether it should be ignored or not
 *
 * Check if a number should be ignored with respect to dialtone cancellation.
 *
 * \retval 0 if the pattern should not be ignored 
 * \retval non-zero if the pattern should be ignored 
 */
int ast_ignore_pattern(const char *context, const char *pattern);

/* Locking functions for outer modules, especially for completion functions */

/*! 
 * \brief Locks the context list
 *
 * \retval 0 on success 
 * \retval -1 on error
 */
int ast_lock_contexts(void); /* equivalent to wrlock */
int ast_rdlock_contexts(void);
int ast_wrlock_contexts(void);

/*! 
 * \brief Unlocks contexts
 * 
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_unlock_contexts(void);

/*! 
 * \brief Locks a given context
 * 
 * \param con context to lock
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_lock_context(struct ast_context *con);

/*! 
 * \retval Unlocks the given context
 * 
 * \param con context to unlock
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_unlock_context(struct ast_context *con);

/*! 
 * \brief locks the macrolock in the given given context
 *
 * \param macrocontext name of the macro-context to lock
 *
 * Locks the given macro-context to ensure only one thread (call) can execute it at a time
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_context_lockmacro(const char *macrocontext);

/*!
 * \brief Unlocks the macrolock in the given context
 *
 * \param macrocontext name of the macro-context to unlock
 *
 * Unlocks the given macro-context so that another thread (call) can execute it
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_context_unlockmacro(const char *macrocontext);

int ast_async_goto(struct ast_channel *chan, const char *context, const char *exten, int priority);

int ast_async_goto_by_name(const char *chan, const char *context, const char *exten, int priority);

/*! Synchronously or asynchronously make an outbound call and send it to a
   particular extension */
int ast_pbx_outgoing_exten(const char *type, int format, void *data, int timeout, const char *context, const char *exten, int priority, int *reason, int sync, const char *cid_num, const char *cid_name, struct ast_variable *vars, const char *account, struct ast_channel **locked_channel);

/*! Synchronously or asynchronously make an outbound call and send it to a
   particular application with given extension */
int ast_pbx_outgoing_app(const char *type, int format, void *data, int timeout, const char *app, const char *appdata, int *reason, int sync, const char *cid_num, const char *cid_name, struct ast_variable *vars, const char *account, struct ast_channel **locked_channel);

/*!
 * \brief Evaluate a condition
 *
 * \retval 0 if the condition is NULL or of zero length
 * \retval int If the string is an integer, the integer representation of
 *             the integer is returned
 * \retval 1 Any other non-empty string
 */
int pbx_checkcondition(const char *condition);

/* Functions for returning values from structures */
const char *ast_get_context_name(struct ast_context *con);
const char *ast_get_extension_name(struct ast_exten *exten);
struct ast_context *ast_get_extension_context(struct ast_exten *exten);
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

/*!
 * \note Will lock the channel.
 */
int pbx_builtin_serialize_variables(struct ast_channel *chan, char *buf, size_t size);

/*!
 * \note Will lock the channel.
 */
const char *pbx_builtin_getvar_helper(struct ast_channel *chan, const char *name);

/*!
 * \note Will lock the channel.
 */
void pbx_builtin_pushvar_helper(struct ast_channel *chan, const char *name, const char *value);

/*!
 * \note Will lock the channel.
 */
void pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value);

/*!
 * \note Will lock the channel.
 */
void pbx_retrieve_variable(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp);
void pbx_builtin_clear_globals(void);

/*!
 * \note Will lock the channel.
 */
int pbx_builtin_setvar(struct ast_channel *chan, void *data);

void pbx_substitute_variables_helper(struct ast_channel *c,const char *cp1,char *cp2,int count);
void pbx_substitute_variables_varshead(struct varshead *headp, const char *cp1, char *cp2, int count);

int ast_extension_patmatch(const char *pattern, const char *data);

/*! Set "autofallthrough" flag, if newval is <0, does not acutally set.  If
  set to 1, sets to auto fall through.  If newval set to 0, sets to no auto
  fall through (reads extension instead).  Returns previous value. */
int pbx_set_autofallthrough(int newval);

/*!
 * \note This function will handle locking the channel as needed.
 */
int ast_goto_if_exists(struct ast_channel *chan, const char *context, const char *exten, int priority);

/*!
 * \note I can find neither parsable nor parseable at dictionary.com, 
 *       but google gives me 169000 hits for parseable and only 49,800 
 *       for parsable 
 *
 * \note This function will handle locking the channel as needed.
 */
int ast_parseable_goto(struct ast_channel *chan, const char *goto_string);

/*!
 * \note This function will handle locking the channel as needed.
 */
int ast_explicit_goto(struct ast_channel *chan, const char *context, const char *exten, int priority);

/*!
 * \note This function will handle locking the channel as needed.
 */
int ast_async_goto_if_exists(struct ast_channel *chan, const char *context, const char *exten, int priority);

struct ast_custom_function* ast_custom_function_find(const char *name);

/*!
 * \brief Unregister a custom function
 */
int ast_custom_function_unregister(struct ast_custom_function *acf);

/*!
 * \brief Reigster a custom function
 */
int ast_custom_function_register(struct ast_custom_function *acf);

/*! 
 * \brief Retrieve the number of active calls
 */
int ast_active_calls(void);
	
/*!
 * \brief executes a read operation on a function 
 *
 * \param chan Channel to execute on
 * \param function Data containing the function call string (will be modified)
 * \param workspace A pointer to safe memory to use for a return value 
 * \param len the number of bytes in workspace
 *
 * This application executes a function in read mode on a given channel.
 *
 * \return zero on success, non-zero on failure
 */
int ast_func_read(struct ast_channel *chan, char *function, char *workspace, size_t len);

/*!
 * \brief executes a write operation on a function
 *
 * \param chan Channel to execute on
 * \param function Data containing the function call string (will be modified)
 * \param value A value parameter to pass for writing
 *
 * This application executes a function in write mode on a given channel.
 *
 * \return zero on success, non-zero on failure
 */
int ast_func_write(struct ast_channel *chan, char *function, const char *value);

void ast_hint_state_changed(const char *device);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_PBX_H */
