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

#define AMI_VERSION                     "2.8.0"
#define DEFAULT_MANAGER_PORT 5038	/* Default port for Asterisk management via TCP */
#define DEFAULT_MANAGER_TLS_PORT 5039	/* Default port for Asterisk management via TCP */

/*! \name Constant return values
 *\note Currently, returning anything other than zero causes the session to terminate.
 */
/*@{ */
#define	AMI_SUCCESS	(0)
#define	AMI_DESTROY	(-1)
/*@} */

/*! \name Manager event classes */
/*@{ */
#define EVENT_FLAG_SYSTEM           (1 << 0) /* System events such as module load/unload */
#define EVENT_FLAG_CALL             (1 << 1) /* Call event, such as state change, etc */
#define EVENT_FLAG_LOG              (1 << 2) /* Log events */
#define EVENT_FLAG_VERBOSE          (1 << 3) /* Verbose messages */
#define EVENT_FLAG_COMMAND          (1 << 4) /* Ability to read/set commands */
#define EVENT_FLAG_AGENT            (1 << 5) /* Ability to read/set agent info */
#define EVENT_FLAG_USER             (1 << 6) /* Ability to read/set user info */
#define EVENT_FLAG_CONFIG           (1 << 7) /* Ability to modify configurations */
#define EVENT_FLAG_DTMF             (1 << 8) /* Ability to read DTMF events */
#define EVENT_FLAG_REPORTING        (1 << 9) /* Reporting events such as rtcp sent */
#define EVENT_FLAG_CDR              (1 << 10) /* CDR events */
#define EVENT_FLAG_DIALPLAN         (1 << 11) /* Dialplan events (VarSet, NewExten) */
#define EVENT_FLAG_ORIGINATE        (1 << 12) /* Originate a call to an extension */
#define EVENT_FLAG_AGI              (1 << 13) /* AGI events */
#define EVENT_FLAG_HOOKRESPONSE     (1 << 14) /* Hook Response */
#define EVENT_FLAG_CC               (1 << 15) /* Call Completion events */
#define EVENT_FLAG_AOC              (1 << 16) /* Advice Of Charge events */
#define EVENT_FLAG_TEST             (1 << 17) /* Test event used to signal the Asterisk Test Suite */
#define EVENT_FLAG_SECURITY         (1 << 18) /* Security Message as AMI Event */
/*XXX Why shifted by 30? XXX */
#define EVENT_FLAG_MESSAGE          (1 << 30) /* MESSAGE events. */
/*@} */

/*! \brief Export manager structures */
#define AST_MAX_MANHEADERS 128

/*! \brief Manager Helper Function
 *
 * \param category The class authorization category of the event
 * \param event The name of the event being raised
 * \param body The body of the event
 *
 * \retval 0 Success
 * \retval non-zero Error
 */
typedef int (*manager_hook_t)(int category, const char *event, char *body);

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
	/*! Possible list element response events. */
	struct ast_xml_doc_item *list_responses;
	/*! Final response event. */
	struct ast_xml_doc_item *final_response;
	/*! Permission required for action.  EVENT_FLAG_* */
	int authority;
	/*! Function to be called */
	int (*func)(struct mansession *s, const struct message *m);
	struct ast_module *module;		/*!< Module this action belongs to */
	/*! Where the documentation come from. */
	enum ast_doc_src docsrc;
	/*! For easy linking */
	AST_RWLIST_ENTRY(manager_action) list;
	/*!
	 * \brief TRUE if the AMI action is registered and the callback can be called.
	 *
	 * \note Needed to prevent a race between calling the callback
	 * function and unregestring the AMI action object.
	 */
	unsigned int registered:1;
};

/*! \brief External routines may register/unregister manager callbacks this way 
 * \note  Use ast_manager_register2() to register with help text for new manager commands */
#define ast_manager_register(action, authority, func, synopsis) ast_manager_register2(action, authority, func, AST_MODULE_SELF, synopsis, NULL)

/*! \brief Register a manager callback using XML documentation to describe the manager. */
#define ast_manager_register_xml(action, authority, func) ast_manager_register2(action, authority, func, AST_MODULE_SELF, NULL, NULL)

/*!
 * \brief Register a manager callback using XML documentation to describe the manager.
 *
 * \note For Asterisk core modules that are not independently
 * loadable.
 *
 * \warning If you use ast_manager_register_xml() instead when
 * you need to use this function, Asterisk will crash on load.
 */
#define ast_manager_register_xml_core(action, authority, func) ast_manager_register2(action, authority, func, NULL, NULL, NULL)

/*!
 * \brief Register a manager command with the manager interface
 * \param action Name of the requested Action:
 * \param authority Required authority for this command
 * \param func Function to call for this command
 * \param module The module containing func.  (NULL if module is part of core and not loadable)
 * \param synopsis Help text (one line, up to 30 chars) for CLI manager show commands
 * \param description Help text, several lines
 */
int ast_manager_register2(
	const char *action,
	int authority,
	int (*func)(struct mansession *s, const struct message *m),
	struct ast_module *module,
	const char *synopsis,
	const char *description);

/*!
 * \brief Unregister a registered manager command
 * \param action Name of registered Action:
 */
int ast_manager_unregister(const char *action);

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
 * \param file, line, func
 * \param contents Format string describing event
 * \param ...
 * \since 1.8
*/
int __ast_manager_event_multichan(int category, const char *event, int chancount,
		struct ast_channel **chans, const char *file, int line, const char *func,
		const char *contents, ...) __attribute__((format(printf, 8, 9)));

/*! \brief Get header from mananger transaction */
const char *astman_get_header(const struct message *m, char *var);

/*! \brief Get a linked list of the Variable: headers
 *
 *  \note Order of variables is reversed from the order they are specified in
 *        the manager message
 */
struct ast_variable *astman_get_variables(const struct message *m);

enum variable_orders {
	ORDER_NATURAL,
	ORDER_REVERSE
};

/*! \brief Get a linked list of the Variable: headers with order specified */
struct ast_variable *astman_get_variables_order(const struct message *m, enum variable_orders order);

/*! \brief Send error in manager transaction */
void astman_send_error(struct mansession *s, const struct message *m, char *error);

/*! \brief Send error in manager transaction (with va_args support) */
void __attribute__((format(printf, 3, 4))) astman_send_error_va(struct mansession *s, const struct message *m, const char *fmt, ...);

/*! \brief Send response in manager transaction */
void astman_send_response(struct mansession *s, const struct message *m, char *resp, char *msg);

/*! \brief Send ack in manager transaction */
void astman_send_ack(struct mansession *s, const struct message *m, char *msg);

/*!
 * \brief Send ack in manager transaction to begin a list.
 *
 * \param s - AMI session control struct.
 * \param m - AMI action request that started the list.
 * \param msg - Message contents describing the list to follow.
 * \param listflag - Should always be set to "start".
 *
 * \note You need to call astman_send_list_complete_start() and
 * astman_send_list_complete_end() to send the AMI list completion event.
 *
 * \return Nothing
 */
void astman_send_listack(struct mansession *s, const struct message *m, char *msg, char *listflag);

/*!
 * \brief Start the list complete event.
 * \since 13.2.0
 *
 * \param s - AMI session control struct.
 * \param m - AMI action request that started the list.
 * \param event_name - AMI list complete event name.
 * \param count - Number of items in the list.
 *
 * \note You need to call astman_send_list_complete_end() to end
 * the AMI list completion event.
 *
 * \note Between calling astman_send_list_complete_start() and
 * astman_send_list_complete_end() you can add additonal headers
 * using astman_append().
 *
 * \return Nothing
 */
void astman_send_list_complete_start(struct mansession *s, const struct message *m, const char *event_name, int count);

/*!
 * \brief End the list complete event.
 * \since 13.2.0
 *
 * \param s - AMI session control struct.
 *
 * \note You need to call astman_send_list_complete_start() to start
 * the AMI list completion event.
 *
 * \note Between calling astman_send_list_complete_start() and
 * astman_send_list_complete_end() you can add additonal headers
 * using astman_append().
 *
 * \return Nothing
 */
void astman_send_list_complete_end(struct mansession *s);

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

/*!
 * \brief append an event header to an ast string
 * \since 12
 *
 * \param fields_string pointer to an ast_string pointer. It may be a pointer to a
 *        NULL ast_str pointer, in which case the ast_str will be initialized.
 * \param header The header being applied
 * \param value the value of the header
 *
 * \retval 0 if successful
 * \retval non-zero on failure
 */
int ast_str_append_event_header(struct ast_str **fields_string,
	const char *header, const char *value);

/*! \brief Struct representing a snapshot of channel state */
struct ast_channel_snapshot;

/*!
 * \brief Generate the AMI message body from a channel snapshot
 * \since 12
 *
 * \param snapshot the channel snapshot for which to generate an AMI message
 *                 body
 * \param prefix What to prepend to the channel fields
 *
 * \retval NULL on error
 * \retval ast_str* on success (must be ast_freed by caller)
 */
struct ast_str *ast_manager_build_channel_state_string_prefix(
		const struct ast_channel_snapshot *snapshot,
		const char *prefix);

/*!
 * \brief Generate the AMI message body from a channel snapshot
 * \since 12
 *
 * \param snapshot the channel snapshot for which to generate an AMI message
 *                 body
 *
 * \retval NULL on error
 * \retval ast_str* on success (must be ast_freed by caller)
 */
struct ast_str *ast_manager_build_channel_state_string(
		const struct ast_channel_snapshot *snapshot);

/*! \brief Struct representing a snapshot of bridge state */
struct ast_bridge_snapshot;

/*!
 * \since 12
 * \brief Callback used to determine whether a key should be skipped when converting a
 *  JSON object to a manager blob
 * \param key Key from JSON blob to be evaluated
 * \retval non-zero if the key should be excluded
 * \retval zero if the key should not be excluded
 */
typedef int (*key_exclusion_cb)(const char *key);

struct ast_json;

/*!
 * \since 12
 * \brief Convert a JSON object into an AMI compatible string
 *
 * \param blob The JSON blob containing key/value pairs to convert
 * \param exclusion_cb A \ref key_exclusion_cb pointer to a function that will exclude
 * keys from the final AMI string
 *
 * \retval A malloc'd \ref ast_str object. Callers of this function should free
 * the returned \ref ast_str object
 * \retval NULL on error
 */
struct ast_str *ast_manager_str_from_json_object(struct ast_json *blob, key_exclusion_cb exclusion_cb);

/*!
 * \brief Generate the AMI message body from a bridge snapshot
 * \since 12
 *
 * \param snapshot the bridge snapshot for which to generate an AMI message
 *                 body
 * \param prefix What to prepend to the bridge fields
 *
 * \retval NULL on error
 * \retval ast_str* on success (must be ast_freed by caller)
 */
struct ast_str *ast_manager_build_bridge_state_string_prefix(
	const struct ast_bridge_snapshot *snapshot,
	const char *prefix);

/*!
 * \brief Generate the AMI message body from a bridge snapshot
 * \since 12
 *
 * \param snapshot the bridge snapshot for which to generate an AMI message
 *                 body
 *
 * \retval NULL on error
 * \retval ast_str* on success (must be ast_freed by caller)
 */
struct ast_str *ast_manager_build_bridge_state_string(
	const struct ast_bridge_snapshot *snapshot);

/*! \brief Struct containing info for an AMI event to send out. */
struct ast_manager_event_blob {
	int event_flags;		/*!< Flags the event should be raised with. */
	const char *manager_event;	/*!< The event to be raised, should be a string literal. */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(extra_fields);	/*!< Extra fields to include in the event. */
	);
};

/*!
 * \since 12
 * \brief Construct a \ref ast_manager_event_blob.
 *
 * The returned object is AO2 managed, so clean up with ao2_cleanup().
 *
 * \param event_flags Flags the event should be raised with.
 * \param manager_event The event to be raised, should be a string literal.
 * \param extra_fields_fmt Format string for extra fields to include.
 *                         Or NO_EXTRA_FIELDS for no extra fields.
 *
 * \return New \ref ast_manager_snapshot_event object.
 * \return \c NULL on error.
 */
struct ast_manager_event_blob *
__attribute__((format(printf, 3, 4)))
ast_manager_event_blob_create(
	int event_flags,
	const char *manager_event,
	const char *extra_fields_fmt,
	...);

/*! GCC warns about blank or NULL format strings. So, shenanigans! */
#define NO_EXTRA_FIELDS "%s", ""

/*!
 * \since 12
 * \brief Initialize support for AMI system events.
 * \retval 0 on success
 * \retval non-zero on error
 */
int manager_system_init(void);

/*!
 * \brief Initialize support for AMI channel events.
 * \retval 0 on success.
 * \retval non-zero on error.
 * \since 12
 */
int manager_channels_init(void);

/*!
 * \since 12
 * \brief Initialize support for AMI MWI events.
 * \retval 0 on success
 * \retval non-zero on error
 */
int manager_mwi_init(void);

/*!
 * \brief Initialize support for AMI channel events.
 * \return 0 on success.
 * \return non-zero on error.
 * \since 12
 */
int manager_bridging_init(void);

/*!
 * \brief Initialize support for AMI endpoint events.
 * \return 0 on success.
 * \return non-zero on error.
 * \since 12
 */
int manager_endpoints_init(void);

/*!
 * \since 12
 * \brief Get the \ref stasis_message_type for generic messages
 *
 * A generic AMI message expects a JSON only payload. The payload must have the following
 * structure:
 * {type: s, class_type: i, event: [ {s: s}, ...] }
 *
 * - type is the AMI event type
 * - class_type is the class authorization type for the event
 * - event is a list of key/value tuples to be sent out in the message
 *
 * \retval A \ref stasis_message_type for AMI messages
 */
struct stasis_message_type *ast_manager_get_generic_type(void);

/*!
 * \since 12
 * \brief Get the \ref stasis topic for AMI
 *
 * \retval The \ref stasis topic for AMI
 * \retval NULL on error
 */
struct stasis_topic *ast_manager_get_topic(void);

/*!
 * \since 12
 * \brief Publish an event to AMI
 *
 * \param type The type of AMI event to publish
 * \param class_type The class on which to publish the event
 * \param obj The event data to be published.
 *
 * Publishes a message to the \ref stasis message bus solely for the consumption of AMI.
 * The message will be of the type provided by \ref ast_manager_get_type, and will be
 * published to the topic provided by \ref ast_manager_get_topic. As such, the JSON must
 * be constructed as defined by the \ref ast_manager_get_type message.
 */
void ast_manager_publish_event(const char *type, int class_type, struct ast_json *obj);

/*!
 * \since 12
 * \brief Get the \ref stasis_message_router for AMI
 *
 * \retval The \ref stasis_message_router for AMI
 * \retval NULL on error
 */
struct stasis_message_router *ast_manager_get_message_router(void);

#endif /* _ASTERISK_MANAGER_H */
