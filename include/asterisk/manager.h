/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

#ifndef _ASTERISK_MANAGER_H
#define _ASTERISK_MANAGER_H

#include "asterisk/network.h"
#include "asterisk/lock.h"
#include "asterisk/datastore.h"
#include "asterisk/xmldoc.h"

/*!
 \file
 \brief The AMI - Asterisk Manager Interface - is a TCP protocol created to
 manage Asterisk with third-party software.

 Manager protocol packages are text fields of the form a: b.  There is
 always exactly one space after the colon.

\verbatim

 For Actions replies, the first line of the reply is a "Response:" header with
 values "success", "error" or "follows". "Follows" implies that the
 response is coming as separate events with the same ActionID. If the
 Action request has no ActionID, it will be hard matching events
 to the Action request in the manager client.

 The first header type is the "Event" header.  Other headers vary from
 event to event.  Headers end with standard \\r\\n termination.
 The last line of the manager response or event is an empty line.
 (\\r\\n)

\endverbatim

 \note Please try to \b re-use \b existing \b headers to simplify manager message parsing in clients.
    Don't re-use an existing header with a new meaning, please.
    You can find a reference of standard headers in doc/manager.txt

- \ref manager.c Main manager code file
 */

#define AMI_VERSION                     "1.2"
#define DEFAULT_MANAGER_PORT 5038	/* Default port for Asterisk management via TCP */

/*! \name Constant return values
 *\note Currently, returning anything other than zero causes the session to terminate.
 */
/*@{ */
#define	AMI_SUCCESS	(0)
#define	AMI_DESTROY	(-1)
/*@} */

/*! \name Manager event classes */
/*@{ */
#define EVENT_FLAG_SYSTEM 		(1 << 0) /* System events such as module load/unload */
#define EVENT_FLAG_CALL			(1 << 1) /* Call event, such as state change, etc */
#define EVENT_FLAG_LOG			(1 << 2) /* Log events */
#define EVENT_FLAG_VERBOSE		(1 << 3) /* Verbose messages */
#define EVENT_FLAG_COMMAND		(1 << 4) /* Ability to read/set commands */
#define EVENT_FLAG_AGENT		(1 << 5) /* Ability to read/set agent info */
#define EVENT_FLAG_USER                 (1 << 6) /* Ability to read/set user info */
#define EVENT_FLAG_CONFIG		(1 << 7) /* Ability to modify configurations */
#define EVENT_FLAG_DTMF  		(1 << 8) /* Ability to read DTMF events */
#define EVENT_FLAG_REPORTING		(1 << 9) /* Reporting events such as rtcp sent */
#define EVENT_FLAG_CDR			(1 << 10) /* CDR events */
#define EVENT_FLAG_DIALPLAN		(1 << 11) /* Dialplan events (VarSet, NewExten) */
#define EVENT_FLAG_ORIGINATE	(1 << 12) /* Originate a call to an extension */
#define EVENT_FLAG_AGI			(1 << 13) /* AGI events */
#define EVENT_FLAG_HOOKRESPONSE		(1 << 14) /* Hook Response */
#define EVENT_FLAG_CC			(1 << 15) /* Call Completion events */
#define EVENT_FLAG_AOC			(1 << 16) /* Advice Of Charge events */
#define EVENT_FLAG_TEST			(1 << 17) /* Test event used to signal the Asterisk Test Suite */
/*@} */

/*! \brief Export manager structures */
#define AST_MAX_MANHEADERS 128

/*! \brief Manager Helper Function */
typedef int (*manager_hook_t)(int, const char *, char *);

struct manager_custom_hook {
	/*! Identifier */
	char *file;
	/*! helper function */
	manager_hook_t helper;
	/*! Linked list information */
	AST_RWLIST_ENTRY(manager_custom_hook) list;
};

/*! \brief Check if AMI is enabled */
int check_manager_enabled(void);

/*! \brief Check if AMI/HTTP is enabled */
int check_webmanager_enabled(void);

/*! Add a custom hook to be called when an event is fired 
 \param hook struct manager_custom_hook object to add
*/
void ast_manager_register_hook(struct manager_custom_hook *hook);

/*! Delete a custom hook to be called when an event is fired
    \param hook struct manager_custom_hook object to delete
*/
void ast_manager_unregister_hook(struct manager_custom_hook *hook);

/*! \brief Registered hooks can call this function to invoke actions and they will receive responses through registered callback
 * \param hook the file identifier specified in manager_custom_hook struct when registering a hook
 * \param msg ami action mesage string e.g. "Action: SipPeers\r\n"

 * \retval 0 on Success
 * \retval non-zero on Failure
*/
int ast_hook_send_action(struct manager_custom_hook *hook, const char *msg);

struct mansession;

struct message {
	unsigned int hdrcount;
	const char *headers[AST_MAX_MANHEADERS];
};

struct manager_action {
	/*! Name of the action */
	const char *action;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(synopsis);	/*!< Synopsis text (short description). */
		AST_STRING_FIELD(description);	/*!< Description (help text) */
		AST_STRING_FIELD(syntax);	/*!< Syntax text */
		AST_STRING_FIELD(arguments);	/*!< Description of each argument. */
		AST_STRING_FIELD(seealso);	/*!< See also */
	);
	/*! Permission required for action.  EVENT_FLAG_* */
	int authority;
	/*! Function to be called */
	int (*func)(struct mansession *s, const struct message *m);
	/*! Where the documentation come from. */
	enum ast_doc_src docsrc;
	/*! For easy linking */
	AST_RWLIST_ENTRY(manager_action) list;
};

/*! \brief External routines may register/unregister manager callbacks this way 
 * \note  Use ast_manager_register2() to register with help text for new manager commands */
#define ast_manager_register(a, b, c, d) ast_manager_register2(a, b, c, d, NULL)

/*! \brief Register a manager callback using XML documentation to describe the manager. */
#define ast_manager_register_xml(a, b, c) ast_manager_register2(a, b, c, NULL, NULL)

/*! \brief Register a manager command with the manager interface 
 	\param action Name of the requested Action:
	\param authority Required authority for this command
	\param func Function to call for this command
	\param synopsis Help text (one line, up to 30 chars) for CLI manager show commands
	\param description Help text, several lines
*/
int ast_manager_register2(
	const char *action,
	int authority,
	int (*func)(struct mansession *s, const struct message *m),
	const char *synopsis,
	const char *description);

/*! \brief Unregister a registered manager command 
	\param action Name of registered Action:
*/
int ast_manager_unregister( char *action );

/*! 
 * \brief Verify a session's read permissions against a permission mask.  
 * \param ident session identity
 * \param perm permission mask to verify
 * \retval 1 if the session has the permission mask capabilities
 * \retval 0 otherwise
 */
int astman_verify_session_readpermissions(uint32_t ident, int perm);

/*!
 * \brief Verify a session's write permissions against a permission mask.  
 * \param ident session identity
 * \param perm permission mask to verify
 * \retval 1 if the session has the permission mask capabilities, otherwise 0
 * \retval 0 otherwise
 */
int astman_verify_session_writepermissions(uint32_t ident, int perm);

/*! \brief External routines may send asterisk manager events this way 
 *  	\param category	Event category, matches manager authorization
	\param event	Event name
	\param contents	Contents of event
*/

/* XXX the parser in gcc 2.95 gets confused if you don't put a space
 * between the last arg before VA_ARGS and the comma */
#define manager_event(category, event, contents , ...)	\
        __ast_manager_event_multichan(category, event, 0, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, contents , ## __VA_ARGS__)
#define ast_manager_event(chan, category, event, contents , ...) \
	do { \
		struct ast_channel *_chans[] = { chan, }; \
		__ast_manager_event_multichan(category, event, 1, _chans, __FILE__, __LINE__, __PRETTY_FUNCTION__, contents , ## __VA_ARGS__); \
	} while (0)
#define ast_manager_event_multichan(category, event, nchans, chans, contents , ...) \
	__ast_manager_event_multichan(category, event, nchans, chans, __FILE__, __LINE__, __PRETTY_FUNCTION__, contents , ## __VA_ARGS__);

/*! External routines may send asterisk manager events this way
 * \param category Event category, matches manager authorization
 * \param event Event name
 * \param chancount Number of channels in chans parameter
 * \param chans A pointer to an array of channels involved in the event
 * \param contents Format string describing event
 * \since 1.8
*/
int __ast_manager_event_multichan(int category, const char *event, int chancount,
		struct ast_channel **chans, const char *file, int line, const char *func,
		const char *contents, ...) __attribute__((format(printf, 8, 9)));

/*! \brief Get header from mananger transaction */
const char *astman_get_header(const struct message *m, char *var);

/*! \brief Get a linked list of the Variable: headers */
struct ast_variable *astman_get_variables(const struct message *m);

/*! \brief Send error in manager transaction */
void astman_send_error(struct mansession *s, const struct message *m, char *error);

/*! \brief Send response in manager transaction */
void astman_send_response(struct mansession *s, const struct message *m, char *resp, char *msg);

/*! \brief Send ack in manager transaction */
void astman_send_ack(struct mansession *s, const struct message *m, char *msg);

/*! \brief Send ack in manager list transaction */
void astman_send_listack(struct mansession *s, const struct message *m, char *msg, char *listflag);

void __attribute__((format(printf, 2, 3))) astman_append(struct mansession *s, const char *fmt, ...);

/*! \brief Determinie if a manager session ident is authenticated */
int astman_is_authed(uint32_t ident);

/*! \brief Called by Asterisk initialization */
int init_manager(void);

/*! \brief Called by Asterisk module functions and the CLI command */
int reload_manager(void);

/*! 
 * \brief Add a datastore to a session
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */

int astman_datastore_add(struct mansession *s, struct ast_datastore *datastore);

/*! 
 * \brief Remove a datastore from a session
 *
 * \retval 0 success
 * \retval non-zero failure
 * \since 1.6.1
 */
int astman_datastore_remove(struct mansession *s, struct ast_datastore *datastore);

/*! 
 * \brief Find a datastore on a session
 *
 * \retval pointer to the datastore if found
 * \retval NULL if not found
 * \since 1.6.1
 */
struct ast_datastore *astman_datastore_find(struct mansession *s, const struct ast_datastore_info *info, const char *uid);

#endif /* _ASTERISK_MANAGER_H */
